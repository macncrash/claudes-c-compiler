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
//! function, most loop uses first. On AArch64 so are 32-bit operations'
//! constants that take a movz and a movk and that the instruction cannot
//! encode (see candidate32). Runs after phi elimination, where no
//! copy propagation folds the constants back in.

use crate::common::types::IrType;
use crate::ir::analysis;
use crate::ir::reexports::{Instruction, IrBinOp, IrConst, IrFunction, IrModule, Operand, Value};
use super::loop_analysis;

/// Most constants given a register per function.
const MAX_HOISTED: usize = 4;

/// `aarch64` also hoists the constants on the left of subtracts and shifts.
pub(crate) fn hoist_constants(module: &mut IrModule, aarch64: bool) {
    if crate::common::types::target_is_32bit() {
        return;
    }
    for func in module.functions.iter_mut().filter(|f| !f.is_declaration) {
        hoist_function(func, aarch64);
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

/// A constant in a 32-bit operation that AArch64 builds with a movz and a
/// movk at each use (both 16-bit halves differ from 0 and 0xffff) and the
/// instruction cannot encode: no add or subtract immediate (12 bits,
/// optionally shifted by 12), no logical bitmask. deflate's hash multiplies
/// by 66521 and adds it.
fn candidate32(op: &Operand, binop: Option<IrBinOp>) -> Option<i64> {
    let k = match op {
        Operand::Const(IrConst::I64(k)) => *k,
        Operand::Const(IrConst::I32(k)) => *k as i64,
        _ => return None,
    };
    let w = k as u32;
    let (lo, hi) = (w & 0xffff, w >> 16);
    if lo == 0 || hi == 0 || lo == 0xffff || hi == 0xffff {
        return None;
    }
    let imm12 = |x: u32| x <= 0xfff || x & !0xfff000 == 0;
    match binop {
        Some(IrBinOp::Add | IrBinOp::Sub) | None if imm12(w) || imm12(w.wrapping_neg()) => None,
        Some(IrBinOp::And | IrBinOp::Or | IrBinOp::Xor)
            if crate::backend::arm::assembler::encoder::encode_bitmask_imm(w as u64, false).is_some() => None,
        _ => Some(k),
    }
}

/// A nonzero constant on the left of a subtract or shift, which AArch64
/// builds into a register at each use (it has no reversed forms taking an
/// immediate); zero is the zero register.
fn left_candidate(op: &Operand) -> Option<i64> {
    match op {
        Operand::Const(IrConst::I64(k)) if *k != 0 => Some(*k),
        Operand::Const(IrConst::I32(k)) if *k != 0 => Some(*k as i64),
        _ => None,
    }
}

/// Instructions building a constant at a use: the weight of its loop uses.
/// (A movz or movn and a movk for each other piece.)
fn build_cost(k: i64) -> usize {
    if costly(k) {
        return 3;
    }
    let pieces = |skip: u64| (0..4).filter(|i| (k as u64 >> (16 * i)) & 0xffff != skip).count();
    pieces(0).min(pieces(0xffff)).max(1)
}

/// The hoistable constant operands of an instruction.
fn for_each_candidate(inst: &mut Instruction, left: bool, mut f: impl FnMut(&mut Operand, i64)) {
    let wide = |ty: &IrType| matches!(ty, IrType::I64 | IrType::U64);
    match inst {
        // AArch64 multiplies and divides take no immediate: zbench's word
        // choice `r % nw`, a multiply by 0x38e38e39 for each word. Only a
        // constant that takes a movk too: with one `mov` saved per use,
        // SQLite's speedtest ran 4% more instructions.
        Instruction::BinOp { op, lhs, rhs, ty, .. } if left && ty.is_integer() && matches!(ty.size(), 4 | 8)
            && matches!(op, IrBinOp::Mul | IrBinOp::SDiv | IrBinOp::UDiv | IrBinOp::SRem | IrBinOp::URem) => {
            for o in [lhs, rhs] {
                if let Some(k) = left_candidate(o).filter(|&k| build_cost(k) >= 2) {
                    f(o, k);
                }
            }
        }
        Instruction::BinOp { op, lhs, rhs, ty, .. } if left && !ty.is_float()
            && matches!(op, IrBinOp::Sub | IrBinOp::Shl | IrBinOp::LShr | IrBinOp::AShr)
            && matches!(ty.size(), 4 | 8) && left_candidate(lhs).is_some() => {
            let k = left_candidate(lhs).expect("checked by the guard");
            f(lhs, k);
            let shift = !matches!(op, IrBinOp::Sub);
            if let (false, Some(k)) = (shift || !wide(ty), candidate(rhs, false)) {
                f(rhs, k);
            }
        }
        Instruction::BinOp { op, lhs, rhs, ty, .. } if left && matches!(ty, IrType::I32 | IrType::U32) => {
            // A shift amount is always an immediate.
            if !matches!(op, IrBinOp::Shl | IrBinOp::LShr | IrBinOp::AShr) {
                for o in [lhs, rhs] {
                    if let Some(k) = candidate32(o, Some(*op)) {
                        f(o, k);
                    }
                }
            }
        }
        Instruction::Cmp { lhs, rhs, ty, .. } if left && matches!(ty, IrType::I32 | IrType::U32) => {
            for o in [lhs, rhs] {
                if let Some(k) = candidate32(o, None) {
                    f(o, k);
                }
            }
        }
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

fn hoist_function(func: &mut IrFunction, left: bool) -> usize {
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
    // Loop uses of each candidate constant, weighted by the instructions
    // building it takes, in first-seen order.
    let mut uses: Vec<(i64, usize)> = Vec::new();
    for (b, block) in func.blocks.iter_mut().enumerate() {
        if !in_loop[b] {
            continue;
        }
        for inst in &mut block.instructions {
            for_each_candidate(inst, left, |_, k| match uses.iter_mut().find(|(c, _)| *c == k) {
                Some((_, n)) => *n += build_cost(k),
                None => uses.push((k, build_cost(k))),
            });
        }
    }
    if uses.is_empty() {
        return 0;
    }
    // A cheap constant read once saves one instruction but holds a register
    // through the whole function (`MAX_MATCH - len` in longest_match lost
    // more to the allocation around it); one read in several places pays.
    uses.retain(|&(k, n)| costly(k) || n >= 2);
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
            for_each_candidate(inst, left, |o, k| {
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
        assert_eq!(hoist_function(&mut func, true), 1);
        assert!(matches!(func.blocks[0].instructions[..], [Instruction::Copy { dest: Value(3), src: Operand::Const(IrConst::I64(_)) }]));
        assert!(matches!(func.blocks[1].instructions[0], Instruction::BinOp { rhs: Operand::Value(Value(3)), .. }));
        // The use outside the loop keeps its immediate.
        assert!(matches!(func.blocks[2].instructions[0], Instruction::BinOp { rhs: Operand::Const(_), .. }));
        assert_eq!(func.next_value_id, 4);
    }

    #[test]
    fn test_constant_left_of_subtract_built_once_on_aarch64() {
        let with_subs = |n: u32| {
            let mut func = loop_func();
            for d in 0..n {
                func.blocks[1].instructions.push(Instruction::BinOp {
                    dest: Value(9 + d), op: IrBinOp::Sub, lhs: Operand::Const(IrConst::I32(16)), rhs: Operand::Value(Value(1)), ty: IrType::I32,
                });
            }
            func.next_value_id = 20;
            func
        };
        let (mut func, mut other) = (with_subs(2), with_subs(2));
        assert_eq!(hoist_function(&mut func, true), 3);
        assert!(matches!(func.blocks[1].instructions[1], Instruction::BinOp { lhs: Operand::Value(_), .. }));
        assert!(func.blocks[0].instructions.iter().any(|i| matches!(i, Instruction::Copy { src: Operand::Const(IrConst::I64(16)), .. })));
        // Elsewhere only the costly constant moves.
        assert_eq!(hoist_function(&mut other, false), 1);
        assert!(matches!(other.blocks[1].instructions[1], Instruction::BinOp { lhs: Operand::Const(_), .. }));
        // Read once, it keeps its `mov`.
        let mut once = with_subs(1);
        assert_eq!(hoist_function(&mut once, true), 1);
        assert!(matches!(once.blocks[1].instructions[1], Instruction::BinOp { lhs: Operand::Const(_), .. }));
    }

    #[test]
    fn test_32bit_constant_built_once_when_not_an_immediate() {
        let mut func = loop_func();
        let op = |dest, op, k| Instruction::BinOp {
            dest: Value(dest), op, lhs: Operand::Value(Value(1)), rhs: Operand::Const(IrConst::I64(k)), ty: IrType::U32,
        };
        func.blocks[1].instructions = vec![op(1, IrBinOp::Mul, 66521), op(4, IrBinOp::Add, 66521), op(5, IrBinOp::Add, 0x123000)];
        func.next_value_id = 6;
        assert_eq!(hoist_function(&mut func, true), 2);
        assert!(func.blocks[0].instructions.iter().any(|i| matches!(i, Instruction::Copy { src: Operand::Const(IrConst::I64(66521)), .. })));
        // An add immediate (shifted by 12) stays one.
        assert!(matches!(func.blocks[1].instructions[2], Instruction::BinOp { rhs: Operand::Const(_), .. }));
        assert_eq!(candidate32(&Operand::Const(IrConst::I64(0x0f0f0f0f)), Some(IrBinOp::And)), None);
        assert_eq!(candidate32(&Operand::Const(IrConst::I64(0x10000)), Some(IrBinOp::Mul)), None);
    }

    #[test]
    fn test_bitmask_immediate_left_in_bitwise_ops() {
        let k = Operand::Const(IrConst::I64(0x5555_5555_5555_5555));
        assert_eq!(candidate(&k, true), None);
        assert_eq!(candidate(&k, false), Some(0x5555_5555_5555_5555));
    }
}
