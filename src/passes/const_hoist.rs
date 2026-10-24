//! Build large 64-bit constants used in loops once, at function entry.
//!
//!   loop:  %z = mul %x, 0xbf58476d1ce4e5b9      entry: %k = copy 0xbf58476d1ce4e5b9
//!          ...                             =>   loop:  %z = mul %x, %k
//!
//! A backend materializes a constant operand at each use: on AArch64 a
//! 64-bit value with three or four distinct 16-bit pieces is a movz and two
//! or three movks, on every iteration (hash and random number mixing steps
//! use several). Defined once at entry, the constant takes a register for
//! the whole function instead.
//!
//! Only constants that need three or more instructions to build are moved
//! (not ones whose pieces are mostly 0 or 0xffff, and not bitwise-operation
//! masks that AArch64 encodes in the instruction), at most MAX_HOISTED per
//! function, most loop uses first. Runs after phi elimination, where no
//! copy propagation folds the constants back in.

use crate::common::types::IrType;
use crate::ir::analysis;
use crate::ir::reexports::{Instruction, IrBinOp, IrConst, IrFunction, IrModule, Operand, Value};
use super::loop_analysis;

/// Most constants given a register per function.
const MAX_HOISTED: usize = 4;

pub(crate) fn hoist_constants(module: &mut IrModule) {
    if crate::common::types::target_is_32bit() {
        return;
    }
    for func in module.functions.iter_mut().filter(|f| !f.is_declaration) {
        hoist_function(func);
    }
}

/// Whether a constant takes three or more instructions to build: three or
/// more of its 16-bit pieces differ from both 0 and 0xffff.
fn costly(k: i64) -> bool {
    let pieces = (0..4).map(|i| (k as u64 >> (16 * i)) & 0xffff);
    pieces.filter(|&p| p != 0 && p != 0xffff).count() >= 3
}

/// The constant of an operand worth hoisting, given the operation reading it.
fn candidate(op: &Operand, bitwise: bool) -> Option<i64> {
    let Operand::Const(IrConst::I64(k)) = op else { return None };
    if !costly(*k) {
        return None;
    }
    // AArch64 and/orr/eor take repeating bit patterns as immediates.
    if bitwise && crate::backend::arm::assembler::encoder::encode_bitmask_imm(*k as u64, true).is_some() {
        return None;
    }
    Some(*k)
}

/// The hoistable constant operands of an instruction.
fn for_each_candidate(inst: &mut Instruction, mut f: impl FnMut(&mut Operand, i64)) {
    let wide = |ty: &IrType| matches!(ty, IrType::I64 | IrType::U64);
    match inst {
        Instruction::BinOp { op, lhs, rhs, ty, .. } if wide(ty) => {
            let bitwise = matches!(op, IrBinOp::And | IrBinOp::Or | IrBinOp::Xor);
            // A shift amount is always an immediate.
            let shift = matches!(op, IrBinOp::Shl | IrBinOp::LShr | IrBinOp::AShr);
            if let Some(k) = candidate(lhs, bitwise) {
                f(lhs, k);
            }
            if let (false, Some(k)) = (shift, candidate(rhs, bitwise)) {
                f(rhs, k);
            }
        }
        Instruction::Cmp { lhs, rhs, ty, .. } if wide(ty) => {
            for o in [lhs, rhs] {
                if let Some(k) = candidate(o, false) {
                    f(o, k);
                }
            }
        }
        Instruction::Store { val, ty, .. } if wide(ty) => {
            if let Some(k) = candidate(val, false) {
                f(val, k);
            }
        }
        Instruction::Copy { src, .. } => {
            if let Some(k) = candidate(src, false) {
                f(src, k);
            }
        }
        _ => {}
    }
}

fn hoist_function(func: &mut IrFunction) -> usize {
    if func.blocks.len() < 2 {
        return 0;
    }
    let cfg = analysis::CfgAnalysis::build(func);
    let loops = loop_analysis::find_natural_loops(cfg.num_blocks, &cfg.preds, &cfg.succs, &cfg.idom);
    let mut in_loop = vec![false; func.blocks.len()];
    for lp in &loops {
        for &b in &lp.body {
            in_loop[b] = true;
        }
    }
    // The entry block must run once, before every loop.
    if in_loop[0] {
        return 0;
    }
    // Loop uses of each candidate constant, in first-seen order.
    let mut uses: Vec<(i64, usize)> = Vec::new();
    for (b, block) in func.blocks.iter_mut().enumerate() {
        if !in_loop[b] {
            continue;
        }
        for inst in &mut block.instructions {
            for_each_candidate(inst, |_, k| match uses.iter_mut().find(|(c, _)| *c == k) {
                Some((_, n)) => *n += 1,
                None => uses.push((k, 1)),
            });
        }
    }
    if uses.is_empty() {
        return 0;
    }
    uses.sort_by_key(|&(_, n)| std::cmp::Reverse(n));
    uses.truncate(MAX_HOISTED);
    let mut next_id = func.next_value_id.max(func.max_value_id() + 1);
    let regs: Vec<(i64, Value)> = uses.iter().map(|&(k, _)| {
        next_id += 1;
        (k, Value(next_id - 1))
    }).collect();
    func.next_value_id = next_id;
    let mut changes = 0;
    for (b, block) in func.blocks.iter_mut().enumerate() {
        if !in_loop[b] {
            continue;
        }
        for inst in &mut block.instructions {
            for_each_candidate(inst, |o, k| {
                if let Some(&(_, v)) = regs.iter().find(|(c, _)| *c == k) {
                    *o = Operand::Value(v);
                    changes += 1;
                }
            });
        }
    }
    let entry = &mut func.blocks[0];
    let spans = !entry.source_spans.is_empty();
    let span = entry.source_spans.last().copied().unwrap_or(crate::common::source::Span::new(0, 0, 0));
    for &(k, v) in &regs {
        entry.instructions.push(Instruction::Copy { dest: v, src: Operand::Const(IrConst::I64(k)) });
        if spans {
            entry.source_spans.push(span);
        }
    }
    changes
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_costly_constants() {
        assert!(costly(0x9e3779b97f4a7c15u64 as i64));
        assert!(costly(0x0000_1234_5678_9abc));
        assert!(!costly(0x0000_0000_5678_9abc));
        assert!(!costly(-2)); // movn
        assert!(!costly(0xffff_1234_ffff_5678u64 as i64));
        assert!(!costly(0x5555_5555_0000_0000));
    }

    /// entry -> loop (mul %0 by K, branch back or exit) -> exit (mul by K).
    fn loop_func() -> IrFunction {
        use crate::ir::reexports::{BasicBlock, BlockId, Terminator};
        const K: i64 = 0xbf58476d1ce4e5b9u64 as i64;
        let mul = |dest| Instruction::BinOp {
            dest: Value(dest), op: IrBinOp::Mul, lhs: Operand::Value(Value(0)),
            rhs: Operand::Const(IrConst::I64(K)), ty: IrType::I64,
        };
        let mut func = IrFunction::new("test".to_string(), IrType::I64, vec![], false);
        func.blocks = vec![
            BasicBlock { label: BlockId(0), instructions: vec![], terminator: Terminator::Branch(BlockId(1)), source_spans: Vec::new() },
            BasicBlock {
                label: BlockId(1),
                instructions: vec![mul(1)],
                terminator: Terminator::CondBranch { cond: Operand::Value(Value(1)), true_label: BlockId(1), false_label: BlockId(2) },
                source_spans: Vec::new(),
            },
            BasicBlock { label: BlockId(2), instructions: vec![mul(2)], terminator: Terminator::Return(Some(Operand::Value(Value(2)))), source_spans: Vec::new() },
        ];
        func.next_value_id = 3;
        func
    }

    #[test]
    fn test_loop_constant_built_at_entry() {
        let mut func = loop_func();
        assert_eq!(hoist_function(&mut func), 1);
        assert!(matches!(func.blocks[0].instructions[..], [Instruction::Copy { dest: Value(3), src: Operand::Const(IrConst::I64(_)) }]));
        assert!(matches!(func.blocks[1].instructions[0], Instruction::BinOp { rhs: Operand::Value(Value(3)), .. }));
        // The use outside the loop keeps its immediate.
        assert!(matches!(func.blocks[2].instructions[0], Instruction::BinOp { rhs: Operand::Const(_), .. }));
        assert_eq!(func.next_value_id, 4);
    }

    #[test]
    fn test_bitmask_immediate_left_in_bitwise_ops() {
        let k = Operand::Const(IrConst::I64(0x5555_5555_5555_5555));
        assert_eq!(candidate(&k, true), None);
        assert_eq!(candidate(&k, false), Some(0x5555_5555_5555_5555));
    }
}
