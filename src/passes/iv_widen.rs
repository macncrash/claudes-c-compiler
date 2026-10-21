//! Induction variable widening.
//!
//! A signed 32-bit loop counter on a 64-bit target costs a sign extension
//! per iteration: the narrowed `i + 1` is an I32 add, kept in canonical
//! sign-extended form, and array indexing sign-extends `i` again. Since
//! signed overflow is undefined, the counter can instead be kept as a 64-bit
//! value equal to `sext(i)` throughout:
//!
//!   %i = phi I32 [%init, pre], [%next, latch]     %i = phi I64 [sext %init, pre], [%next, latch]
//!   %next = Add I32 %i, 1                     =>  %next = Add I64 %i, 1
//!   %w = Cast %i, I32 -> I64                      %w = Copy %i
//!   %c = Cmp slt I32 %i, %n                       %c = Cmp slt I64 %i, sext %n
//!
//! Compares against constants or loop-invariant values are widened (sign
//! extension preserves both signed and unsigned order of I32 values); any
//! other use reads an explicit truncation. Runs once, late (after narrowing
//! has produced the I32 adds), on 64-bit targets only.

use crate::common::types::IrType;
use crate::ir::analysis;
use crate::ir::reexports::{Instruction, IrBinOp, IrConst, IrFunction, Operand, Value};
use super::copy_prop::{replace_operands_in_instruction, replace_operands_in_terminator};
use super::loop_analysis;

pub(crate) fn widen_function(func: &mut IrFunction) -> usize {
    if crate::common::types::target_is_32bit() || func.blocks.len() < 2 {
        return 0;
    }
    let cfg = analysis::CfgAnalysis::build(func);
    let loops = loop_analysis::find_natural_loops(cfg.num_blocks, &cfg.preds, &cfg.succs, &cfg.idom);
    let mut widened = 0;
    for lp in loop_analysis::merge_loops_by_header(loops) {
        let Some(pre) = loop_analysis::find_preheader(lp.header, &lp.body, &cfg.preds) else { continue };
        let latches: Vec<usize> = cfg.preds.row(lp.header).iter().map(|&p| p as usize)
            .filter(|p| lp.body.contains(p)).collect();
        if latches.len() != 1 {
            continue;
        }
        let (pre_label, latch_label) = (func.blocks[pre].label, func.blocks[latches[0]].label);
        let phis: Vec<Value> = func.blocks[lp.header].instructions.iter()
            .filter_map(|i| match i { Instruction::Phi { dest, ty: IrType::I32, .. } => Some(*dest), _ => None })
            .collect();
        for phi in phis {
            if widen_iv(func, &lp.body, lp.header, pre, phi, pre_label, latch_label) {
                widened += 1;
            }
        }
    }
    widened
}

/// `op` sign-extended from I32 to I64: a constant, or a Cast appended to
/// `pre_insts` (for the preheader).
fn sext(op: Operand, pre_insts: &mut Vec<Instruction>, fresh: &mut dyn FnMut() -> Value) -> Operand {
    match op {
        Operand::Const(c) => Operand::Const(IrConst::I64(c.to_i64().unwrap_or(0) as i32 as i64)),
        Operand::Value(_) => {
            let w = fresh();
            pre_insts.push(Instruction::Cast { dest: w, src: op, from_ty: IrType::I32, to_ty: IrType::I64 });
            Operand::Value(w)
        }
    }
}

/// The constant of `op` as an i64, if it is an integer constant.
fn const_i64(op: &Operand) -> Option<i64> {
    match op {
        Operand::Const(c) => c.to_i64(),
        _ => None,
    }
}

#[allow(clippy::too_many_arguments)]
fn widen_iv(func: &mut IrFunction, body: &crate::common::fx_hash::FxHashSet<usize>, header: usize, pre: usize,
            phi: Value, pre_label: crate::ir::reexports::BlockId, latch_label: crate::ir::reexports::BlockId) -> bool {
    // The phi: [init from the preheader, next from the latch].
    let Some((init, next)) = func.blocks[header].instructions.iter().find_map(|i| match i {
        Instruction::Phi { dest, incoming, .. } if *dest == phi && incoming.len() == 2 => {
            let init = incoming.iter().find(|(_, l)| *l == pre_label)?.0;
            let Operand::Value(next) = incoming.iter().find(|(_, l)| *l == latch_label)?.0 else { return None };
            Some((init, next))
        }
        _ => None,
    }) else { return false };
    // next = Add/Sub I32 phi, const, in the loop.
    let mut next_at = None;
    for &b in body {
        for (k, inst) in func.blocks[b].instructions.iter().enumerate() {
            if let Instruction::BinOp { dest, op: IrBinOp::Add | IrBinOp::Sub, lhs, rhs, ty: IrType::I32 } = inst {
                let is_phi = |op: &Operand| matches!(op, Operand::Value(v) if *v == phi);
                let steps = (is_phi(lhs) && const_i64(rhs).is_some())
                    || (matches!(inst, Instruction::BinOp { op: IrBinOp::Add, .. })
                        && is_phi(rhs) && const_i64(lhs).is_some());
                if *dest == next && steps {
                    next_at = Some((b, k));
                }
            }
        }
    }
    let Some((next_block, next_idx)) = next_at else { return false };

    let is_iv = |v: u32| v == phi.0 || v == next.0;
    let defined_in_loop = |func: &IrFunction, v: u32| body.iter().any(|&b| {
        func.blocks[b].instructions.iter().any(|i| i.dest().is_some_and(|d| d.0 == v))
    });
    // No other phi may read the counter (its use point would be a
    // predecessor's end, where a truncation might not be available).
    for block in &func.blocks {
        for inst in &block.instructions {
            if let Instruction::Phi { dest, incoming, .. } = inst {
                if *dest != phi && incoming.iter().any(|(op, _)| matches!(op, Operand::Value(v) if is_iv(v.0))) {
                    return false;
                }
            }
        }
    }

    let mut next_id = func.next_value_id;
    let mut fresh = || { let v = Value(next_id); next_id += 1; v };
    let mut pre_insts: Vec<Instruction> = Vec::new();
    if let Operand::Const(c) = init {
        if c.to_i64().is_none() {
            return false;
        }
    }

    // Rewrite the uses that widen naturally; note whether others remain.
    let mut needs_trunc = [false, false]; // [phi, next]
    let mut compares: Vec<(usize, usize)> = Vec::new();
    for (b, block) in func.blocks.iter().enumerate() {
        for (k, inst) in block.instructions.iter().enumerate() {
            let mut uses_iv = false;
            inst.for_each_used_value(|v| uses_iv |= is_iv(v));
            if !uses_iv || (b == next_block && k == next_idx) || matches!(inst, Instruction::Phi { dest, .. } if *dest == phi) {
                continue;
            }
            match inst {
                Instruction::Cast { src: Operand::Value(v), from_ty: IrType::I32, to_ty: IrType::I64 | IrType::U64, .. }
                    if is_iv(v.0) => {}
                Instruction::Cmp { ty: IrType::I32, lhs, rhs, .. } => {
                    let other = if matches!(lhs, Operand::Value(v) if is_iv(v.0)) { rhs } else { lhs };
                    let ok = match other {
                        Operand::Const(c) => c.to_i64().is_some(),
                        Operand::Value(v) => !is_iv(v.0) && !defined_in_loop(func, v.0),
                    };
                    if ok {
                        compares.push((b, k));
                    } else {
                        inst.for_each_used_value(|v| { if v == phi.0 { needs_trunc[0] = true } else if v == next.0 { needs_trunc[1] = true } });
                    }
                }
                _ => inst.for_each_used_value(|v| { if v == phi.0 { needs_trunc[0] = true } else if v == next.0 { needs_trunc[1] = true } }),
            }
        }
        let mut uses_iv = false;
        block.terminator.for_each_used_value(|v| uses_iv |= is_iv(v));
        if uses_iv {
            block.terminator.for_each_used_value(|v| { if v == phi.0 { needs_trunc[0] = true } else if v == next.0 { needs_trunc[1] = true } });
        }
    }

    // Truncations for the remaining uses.
    let trunc = [needs_trunc[0].then(&mut fresh), needs_trunc[1].then(&mut fresh)];
    let mut map: Vec<Option<Operand>> = vec![None; func.next_value_id as usize];
    if let Some(t) = trunc[0] {
        map[phi.0 as usize] = Some(Operand::Value(t));
    }
    if let Some(t) = trunc[1] {
        map[next.0 as usize] = Some(Operand::Value(t));
    }
    let new_init = sext(init, &mut pre_insts, &mut fresh);

    // Widen the compares (their other operand: a constant, or sign-extended
    // in the preheader), and turn sign extensions of the counter into copies.
    for &(b, k) in &compares {
        if let Instruction::Cmp { ty, lhs, rhs, .. } = &mut func.blocks[b].instructions[k] {
            for op in [lhs, rhs] {
                if !matches!(op, Operand::Value(v) if is_iv(v.0)) {
                    *op = sext(*op, &mut pre_insts, &mut fresh);
                }
            }
            *ty = IrType::I64;
        }
    }
    for (b, block) in func.blocks.iter_mut().enumerate() {
        for (k, inst) in block.instructions.iter_mut().enumerate() {
            match inst {
                Instruction::Phi { dest, ty, incoming } if *dest == phi => {
                    *ty = IrType::I64;
                    for (op, label) in incoming.iter_mut() {
                        if *label == pre_label {
                            *op = new_init;
                        }
                    }
                }
                Instruction::BinOp { ty, lhs, rhs, .. } if b == next_block && k == next_idx => {
                    *ty = IrType::I64;
                    for op in [lhs, rhs] {
                        if let Some(c) = const_i64(op) {
                            *op = Operand::Const(IrConst::I64(c as i32 as i64));
                        }
                    }
                }
                Instruction::Cast { dest, src: Operand::Value(v), from_ty: IrType::I32, to_ty: IrType::I64 | IrType::U64 }
                    if is_iv(v.0) => {
                    *inst = Instruction::Copy { dest: *dest, src: Operand::Value(*v) };
                }
                Instruction::Phi { .. } => {}
                _ if compares.contains(&(b, k)) => {}
                _ => { replace_operands_in_instruction(inst, &map); }
            }
        }
        replace_operands_in_terminator(&mut block.terminator, &map);
    }

    // Place the truncations: the phi's after the header's phis, next's
    // right after its definition.
    if let Some(t) = trunc[1] {
        func.blocks[next_block].instructions.insert(next_idx + 1,
            Instruction::Cast { dest: t, src: Operand::Value(next), from_ty: IrType::I64, to_ty: IrType::I32 });
    }
    if let Some(t) = trunc[0] {
        let insts = &mut func.blocks[header].instructions;
        let at = insts.iter().position(|i| !matches!(i, Instruction::Phi { .. })).unwrap_or(insts.len());
        insts.insert(at, Instruction::Cast { dest: t, src: Operand::Value(phi), from_ty: IrType::I64, to_ty: IrType::I32 });
    }
    func.blocks[pre].instructions.extend(pre_insts);
    func.next_value_id = next_id;
    true
}
