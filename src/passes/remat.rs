//! Recompute cheap values where a leaf function runs out of registers.
//!
//!   bb0: %t = gep %p, (sext %us) * 48         bb0: %t = gep %p, (sext %us) * 48
//!        ... uses %t ...                           ... uses %t ...
//!        condbr %c, bb1, bb2                       condbr %c, bb1, bb2
//!   bb1: ... (more live values than   =>     bb1: ...
//!        registers) ...                            %t2 = gep %p, (sext %us) * 48
//!        load %t                                   load %t2
//!
//! A leaf function keeps its values in caller-saved registers; one more live
//! value than those hold costs a callee-saved register, saved and restored
//! (with a frame) on every call, however rarely the block that needs it
//! runs. When the values live at some point of a block outnumber the
//! budget, a value that is live through that point only to be read later in
//! the block, and is computed by up to four cheap instructions from values
//! still live at its first read there, is recomputed right before that read
//! instead. GVN merged such values (the same address in an inlined helper
//! called twice); this splits them again where that pays.
//!
//! The pressure count follows the register allocator: a GEP with a constant
//! offset only used as a load or store address holds its base's register
//! (the backend folds the offset into the access), operands dying at an
//! instruction still hold theirs while its result takes one (except where a
//! simple instruction's result may reuse one), and float values use other
//! registers. Only blocks outside loops get recomputations, and only when
//! then every block fits: otherwise the function saves a register anyway.
//! Runs once, last; GVN does not run again to undo it.

use crate::common::fx_hash::{FxHashMap, FxHashSet};
use crate::common::source::Span;
use crate::common::types::IrType;
use crate::ir::analysis::CfgAnalysis;
use crate::ir::reexports::{Instruction, IrBinOp, IrFunction, Operand, Value};
use super::copy_prop::{replace_operands_in_instruction, replace_operands_in_terminator};
use super::loop_analysis;

/// Most instructions recomputed for one value.
const MAX_CHAIN: usize = 4;
/// Most values recomputed per function.
const MAX_REMATS: usize = 16;

/// AArch64: the general registers a leaf function allocates without saving
/// any (x10-x15, and x3-x8 above its argument registers), or None when the
/// backend gives it fewer (see ArmCodegen::leaf_extra_caller_saved) or it is
/// not a leaf.
pub(crate) fn aarch64_leaf_budget(func: &IrFunction) -> Option<usize> {
    if func.is_variadic {
        return None;
    }
    let wide = |ty: &IrType| matches!(ty, IrType::I128 | IrType::U128 | IrType::F128);
    let mut has_div = false;
    let mut gp_params = 0usize;
    for inst in func.blocks.iter().flat_map(|b| &b.instructions) {
        match inst {
            Instruction::BinOp { op, ty, .. } => {
                if wide(ty) {
                    return None;
                }
                has_div |= matches!(op, IrBinOp::SDiv | IrBinOp::UDiv | IrBinOp::SRem | IrBinOp::URem);
            }
            Instruction::Cast { from_ty, to_ty, .. } if wide(from_ty) || wide(to_ty) => return None,
            Instruction::Load { ty, .. } | Instruction::Store { ty, .. }
            | Instruction::UnaryOp { ty, .. } | Instruction::Cmp { ty, .. } if wide(ty) => return None,
            Instruction::ParamRef { ty, .. } => {
                if wide(ty) {
                    return None;
                }
                gp_params += usize::from(!ty.is_float());
            }
            Instruction::Call { .. } | Instruction::CallIndirect { .. } | Instruction::InlineAsm { .. }
            | Instruction::Intrinsic { .. } | Instruction::AtomicLoad { .. } | Instruction::AtomicStore { .. }
            | Instruction::AtomicRmw { .. } | Instruction::AtomicCmpxchg { .. } | Instruction::Fence { .. }
            | Instruction::VaArg { .. } | Instruction::VaStart { .. } | Instruction::VaEnd { .. }
            | Instruction::VaCopy { .. } | Instruction::DynAlloca { .. } | Instruction::Memcpy { .. } => return None,
            _ => {}
        }
    }
    if func.uses_sret || func.params.iter().any(|p| p.struct_size.is_some()) {
        return None;
    }
    let first = gp_params.max(if has_div { 4 } else { 3 });
    Some(6 + 9usize.saturating_sub(first))
}

/// Recompute values until every block fits the budget. All or nothing:
/// the recomputations only pay when no callee-saved register (and frame)
/// is needed at all.
pub(crate) fn remat_function(func: &mut IrFunction, budget: usize) -> usize {
    if func.blocks.len() < 2 {
        return 0;
    }
    let saved = (func.blocks.clone(), func.next_value_id);
    for done in 0..=MAX_REMATS {
        match remat_once(func, budget) {
            Step::Fits => return done,
            Step::Applied if done < MAX_REMATS => {}
            _ => break,
        }
    }
    (func.blocks, func.next_value_id) = saved;
    0
}

enum Step {
    /// No block is over budget.
    Fits,
    /// One value was recomputed.
    Applied,
    /// A block over budget has nothing to recompute.
    Stuck,
}

fn is_cheap(inst: &Instruction) -> bool {
    match inst {
        Instruction::BinOp { op, ty, .. } => !ty.is_float()
            && !matches!(op, IrBinOp::SDiv | IrBinOp::UDiv | IrBinOp::SRem | IrBinOp::URem),
        Instruction::Cast { from_ty, to_ty, .. } => !from_ty.is_float() && !to_ty.is_float(),
        Instruction::GetElementPtr { .. } => true,
        _ => false,
    }
}

/// Recompute one value in the first block over budget.
fn remat_once(func: &mut IrFunction, budget: usize) -> Step {
    let cfg = CfgAnalysis::build(func);
    let n = func.blocks.len();
    let mut depth = vec![0usize; n];
    for lp in loop_analysis::find_natural_loops(n, &cfg.preds, &cfg.succs, &cfg.idom) {
        for &b in &lp.body {
            depth[b] += 1;
        }
    }

    let mut def_at: FxHashMap<u32, (usize, usize)> = FxHashMap::default();
    for (bi, block) in func.blocks.iter().enumerate() {
        for (ii, inst) in block.instructions.iter().enumerate() {
            if let Some(d) = inst.dest() {
                def_at.insert(d.0, (bi, ii));
            }
        }
    }
    // The value whose register a live value occupies: itself, None for
    // allocas and floats, or for a constant-offset GEP used only as a load
    // or store address (folded into the access) its base's.
    let mut other_use: FxHashSet<u32> = FxHashSet::default();
    for block in &func.blocks {
        for inst in &block.instructions {
            match inst {
                Instruction::Load { .. } => {}
                Instruction::Store { val: Operand::Value(v), .. } => { other_use.insert(v.0); }
                Instruction::Store { .. } => {}
                _ => inst.for_each_used_value(|v| { other_use.insert(v); }),
            }
        }
        block.terminator.for_each_used_value(|v| { other_use.insert(v); });
    }
    let holder = |mut v: u32| -> Option<u32> {
        for _ in 0..8 {
            let Some(&(b, i)) = def_at.get(&v) else { return Some(v) };
            match &func.blocks[b].instructions[i] {
                Instruction::Alloca { .. } => return None,
                Instruction::GetElementPtr { base, offset: Operand::Const(_), .. } if !other_use.contains(&v) => v = base.0,
                inst => return if inst.result_type().is_some_and(|t| t.is_float()) { None } else { Some(v) },
            }
        }
        Some(v)
    };
    let regs = |s: &FxHashSet<u32>| s.iter().filter_map(|&v| holder(v)).collect::<FxHashSet<u32>>().len();

    // Liveness: phi operands are live out of the predecessor they come from.
    let label_idx: FxHashMap<_, usize> = func.blocks.iter().enumerate().map(|(i, b)| (b.label, i)).collect();
    let mut phi_out: Vec<FxHashSet<u32>> = vec![FxHashSet::default(); n];
    let mut gen: Vec<FxHashSet<u32>> = vec![FxHashSet::default(); n];
    let mut kill: Vec<FxHashSet<u32>> = vec![FxHashSet::default(); n];
    for (bi, block) in func.blocks.iter().enumerate() {
        for inst in &block.instructions {
            if let Instruction::Phi { incoming, .. } = inst {
                for (op, from) in incoming {
                    if let (Operand::Value(v), Some(&p)) = (op, label_idx.get(from)) {
                        phi_out[p].insert(v.0);
                    }
                }
            } else {
                inst.for_each_used_value(|v| if !kill[bi].contains(&v) { gen[bi].insert(v); });
            }
            if let Some(d) = inst.dest() {
                kill[bi].insert(d.0);
            }
        }
        block.terminator.for_each_used_value(|v| if !kill[bi].contains(&v) { gen[bi].insert(v); });
    }
    let mut live_in: Vec<FxHashSet<u32>> = gen.clone();
    let mut live_out: Vec<FxHashSet<u32>> = vec![FxHashSet::default(); n];
    let mut changed = true;
    while changed {
        changed = false;
        for bi in (0..n).rev() {
            let mut out = phi_out[bi].clone();
            for &s in cfg.succs.row(bi) {
                out.extend(live_in[s as usize].iter().copied());
            }
            let mut inn = gen[bi].clone();
            inn.extend(out.iter().filter(|v| !kill[bi].contains(v)).copied());
            if inn.len() != live_in[bi].len() || out.len() != live_out[bi].len() {
                live_in[bi] = inn;
                live_out[bi] = out;
                changed = true;
            }
        }
    }

    for bi in 0..n {
        let block = &func.blocks[bi];
        // after[i]: values live right after instruction i.
        let len = block.instructions.len();
        let mut after: Vec<FxHashSet<u32>> = vec![FxHashSet::default(); len];
        let mut live = live_out[bi].clone();
        block.terminator.for_each_used_value(|v| { live.insert(v); });
        for i in (0..len).rev() {
            after[i] = live.clone();
            let inst = &block.instructions[i];
            if let Some(d) = inst.dest() {
                live.remove(&d.0);
            }
            if !matches!(inst, Instruction::Phi { .. }) {
                inst.for_each_used_value(|v| { live.insert(v); });
            }
        }
        // At an instruction, its operands that die there still hold their
        // registers while its result takes one (allocator intervals are
        // inclusive), except that a simple instruction's result may take
        // the register of an operand dying there (see dest_reuse_points in
        // the register allocator).
        let pressure = |i: usize| {
            let inst = &block.instructions[i];
            let mut at = after[i].clone();
            if let Some(d) = inst.dest() {
                at.insert(d.0);
            }
            let held = regs(&at);
            if matches!(inst, Instruction::Phi { .. }) {
                return held;
            }
            inst.for_each_used_value(|v| { at.insert(v); });
            let all = regs(&at);
            let simple = matches!(inst, Instruction::Copy { .. } | Instruction::Cast { .. } | Instruction::BinOp { .. }
                | Instruction::UnaryOp { .. } | Instruction::Cmp { .. } | Instruction::GetElementPtr { .. } | Instruction::Load { .. });
            let shares = simple && all > held && inst.dest().and_then(|d| holder(d.0)) == inst.dest().map(|d| d.0);
            all - usize::from(shares)
        };
        let Some((peak, max)) = (0..len).map(|i| (i, pressure(i))).max_by_key(|&(i, p)| (p, std::cmp::Reverse(i)))
        else { continue };
        // Recomputing in a loop costs every iteration; a saved register
        // costs once per call.
        if max <= budget {
            continue;
        }
        if depth[bi] > 0 {
            return Step::Stuck;
        }

        // Candidates: live through the peak, read later here, dead after,
        // and the only reason a register is held there.
        let first_use = |v: u32| (peak + 1..len).find(|&i| block.instructions[i].used_values().contains(&v)).unwrap_or(len);
        let mut best: Option<(usize, u32, usize, Vec<u32>)> = None;
        let held = regs(&after[peak]);
        let mut cands: Vec<u32> = after[peak].iter().copied()
            .filter(|&v| !live_out[bi].contains(&v) && {
                let mut rest = after[peak].clone();
                rest.remove(&v);
                regs(&rest) < held
            })
            .collect();
        cands.sort_unstable();
        for v in cands {
            let Some(&(db, _)) = def_at.get(&v) else { continue };
            if db == bi {
                continue;
            }
            let u = first_use(v);
            let live_at_u = if u == 0 { &live_in[bi] } else { &after[u - 1] };
            // The chain in dependency order, its values not live at u.
            let mut chain: Vec<u32> = Vec::new();
            if !build_chain(func, &def_at, v, live_at_u, &mut chain) {
                continue;
            }
            if best.as_ref().is_none_or(|b| chain.len() < b.0) {
                best = Some((chain.len(), v, u, chain));
            }
        }
        let Some((_, v, u, chain)) = best else { return Step::Stuck };
        apply(func, bi, u, v, &chain, &def_at);
        return Step::Applied;
    }
    Step::Fits
}

/// Push `v`'s defining instruction and those of its operands that are not
/// live at the recompute point, operands first, up to MAX_CHAIN in all.
fn build_chain(func: &IrFunction, def_at: &FxHashMap<u32, (usize, usize)>, v: u32,
               live: &FxHashSet<u32>, chain: &mut Vec<u32>) -> bool {
    if chain.contains(&v) {
        return true;
    }
    let Some(&(b, i)) = def_at.get(&v) else { return false };
    let inst = &func.blocks[b].instructions[i];
    if !is_cheap(inst) || chain.len() >= MAX_CHAIN {
        return false;
    }
    for w in inst.used_values() {
        if !live.contains(&w) && !build_chain(func, def_at, w, live, chain) {
            return false;
        }
    }
    if chain.len() >= MAX_CHAIN {
        return false;
    }
    chain.push(v);
    true
}

/// Insert copies of `chain` (ending with `v`) before instruction `u` of
/// block `bi` and make the block's reads of `v` from `u` on use the copy.
fn apply(func: &mut IrFunction, bi: usize, u: usize, v: u32, chain: &[u32], def_at: &FxHashMap<u32, (usize, usize)>) {
    let mut map: Vec<Option<Operand>> = Vec::new();
    let set = |map: &mut Vec<Option<Operand>>, from: u32, to: Value| {
        if map.len() <= from as usize {
            map.resize(from as usize + 1, None);
        }
        map[from as usize] = Some(Operand::Value(to));
    };
    let mut clones: Vec<Instruction> = Vec::new();
    for &c in chain {
        let (b, i) = def_at[&c];
        let mut inst = func.blocks[b].instructions[i].clone();
        replace_operands_in_instruction(&mut inst, &map);
        let fresh = Value(func.next_value_id);
        func.next_value_id += 1;
        set_dest(&mut inst, fresh);
        set(&mut map, c, fresh);
        clones.push(inst);
    }
    // Only v is read past the chain.
    let new_v = map[v as usize];
    let mut only_v: Vec<Option<Operand>> = vec![None; v as usize + 1];
    only_v[v as usize] = new_v;
    let block = &mut func.blocks[bi];
    for inst in &mut block.instructions[u..] {
        replace_operands_in_instruction(inst, &only_v);
    }
    replace_operands_in_terminator(&mut block.terminator, &only_v);
    let k = clones.len();
    block.instructions.splice(u..u, clones);
    if !block.source_spans.is_empty() {
        let span = block.source_spans.get(u).copied().unwrap_or_else(Span::dummy);
        block.source_spans.splice(u..u, std::iter::repeat_n(span, k));
    }
}

fn set_dest(inst: &mut Instruction, to: Value) {
    match inst {
        Instruction::BinOp { dest, .. } | Instruction::Cast { dest, .. } | Instruction::GetElementPtr { dest, .. } => *dest = to,
        _ => unreachable!("only cheap instructions are recomputed"),
    }
}
