//! Locals' fields kept in registers through loops (scalar promotion).
//!
//! A local whose address escapes (`&b` passed to a function) stays in
//! memory, and a loop that counts in one of its fields loads and stores it
//! every iteration: SQLite's balance_nonroot gathers cells with
//! `b.apCell[b.nCell] = ...; b.nCell++;`. In a loop that makes no calls and
//! no other memory accesses that may reach the field (strict aliasing rules
//! out the `apCell[]` store, of a pointer, reaching the `int`), the field
//! lives in a value instead: loaded once before the loop, each load in the
//! loop a copy of that value, each store a copy into it, and the value
//! stored back on every edge leaving the loop. The field is at a constant
//! offset in a local, so loading it before the loop is safe even if the
//! loop never touches it.
//!
//! Runs after phi elimination and loop rotation, like loop_hoist: the value
//! may then have several definitions (the load before the loop, the stores'
//! copies), and a rotated loop is entered from a single block.

use crate::common::fx_hash::{FxHashMap, FxHashSet};
use crate::common::source::Span;
use crate::common::types::{AddressSpace, AliasClass, IrType};
use crate::ir::analysis::CfgAnalysis;
use crate::ir::reexports::{BasicBlock, BlockId, Instruction, IrFunction, IrModule, Operand, Terminator, Value};
use super::loop_analysis::{find_natural_loops, merge_loops_by_header, NaturalLoop};
use super::mod_ref::{AliasModel, ModRef};

pub(crate) fn promote_loop_locals(module: &mut IrModule) -> usize {
    let mr = ModRef::compute(module);
    let mut next_label = module.functions.iter().flat_map(|f| &f.blocks).map(|b| b.label.0 + 1).max().unwrap_or(0);
    let mut total = 0;
    for func in module.functions.iter_mut().filter(|f| !f.is_declaration) {
        total += promote_function(func, &mr, &mut next_label);
    }
    total
}

fn promote_function(func: &mut IrFunction, mr: &ModRef, next_label: &mut u32) -> usize {
    if func.blocks.len() < 2 {
        return 0;
    }
    let mut done: FxHashSet<(BlockId, usize)> = FxHashSet::default();
    let mut total = 0;
    loop {
        let model = AliasModel::new(func, mr);
        let cfg = CfgAnalysis::build(func);
        let mut loops = merge_loops_by_header(find_natural_loops(cfg.num_blocks, &cfg.preds, &cfg.succs, &cfg.idom));
        // Inner loops first: what one stores on leaving, an outer loop may
        // then keep in a value too.
        loops.sort_by_key(|l| (l.body.len(), l.header));
        let mut progressed = false;
        for l in &loops {
            // A loop is tried again after each promotion in it, for its
            // next location.
            let key = (func.blocks[l.header].label, l.body.len());
            if done.contains(&key) {
                continue;
            }
            if promote_loop(func, &cfg, l, &model, next_label) {
                total += 1;
                progressed = true;
                break;
            }
            done.insert(key);
        }
        if !progressed {
            return total;
        }
    }
}

/// One memory access in the loop.
struct Access {
    block: usize,
    index: usize,
    store: bool,
    ptr: Value,
    ty: IrType,
    alias: AliasClass,
    /// The bytes accessed, as a value defined once and a byte range from it.
    bytes: Option<(u32, i64, i64)>,
}

/// Promote one location in loop `l`, if one qualifies.
fn promote_loop(func: &mut IrFunction, cfg: &CfgAnalysis, l: &NaturalLoop, model: &AliasModel,
                next_label: &mut u32) -> bool {
    let header = l.header;
    let outside: Vec<usize> = cfg.preds.row(header).iter().map(|&p| p as usize).filter(|p| !l.body.contains(p)).collect();
    let [guard] = outside[..] else { return false };
    if !matches!(func.blocks[guard].terminator, Terminator::Branch(_) | Terminator::CondBranch { .. }) {
        return false;
    }
    let mut blocks: Vec<usize> = l.body.iter().copied().collect();
    blocks.sort_unstable();

    let mut n = func.next_value_id as usize;
    for inst in func.blocks.iter().flat_map(|b| &b.instructions) {
        if let Some(d) = inst.dest() {
            n = n.max(d.0 as usize + 1);
        }
    }
    let mut defs = vec![0u32; n];
    let mut in_loop = vec![false; n];
    let mut result_ty: Vec<Option<IrType>> = vec![None; n];
    let mut alloca_size: FxHashMap<u32, usize> = FxHashMap::default();
    let mut const_gep: FxHashMap<u32, (u32, i64)> = FxHashMap::default();
    for (b, block) in func.blocks.iter().enumerate() {
        for inst in &block.instructions {
            if let Some(d) = inst.dest() {
                defs[d.0 as usize] += 1;
                in_loop[d.0 as usize] |= l.body.contains(&b);
                result_ty[d.0 as usize] = inst.result_type();
            }
            match inst {
                Instruction::Alloca { dest, size, volatile: false, .. } => {
                    alloca_size.insert(dest.0, *size);
                }
                Instruction::GetElementPtr { dest, base, offset: Operand::Const(c), .. } => {
                    if let Some(off) = c.to_i64() {
                        const_gep.insert(dest.0, (base.0, off));
                    }
                }
                _ => {}
            }
        }
    }
    // A pointer as a value defined once plus a constant byte offset.
    let base_offset = |ptr: u32| {
        let (mut v, mut off) = (ptr, 0i64);
        for _ in 0..16 {
            if defs[v as usize] != 1 {
                return None;
            }
            match const_gep.get(&v) {
                Some(&(base, o)) => {
                    v = base;
                    off = off.checked_add(o)?;
                }
                None => return Some((v, off)),
            }
        }
        None
    };

    // The loop's memory accesses: plain loads and stores only, and its
    // exits: branches only.
    let mut accesses: Vec<Access> = Vec::new();
    for &b in &blocks {
        for (index, inst) in func.blocks[b].instructions.iter().enumerate() {
            let (store, ptr, ty, alias) = match inst {
                Instruction::Load { ptr, ty, seg_override: AddressSpace::Default, volatile: false, alias, .. } => (false, *ptr, *ty, *alias),
                Instruction::Store { ptr, ty, seg_override: AddressSpace::Default, volatile: false, alias, .. } => (true, *ptr, *ty, *alias),
                Instruction::Intrinsic { dest_ptr: None, op, .. } if op.is_pure() => continue,
                Instruction::Load { .. } | Instruction::Store { .. } | Instruction::Call { .. }
                | Instruction::CallIndirect { .. } | Instruction::InlineAsm { .. } | Instruction::Memcpy { .. }
                | Instruction::AtomicLoad { .. } | Instruction::AtomicStore { .. } | Instruction::AtomicRmw { .. }
                | Instruction::AtomicCmpxchg { .. } | Instruction::Fence { .. } | Instruction::Intrinsic { .. }
                | Instruction::VaStart { .. } | Instruction::VaEnd { .. } | Instruction::VaCopy { .. }
                | Instruction::VaArg { .. } | Instruction::VaArgStruct { .. } | Instruction::DynAlloca { .. }
                | Instruction::StackSave { .. } | Instruction::StackRestore { .. } => return false,
                _ => continue,
            };
            let bytes = base_offset(ptr.0).map(|(v, o)| (v, o, o + ty.size() as i64));
            accesses.push(Access { block: b, index, store, ptr, ty, alias, bytes });
        }
        match &func.blocks[b].terminator {
            Terminator::Branch(_) | Terminator::CondBranch { .. } | Terminator::Return(_) | Terminator::Unreachable => {}
            Terminator::Switch { .. } if cfg.succs.row(b).iter().all(|&s| l.body.contains(&(s as usize))) => {}
            _ => return false,
        }
    }
    let disjoint = |a: Option<(u32, i64, i64)>, b: Option<(u32, i64, i64)>| match (a, b) {
        (Some((v, lo, hi)), Some((w, lo2, hi2))) => v == w && (hi <= lo2 || hi2 <= lo),
        _ => false,
    };

    // Candidates: the bytes of a store into a local, through a pointer
    // computed before the loop.
    let mut tried: FxHashSet<(u32, i64, i64)> = FxHashSet::default();
    for a in &accesses {
        let Some(loc) = a.bytes else { continue };
        if !a.store || !tried.insert(loc) {
            continue;
        }
        let ty = a.ty;
        let scalar = (ty.is_integer() || matches!(ty, IrType::Ptr | IrType::F32 | IrType::F64)) && ty.size() <= 8;
        let Some(&size) = alloca_size.get(&loc.0) else { continue };
        if !scalar || loc.1 < 0 || loc.2 as usize > size || in_loop[a.ptr.0 as usize] || defs[a.ptr.0 as usize] != 1 {
            continue;
        }
        let root = model.root(a.ptr.0);
        let mine: Vec<&Access> = accesses.iter().filter(|x| x.bytes == Some(loc)).collect();
        let class = if mine.iter().all(|x| x.alias == a.alias) { a.alias } else { AliasClass::ANY };
        // Every access to these bytes has their type, every store into
        // them a value of that type (a copy carries no conversion), and no
        // other access may touch them.
        let typed = |o: &Operand| match o {
            Operand::Const(_) => true,
            Operand::Value(v) => result_ty.get(v.0 as usize).copied().flatten() == Some(ty) && defs[v.0 as usize] == 1,
        };
        let stored_ok = mine.iter().all(|x| {
            x.ty == ty && (!x.store || match &func.blocks[x.block].instructions[x.index] {
                Instruction::Store { val, .. } => typed(val),
                _ => false,
            })
        });
        let others_ok = accesses.iter().filter(|x| x.bytes != Some(loc)).all(|x| {
            !(model.may_alias(model.root(x.ptr.0), root) && !disjoint(x.bytes, Some(loc)) && x.alias.may_alias(class))
        });
        if !stored_ok || !others_ok || mine.len() < 2 {
            continue;
        }
        apply(func, cfg, l, guard, &blocks, &mine.iter().map(|x| (x.block, x.index)).collect::<Vec<_>>(),
              a.ptr, ty, class, n as u32, next_label);
        return true;
    }
    false
}

/// Keep the location `ptr` points to in a new value through the loop.
#[allow(clippy::too_many_arguments)]
fn apply(func: &mut IrFunction, cfg: &CfgAnalysis, l: &NaturalLoop, guard: usize, blocks: &[usize],
         accesses: &[(usize, usize)], ptr: Value, ty: IrType, alias: AliasClass, value: u32, next_label: &mut u32) {
    let p = Value(value);
    func.next_value_id = func.next_value_id.max(value + 1);
    let load = Instruction::Load { dest: p, ptr, ty, seg_override: AddressSpace::Default, volatile: false, alias };
    let store = || Instruction::Store { val: Operand::Value(p), ptr, ty, seg_override: AddressSpace::Default, volatile: false, alias };
    for &(b, k) in accesses {
        let inst = &mut func.blocks[b].instructions[k];
        *inst = match inst {
            Instruction::Load { dest, .. } => Instruction::Copy { dest: *dest, src: Operand::Value(p) },
            Instruction::Store { val: Operand::Const(c), .. } => Instruction::Copy { dest: p, src: Operand::Const(c.coerce_to(ty)) },
            Instruction::Store { val, .. } => Instruction::Copy { dest: p, src: *val },
            _ => unreachable!("only loads and stores are promoted"),
        };
    }
    // New blocks, by the index they go before: the load on the guard's
    // edge into the loop, and the stores on exit edges into blocks with
    // other predecessors.
    let mut new_blocks: Vec<(usize, BasicBlock)> = Vec::new();
    let fresh = |next_label: &mut u32| {
        let label = BlockId(*next_label);
        *next_label += 1;
        label
    };
    let block = |label, instructions: Vec<Instruction>, to: BlockId| BasicBlock {
        label, instructions, terminator: Terminator::Branch(to), source_spans: Vec::new(),
    };
    let header_label = func.blocks[l.header].label;
    if matches!(func.blocks[guard].terminator, Terminator::Branch(_)) {
        push_front_or_back(&mut func.blocks[guard], load, false);
    } else {
        let label = fresh(next_label);
        retarget(&mut func.blocks[guard].terminator, header_label, label);
        new_blocks.push((l.header, block(label, vec![load], header_label)));
    }
    let mut stored: FxHashSet<usize> = FxHashSet::default();
    for &b in blocks {
        for &s in cfg.succs.row(b) {
            let s = s as usize;
            if l.body.contains(&s) {
                continue;
            }
            if cfg.preds.row(s).iter().all(|&q| l.body.contains(&(q as usize))) {
                if stored.insert(s) {
                    push_front_or_back(&mut func.blocks[s], store(), true);
                }
            } else {
                let label = fresh(next_label);
                let target = func.blocks[s].label;
                retarget(&mut func.blocks[b].terminator, target, label);
                new_blocks.push((b + 1, block(label, vec![store()], target)));
            }
        }
    }
    new_blocks.sort_by_key(|(at, _)| std::cmp::Reverse(*at));
    for (at, blk) in new_blocks {
        func.blocks.insert(at, blk);
    }
}

/// Add an instruction at the start (`front`) or the end of a block,
/// keeping its source spans parallel.
fn push_front_or_back(block: &mut BasicBlock, inst: Instruction, front: bool) {
    let parallel = !block.source_spans.is_empty() && block.source_spans.len() == block.instructions.len();
    if front {
        block.instructions.insert(0, inst);
        if parallel {
            block.source_spans.insert(0, Span::dummy());
        }
    } else {
        block.instructions.push(inst);
        if parallel {
            block.source_spans.push(Span::dummy());
        }
    }
}

/// Point every edge of a branch to `from` at `to` instead.
fn retarget(term: &mut Terminator, from: BlockId, to: BlockId) {
    match term {
        Terminator::Branch(t) if *t == from => *t = to,
        Terminator::CondBranch { true_label, false_label, .. } => {
            for t in [true_label, false_label] {
                if *t == from {
                    *t = to;
                }
            }
        }
        _ => {}
    }
}
