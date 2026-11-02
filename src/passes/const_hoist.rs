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

/// Most constants of selects of two constants given one besides.
const MAX_SELECTED: usize = 2;

/// Most instructions in a function whose multiply-high magics are hoisted.
const MUL_HIGH_MAX_SIZE: usize = 500;

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

/// The hoistable constant operands of an instruction. `mul_high`: it is the
/// multiply of a multiply-high (div_by_const::mul_high_chains).
fn for_each_candidate(inst: &mut Instruction, left: bool, mul_high: bool, mut f: impl FnMut(&mut Operand, i64)) {
    let wide = |ty: &IrType| matches!(ty, IrType::I64 | IrType::U64);
    match inst {
        // The magic multiplier of a division by a 64-bit constant, which
        // AArch64 reads from a register into its umulh or smulh: its 64
        // bits (sqlite3Int64ToText's v/100 and v%100, four instructions on
        // every digit pair).
        Instruction::BinOp { lhs, rhs, .. } if left && mul_high => {
            for o in [lhs, rhs] {
                let k = match o {
                    Operand::Const(IrConst::I128(c)) => *c as i64,
                    Operand::Const(IrConst::I64(c)) => *c,
                    _ => continue,
                };
                if build_cost(k) >= 2 {
                    f(o, k);
                }
            }
        }
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
        // A select of two constants (a child pointer's offset, `key < n->key
        // ? 8 : 16`) builds both on every run: a select takes no immediate
        // but the zero register (and csinc/csinv for 1 and -1). Not one
        // constant against a value, which costs a register for one mov.
        Instruction::Select { true_val, false_val, ty, .. } if left && ty.is_integer() => {
            let k = |o: &Operand| match o {
                Operand::Const(IrConst::I64(k)) => Some(*k),
                Operand::Const(IrConst::I32(k)) => Some(*k as i64),
                _ => None,
            };
            if let (Some(a), Some(b)) = (k(true_val), k(false_val)) {
                if !matches!(a, -1..=1) && !matches!(b, -1..=1) {
                    f(true_val, a);
                    f(false_val, b);
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
    // A multiply-high's magic only in a function small enough that it keeps
    // a register: built at entry and spilled, it cost sqlite3_str_vappendf
    // (1721 instructions, its divisions in loops it seldom runs) eight more
    // instructions on every call.
    let size: usize = func.blocks.iter().map(|b| b.instructions.len()).sum();
    let mul_highs: Vec<(usize, usize)> = if left && size <= MUL_HIGH_MAX_SIZE {
        super::div_by_const::mul_high_chains(func).iter().map(|c| (c.block, c.mul)).collect()
    } else {
        Vec::new()
    };
    // Loop uses of each candidate constant, weighted by the instructions
    // building it takes, in first-seen order.
    let mut uses: Vec<(i64, usize)> = Vec::new();
    // The constants of selects of two constants, which get MAX_SELECTED
    // registers of their own (a pair's movs are the saving, however few
    // other constants' builds weigh).
    let mut selected: Vec<i64> = Vec::new();
    // A latch's compare against a constant the compare cannot encode (a
    // loop bound like scalar-bench's HT_KEYS, 40000): one mov an iteration.
    let mut latch = vec![false; func.blocks.len()];
    for lp in &loops {
        for &b in &lp.body {
            if cfg.succs.row(b).iter().any(|&s| s as usize == lp.header) {
                latch[b] = true;
            }
        }
    }
    let encodable = |k: i64| {
        let a = k.unsigned_abs();
        a <= 0xfff || (a & 0xfff == 0 && a <= 0xfff000)
    };
    let mut bounds: Vec<i64> = Vec::new();
    for (_, block) in func.blocks.iter().enumerate().filter(|(b, _)| left && latch[*b]) {
        for inst in &block.instructions {
            if let Instruction::Cmp { lhs, rhs, ty, .. } = inst {
                if !ty.is_integer() || !matches!(ty.size(), 4 | 8) {
                    continue;
                }
                for o in [lhs, rhs] {
                    let k = match o {
                        Operand::Const(IrConst::I64(k)) => *k,
                        Operand::Const(IrConst::I32(k)) => *k as i64,
                        _ => continue,
                    };
                    if !encodable(k) && !bounds.contains(&k) {
                        bounds.push(k);
                    }
                }
            }
        }
    }
    for (b, block) in func.blocks.iter_mut().enumerate() {
        if !in_loop[b] {
            continue;
        }
        for (i, inst) in block.instructions.iter_mut().enumerate() {
            let select = matches!(inst, Instruction::Select { .. });
            for_each_candidate(inst, left, mul_highs.contains(&(b, i)), |_, k| {
                if select {
                    if !selected.contains(&k) {
                        selected.push(k);
                    }
                    return;
                }
                match uses.iter_mut().find(|(c, _)| *c == k) {
                    Some((_, n)) => *n += build_cost(k),
                    None => uses.push((k, build_cost(k))),
                }
            });
        }
    }
    // A cheap constant read once saves one instruction but holds a register
    // through the whole function (`MAX_MATCH - len` in longest_match lost
    // more to the allocation around it); one read in several places pays.
    uses.retain(|&(k, n)| costly(k) || n >= 2);
    uses.sort_by_key(|&(_, n)| std::cmp::Reverse(n));
    uses.truncate(MAX_HOISTED);
    // Only in a function small enough to keep them in registers, as for
    // multiply-highs (jsonTranslateBlobToText's selects, among 1800
    // instructions, cost it 13M more than they saved).
    if selected.len() <= MAX_SELECTED && size <= MUL_HIGH_MAX_SIZE {
        for k in selected.iter().copied().chain(bounds.iter().copied().take(MAX_SELECTED)) {
            if !uses.iter().any(|&(c, _)| c == k) {
                uses.push((k, 0));
            }
        }
    }
    if uses.is_empty() {
        return 0;
    }
    let mut next_id = func.next_value_id.max(func.max_value_id() + 1);
    let regs: Vec<(i64, Value)> = uses.iter().map(|&(k, _)| {
        next_id += 1;
        (k, Value(next_id - 1))
    }).collect();
    func.next_value_id = next_id;
    // The loop uses read the registers, and so do the entry block's, which
    // the copies then precede: ht_delete hashed its key with the constant
    // built in place at entry and again for its probe loop.
    let mut changes = 0;
    let mut first_entry_use: Option<usize> = None;
    for (b, block) in func.blocks.iter_mut().enumerate() {
        if !in_loop[b] && b != 0 {
            continue;
        }
        for (i, inst) in block.instructions.iter_mut().enumerate() {
            for_each_candidate(inst, left, mul_highs.contains(&(b, i)), |o, k| {
                if let Some(&(_, v)) = regs.iter().find(|(c, _)| *c == k) {
                    *o = Operand::Value(v);
                    changes += 1;
                    if b == 0 && first_entry_use.is_none() {
                        first_entry_use = Some(i);
                    }
                }
            });
            if let (true, Instruction::Cmp { lhs, rhs, .. }) = (latch[b], inst) {
                for o in [lhs, rhs] {
                    let k = match o {
                        Operand::Const(IrConst::I64(k)) => *k,
                        Operand::Const(IrConst::I32(k)) => *k as i64,
                        _ => continue,
                    };
                    if let Some(&(_, v)) = regs.iter().find(|(c, _)| *c == k).filter(|_| bounds.contains(&k)) {
                        *o = Operand::Value(v);
                        changes += 1;
                    }
                }
            }
        }
    }
    let entry = &mut func.blocks[0];
    let at = first_entry_use.unwrap_or(entry.instructions.len());
    let spans = entry.source_spans.len() == entry.instructions.len() && !entry.source_spans.is_empty();
    let span = entry.source_spans.last().copied().unwrap_or(crate::common::source::Span::new(0, 0, 0));
    for (n, &(k, v)) in regs.iter().enumerate() {
        entry.instructions.insert(at + n, Instruction::Copy { dest: v, src: Operand::Const(IrConst::I64(k)) });
        if spans {
            entry.source_spans.insert(at + n, span);
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

    /// A select of two constants in a loop (a child pointer's offset) gets
    /// both in registers, besides the costly constant; a select of a value
    /// and one constant does not.
    #[test]
    fn test_select_of_two_constants_built_once() {
        let select = |dest: u32, t: Operand| Instruction::Select {
            dest: Value(dest), cond: Operand::Value(Value(1)), true_val: t,
            false_val: Operand::Const(IrConst::I64(16)), ty: IrType::I64,
        };
        let mut func = loop_func();
        func.blocks[1].instructions.push(select(3, Operand::Const(IrConst::I64(8))));
        func.next_value_id = 4;
        assert_eq!(hoist_function(&mut func, true), 3);
        assert!(matches!(func.blocks[1].instructions[1], Instruction::Select {
            true_val: Operand::Value(_), false_val: Operand::Value(_), .. }));
        let mut func = loop_func();
        func.blocks[1].instructions.push(select(3, Operand::Value(Value(0))));
        func.next_value_id = 4;
        assert_eq!(hoist_function(&mut func, true), 1);
        assert!(matches!(func.blocks[1].instructions[1], Instruction::Select { false_val: Operand::Const(_), .. }));
    }

    /// The entry block's own use of a hoisted constant reads the register
    /// (the copy goes before it), and a loop latch's compare against a bound
    /// no compare encodes (40000) gets one too.
    #[test]
    fn test_entry_use_and_latch_bound_read_registers() {
        const K: i64 = 0xbf58476d1ce4e5b9u64 as i64;
        let mut func = loop_func();
        func.blocks[0].instructions.push(Instruction::BinOp {
            dest: Value(5), op: IrBinOp::Mul, lhs: Operand::Value(Value(0)), rhs: Operand::Const(IrConst::I64(K)), ty: IrType::I64,
        });
        func.blocks[1].instructions.push(Instruction::Cmp {
            dest: Value(6), op: crate::ir::reexports::IrCmpOp::Ult, lhs: Operand::Value(Value(1)),
            rhs: Operand::Const(IrConst::I32(40000)), ty: IrType::I32,
        });
        func.next_value_id = 7;
        assert_eq!(hoist_function(&mut func, true), 3);
        let entry = &func.blocks[0].instructions;
        assert!(matches!(entry[0], Instruction::Copy { src: Operand::Const(IrConst::I64(K)), .. }), "{:?}", entry);
        assert!(matches!(entry.last(), Some(Instruction::BinOp { rhs: Operand::Value(_), .. })), "{:?}", entry);
        assert!(matches!(func.blocks[1].instructions[1], Instruction::Cmp { rhs: Operand::Value(_), .. }));
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
