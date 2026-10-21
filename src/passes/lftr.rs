//! Linear function test replacement: one induction variable per loop.
//!
//! After strength reduction a loop often steps both its counter and a
//! pointer derived from it, and the counter is then only needed for the exit
//! test. This pass moves the test to the other variable so the counter goes
//! away:
//!
//!   %i = phi [0], [%i1]                       %p = phi [%a], [%p1]
//!   %p = phi [%a], [%p1]                      %p1 = gep %p, 16
//!   %i1 = add %i, 1                       =>  %c = cmp slt %p, %end
//!   %p1 = gep %p, 16                          (%end = gep %a, %n * 16 in the preheader)
//!   %c = cmp slt %i, %n
//!
//! When the counter is also shifted to index memory (`%o = shl %i, 3`), the
//! shifted value becomes the induction variable itself (stepping by 8), the
//! test compares it against `%n << 3`, and pointers stepping by the same
//! amount from offset 0 become `gep %base, %o`, so one register indexes
//! every array ([base, offset] addressing).
//!
//! Every variable is affine in the iteration number with a positive scale
//! relative to the counter, so each comparison keeps its outcome. That needs
//! the scaled bound not to overflow: the bound and the counter's start must be
//! small constants or sign-extended 32-bit values, and the scale at most 4096.
//! A pointer test compares signed, as any real address is far below 2^63,
//! or unsigned when constant bounds show the loop runs (its pointer then
//! moves through memory, which does not wrap, wherever it is).
//! Runs late, after iv_widen, on 64-bit targets, and not with -fwrapv (the
//! counter itself must not wrap).

use crate::common::fx_hash::FxHashSet;
use crate::common::types::IrType;
use crate::ir::analysis;
use crate::ir::reexports::{BlockId, Instruction, IrBinOp, IrCmpOp, IrConst, IrFunction, Operand, Terminator, Value};
use super::copy_prop::{replace_operands_in_instruction, replace_operands_in_terminator};
use super::loop_analysis;

/// Largest scale between the counter and the variable that replaces it.
const MAX_SCALE: i64 = 4096;

pub(crate) fn lftr_function(func: &mut IrFunction) -> usize {
    if crate::common::types::target_is_32bit() || func.blocks.len() < 2 {
        return 0;
    }
    let cfg = analysis::CfgAnalysis::build(func);
    let loops = loop_analysis::find_natural_loops(cfg.num_blocks, &cfg.preds, &cfg.succs, &cfg.idom);
    let mut changed = 0;
    for lp in loop_analysis::merge_loops_by_header(loops) {
        let Some(pre) = loop_analysis::find_preheader(lp.header, &lp.body, &cfg.preds) else { continue };
        let latches: Vec<usize> = cfg.preds.row(lp.header).iter().map(|&p| p as usize)
            .filter(|p| lp.body.contains(p)).collect();
        if latches.len() != 1 {
            continue;
        }
        if replace_test(func, &lp.body, lp.header, pre, latches[0]) {
            changed += 1;
        }
    }
    changed
}

/// A header phi stepping by a constant each iteration.
struct Affine {
    phi: Value,
    init: Operand,
    next: Value,
    step: i64,
    /// GEP (pointer) or Add I64 (integer) step.
    pointer: bool,
}

fn def_of(func: &IrFunction, v: Value) -> Option<(usize, usize)> {
    func.blocks.iter().enumerate().find_map(|(b, block)| {
        block.instructions.iter().position(|i| i.dest() == Some(v)).map(|k| (b, k))
    })
}

fn affine_phis(func: &IrFunction, header: usize, pre: BlockId, latch: BlockId) -> Vec<Affine> {
    let mut out = Vec::new();
    for inst in &func.blocks[header].instructions {
        let Instruction::Phi { dest, ty, incoming } = inst else { continue };
        if incoming.len() != 2 || !matches!(ty, IrType::I64 | IrType::Ptr) {
            continue;
        }
        let Some(init) = incoming.iter().find(|(_, l)| *l == pre).map(|(op, _)| *op) else { continue };
        let Some(Operand::Value(next)) = incoming.iter().find(|(_, l)| *l == latch).map(|(op, _)| *op) else { continue };
        let Some((b, k)) = def_of(func, next) else { continue };
        let is_phi = |op: &Operand| matches!(op, Operand::Value(v) if v == dest);
        let step = match &func.blocks[b].instructions[k] {
            Instruction::BinOp { op: IrBinOp::Add, lhs, rhs, ty: IrType::I64, .. } if *ty == IrType::I64 => {
                if is_phi(lhs) { rhs.as_i64() } else if is_phi(rhs) { lhs.as_i64() } else { None }
                    .map(|s| (s, false))
            }
            Instruction::GetElementPtr { base, offset, .. } if base == dest && *ty == IrType::Ptr =>
                offset.as_i64().map(|s| (s, true)),
            _ => None,
        };
        if let Some((step, pointer)) = step.filter(|s| s.0 != 0) {
            out.push(Affine { phi: *dest, init, next, step, pointer });
        }
    }
    out
}

trait OperandExt {
    fn as_i64(&self) -> Option<i64>;
}

impl OperandExt for Operand {
    fn as_i64(&self) -> Option<i64> {
        match self {
            Operand::Const(c) => c.to_i64(),
            Operand::Value(_) => None,
        }
    }
}

/// Whether `op` is known to lie in the 32-bit range: a small constant, or a
/// sign extension of a 32-bit value.
fn bounded(func: &IrFunction, op: &Operand) -> bool {
    match op {
        Operand::Const(c) => c.to_i64().is_some_and(|v| (i32::MIN as i64..=i32::MAX as i64).contains(&v)),
        Operand::Value(v) => def_of(func, *v).is_some_and(|(b, k)| matches!(func.blocks[b].instructions[k],
            Instruction::Cast { from_ty: IrType::I32, to_ty: IrType::I64, .. })),
    }
}

fn replace_test(func: &mut IrFunction, body: &FxHashSet<usize>, header: usize, pre: usize, latch: usize) -> bool {
    let (pre_label, latch_label) = (func.blocks[pre].label, func.blocks[latch].label);
    let ivs = affine_phis(func, header, pre_label, latch_label);
    // The exit test: the header's branch on a signed compare of an integer
    // variable with a loop-invariant bound.
    let Terminator::CondBranch { cond: Operand::Value(cond), .. } = func.blocks[header].terminator else { return false };
    let Some(cmp_at) = func.blocks[header].instructions.iter().position(|i| i.dest() == Some(cond)) else { return false };
    let Instruction::Cmp { op, lhs: Operand::Value(iv), rhs: bound, .. } = func.blocks[header].instructions[cmp_at].clone()
        else { return false };
    if !matches!(op, IrCmpOp::Slt | IrCmpOp::Sle | IrCmpOp::Sgt | IrCmpOp::Sge | IrCmpOp::Eq | IrCmpOp::Ne) {
        return false;
    }
    let Some(counter) = ivs.iter().find(|a| a.phi == iv && !a.pointer) else { return false };
    let defined_in_loop = |func: &IrFunction, v: Value| def_of(func, v).is_some_and(|(b, _)| body.contains(&b));
    if matches!(bound, Operand::Value(v) if defined_in_loop(func, v))
        || !bounded(func, &bound) || !bounded(func, &counter.init) || counter.step.abs() > 64 {
        return false;
    }

    // The counter's other uses: none, or shifts by one constant (indexing).
    let mut shifts: Vec<(usize, usize)> = Vec::new();
    let mut shift_by: Option<i64> = None;
    for (b, block) in func.blocks.iter().enumerate() {
        for (k, inst) in block.instructions.iter().enumerate() {
            let mut uses = 0;
            inst.for_each_used_value(|v| uses += (v == counter.phi.0) as u32 + 2 * (v == counter.next.0) as u32);
            if uses == 0 || inst.dest() == Some(counter.next) || inst.dest() == Some(cond)
                || matches!(inst, Instruction::Phi { dest, .. } if *dest == counter.phi) {
                continue;
            }
            match inst {
                Instruction::BinOp { op: IrBinOp::Shl, lhs: Operand::Value(v), rhs, ty: IrType::I64, .. }
                    if *v == counter.phi && uses == 1 && rhs.as_i64().is_some_and(|s| (0..=12).contains(&s))
                        && shift_by.is_none_or(|s| Some(s) == rhs.as_i64()) =>
                {
                    shift_by = rhs.as_i64();
                    shifts.push((b, k));
                }
                _ => return false,
            }
        }
        let mut used = false;
        block.terminator.for_each_used_value(|v| used |= v == counter.phi.0 || v == counter.next.0);
        if used {
            return false;
        }
    }

    let mut next_id = func.next_value_id;
    let mut fresh = || { let v = Value(next_id); next_id += 1; v };
    let mut pre_insts: Vec<Instruction> = Vec::new();

    let (new_lhs, new_rhs);
    let mut new_op = op;
    let mut pointer_folds: Vec<(Value, Operand)> = Vec::new();
    let mut new_phi: Option<(Value, Operand, Value, i64)> = None;
    if let Some(k) = shift_by {
        // The shifted counter becomes the variable: off = i << k throughout.
        let m = 1i64 << k;
        let step = counter.step * m;
        let off = fresh();
        let off_next = fresh();
        let init = scale(counter.init, m, &mut pre_insts, &mut fresh);
        new_phi = Some((off, init, off_next, step));
        new_lhs = Operand::Value(off);
        new_rhs = scale(bound, m, &mut pre_insts, &mut fresh);
        // Pointers stepping alike from offset 0 become base + off.
        if counter.init.as_i64() == Some(0) {
            for p in ivs.iter().filter(|a| a.pointer && a.step == step) {
                let only_phi_uses_next = func.blocks.iter().all(|block| {
                    block.instructions.iter().all(|i| {
                        matches!(i, Instruction::Phi { dest, .. } if *dest == p.phi) || !i.used_values().contains(&p.next.0)
                    }) && !block.terminator.used_values().contains(&p.next.0)
                });
                let invariant = match p.init {
                    Operand::Value(v) => !defined_in_loop(func, v),
                    Operand::Const(_) => true,
                };
                if only_phi_uses_next && invariant {
                    pointer_folds.push((p.phi, p.init));
                }
            }
        }
    } else {
        // Compare a pointer variable instead, against where it would be when
        // the counter reached the bound.
        let Some(p) = ivs.iter().find(|a| a.pointer && a.step % counter.step == 0
            && (1..=MAX_SCALE).contains(&(a.step / counter.step))) else { return false };
        if matches!(p.init, Operand::Value(v) if defined_in_loop(func, v)) {
            return false;
        }
        let m = p.step / counter.step;
        let diff = sub(bound, counter.init, &mut pre_insts, &mut fresh);
        let bytes = scale(diff, m, &mut pre_insts, &mut fresh);
        let Operand::Value(base) = p.init else { return false };
        let end = fresh();
        pre_insts.push(Instruction::GetElementPtr { dest: end, base, offset: bytes, ty: IrType::I8 });
        new_lhs = Operand::Value(p.phi);
        new_rhs = Operand::Value(end);
        // Addresses compare as signed, which is right for any real address
        // (far below 2^63) with any bound. When the loop is known to run,
        // the pointer moves through memory that does not wrap, so unsigned
        // is right for every address (a pointer near 2^63 included).
        if let (Some(i0), Some(n)) = (counter.init.as_i64(), bound.as_i64()) {
            if op.eval_i64(i0, n) {
                new_op = match op {
                    IrCmpOp::Slt => IrCmpOp::Ult,
                    IrCmpOp::Sle => IrCmpOp::Ule,
                    IrCmpOp::Sgt => IrCmpOp::Ugt,
                    IrCmpOp::Sge => IrCmpOp::Uge,
                    other => other,
                };
            }
        }
    }

    // Rewrite: the compare, the shifts (now copies of the offset), the
    // folded pointers (now addresses off their base), and the new phi.
    let hb = &mut func.blocks[header];
    hb.instructions[cmp_at] = Instruction::Cmp { dest: cond, op: new_op, lhs: new_lhs, rhs: new_rhs, ty: IrType::I64 };
    if let Some((off, _, _, _)) = new_phi {
        for &(b, k) in &shifts {
            let dest = func.blocks[b].instructions[k].dest().unwrap();
            func.blocks[b].instructions[k] = Instruction::Copy { dest, src: Operand::Value(off) };
        }
    }
    // Each use of a folded pointer gets its own address right before it, so
    // the backend can fold it into the access ([base, offset]).
    for &(phi, base) in &pointer_folds {
        let (off, ..) = new_phi.unwrap();
        let Operand::Value(base) = base else { continue };
        func.blocks[header].instructions.retain(|i| !matches!(i, Instruction::Phi { dest, .. } if *dest == phi));
        // A phi reads it at the end of the predecessor the value comes from.
        let mut phi_edges: Vec<(usize, usize, BlockId)> = Vec::new();
        for (b, block) in func.blocks.iter().enumerate() {
            for (k, inst) in block.instructions.iter().enumerate() {
                if let Instruction::Phi { incoming, .. } = inst {
                    for (op, label) in incoming {
                        if matches!(op, Operand::Value(v) if *v == phi) {
                            phi_edges.push((b, k, *label));
                        }
                    }
                }
            }
        }
        for (b, k, label) in phi_edges {
            let Some(pred) = func.blocks.iter().position(|blk| blk.label == label) else { continue };
            let addr = fresh();
            let at = func.blocks[pred].instructions.len();
            insert_at(func, pred, at, Instruction::GetElementPtr { dest: addr, base, offset: Operand::Value(off), ty: IrType::I8 });
            let k = if pred == b && at <= k { k + 1 } else { k };
            if let Instruction::Phi { incoming, .. } = &mut func.blocks[b].instructions[k] {
                for (op, l) in incoming.iter_mut() {
                    if *l == label && matches!(op, Operand::Value(v) if *v == phi) {
                        *op = Operand::Value(addr);
                    }
                }
            }
        }
        for b in 0..func.blocks.len() {
            let mut k = 0;
            while k < func.blocks[b].instructions.len() {
                let inst = &func.blocks[b].instructions[k];
                if matches!(inst, Instruction::Phi { .. }) || !inst.used_values().contains(&phi.0) {
                    k += 1;
                    continue;
                }
                let addr = fresh();
                let mut map: Vec<Option<Operand>> = vec![None; phi.0 as usize + 1];
                map[phi.0 as usize] = Some(Operand::Value(addr));
                replace_operands_in_instruction(&mut func.blocks[b].instructions[k], &map);
                insert_at(func, b, k, Instruction::GetElementPtr { dest: addr, base, offset: Operand::Value(off), ty: IrType::I8 });
                k += 2;
            }
            if func.blocks[b].terminator.used_values().contains(&phi.0) {
                let addr = fresh();
                let mut map: Vec<Option<Operand>> = vec![None; phi.0 as usize + 1];
                map[phi.0 as usize] = Some(Operand::Value(addr));
                replace_operands_in_terminator(&mut func.blocks[b].terminator, &map);
                let at = func.blocks[b].instructions.len();
                insert_at(func, b, at, Instruction::GetElementPtr { dest: addr, base, offset: Operand::Value(off), ty: IrType::I8 });
            }
        }
    }
    if let Some((off, init, off_next, step)) = new_phi {
        let (b, k) = def_of(func, counter.next).unwrap();
        insert_at(func, b, k + 1, Instruction::BinOp {
            dest: off_next, op: IrBinOp::Add, lhs: Operand::Value(off), rhs: Operand::Const(IrConst::I64(step)), ty: IrType::I64,
        });
        insert_at(func, header, 0, Instruction::Phi {
            dest: off, ty: IrType::I64, incoming: vec![(init, pre_label), (Operand::Value(off_next), latch_label)],
        });
    }
    // The counter is now dead: its phi and step.
    let (counter_phi, counter_next) = (counter.phi, counter.next);
    for block in &mut func.blocks {
        let mut k = 0;
        while k < block.instructions.len() {
            if block.instructions[k].dest().is_some_and(|d| d == counter_phi || d == counter_next) {
                block.instructions.remove(k);
                if block.source_spans.len() > k {
                    block.source_spans.remove(k);
                }
            } else {
                k += 1;
            }
        }
    }
    let at = func.blocks[pre].instructions.len();
    for (n, inst) in pre_insts.into_iter().enumerate() {
        insert_at(func, pre, at + n, inst);
    }
    func.next_value_id = next_id;
    true
}

/// `a - b` for the preheader (folded for constants).
fn sub(a: Operand, b: Operand, pre_insts: &mut Vec<Instruction>, fresh: &mut dyn FnMut() -> Value) -> Operand {
    match (a.as_i64(), b.as_i64()) {
        (Some(x), Some(y)) => Operand::Const(IrConst::I64(x - y)),
        (_, Some(0)) => a,
        _ => {
            let d = fresh();
            pre_insts.push(Instruction::BinOp { dest: d, op: IrBinOp::Sub, lhs: a, rhs: b, ty: IrType::I64 });
            Operand::Value(d)
        }
    }
}

/// `a * m` for the preheader: a shift for a power of two, folded for constants.
fn scale(a: Operand, m: i64, pre_insts: &mut Vec<Instruction>, fresh: &mut dyn FnMut() -> Value) -> Operand {
    match a.as_i64() {
        Some(x) => Operand::Const(IrConst::I64(x * m)),
        None if m == 1 => a,
        None => {
            let d = fresh();
            let (op, rhs) = if m.count_ones() == 1 { (IrBinOp::Shl, m.trailing_zeros() as i64) } else { (IrBinOp::Mul, m) };
            pre_insts.push(Instruction::BinOp { dest: d, op, lhs: a, rhs: Operand::Const(IrConst::I64(rhs)), ty: IrType::I64 });
            Operand::Value(d)
        }
    }
}

/// Insert an instruction, keeping the block's source spans parallel.
fn insert_at(func: &mut IrFunction, b: usize, k: usize, inst: Instruction) {
    let block = &mut func.blocks[b];
    if block.source_spans.len() == block.instructions.len() && !block.source_spans.is_empty() {
        let span = block.source_spans[k.min(block.source_spans.len() - 1)];
        block.source_spans.insert(k, span);
    }
    block.instructions.insert(k, inst);
}
