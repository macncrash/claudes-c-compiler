//! Drop extensions of 8-, 16- and 32-bit values that their loads already
//! made, and sign extensions no use reads (AArch64).
//!
//!   %b = load U8 p                         %b = load U8 p
//!   ...                               =>   ...
//!   %e = cast U8->I64 %b                   %e = copy %b
//!
//! AArch64 loads narrow integers with `ldrb`/`ldrh` (zero-extending) and
//! `ldrsb`/`ldrsh` (sign-extending to 64 bits), so the register already
//! holds the value extended its own way. The backend sees that when the
//! extension follows its load, not when the value went through a phi:
//! longest_match's `scan_end` bytes, loaded before the loop and again when a
//! longer match is found, were masked with `and #0xff` at each candidate.
//!
//! A value is taken as extended when it is an 8-, 16- or 32-bit load
//! (`ldrsw` for int), a U32 operation (a 32-bit instruction clears the high
//! half), a narrowing cast (deflate's `(Pos)strstart`, then widened as an
//! index), a copy of one, or a phi all of whose incoming values are (or
//! constants in its type's range). Runs late, after the passes that may
//! replace a load with another value of its type (GVN forwards stored
//! values, whose high bits an 8- or 16-bit operation may leave unspecified).
//! compress_block's `bi_valid`, loaded as int and widened for
//! `bi_valid += len - 16`, and `dist - 1` widened as a table index, were
//! each moved to a register of their own.
//!
//! Then an int add, subtract, multiply, logical operation or left shift
//! whose uses, through copies and phis, read only its low bits becomes U32,
//! which the backend computes without the `sxtw` it keeps an int's sign
//! extension with.

use crate::common::types::IrType;
use crate::ir::reexports::{Instruction, IrBinOp, IrFunction, Operand};

pub(crate) fn elide_extensions(func: &mut IrFunction) -> usize {
    let max_id = func.max_value_id() as usize;
    let narrow = |ty: &IrType| ty.is_integer() && matches!(ty.size(), 1 | 2 | 4);
    // Values whose register holds them extended by their type.
    let mut ext: Vec<Option<IrType>> = vec![None; max_id + 1];
    for inst in func.blocks.iter().flat_map(|b| &b.instructions) {
        match inst {
            Instruction::Load { dest, ty, .. } if narrow(ty) => ext[dest.0 as usize] = Some(*ty),
            // A 32-bit operation (`sub w`) clears the high half.
            Instruction::BinOp { dest, ty: IrType::U32, .. } => ext[dest.0 as usize] = Some(IrType::U32),
            // A 64-bit value shifted right by 32 or more, or masked to 32
            // bits or fewer, is zero above them (zbench's xorshift
            // `(unsigned)(rng >> 32)`, widened again for `% nw`).
            Instruction::BinOp { dest, op, rhs: Operand::Const(k), ty: IrType::I64 | IrType::U64, .. }
                if matches!((op, k.to_i64()), (IrBinOp::LShr, Some(32..=63)) | (IrBinOp::And, Some(0..=0xffff_ffff))) =>
            {
                let k = k.to_i64().unwrap_or(0);
                ext[dest.0 as usize] = Some(match op {
                    IrBinOp::And if k <= 0xff => IrType::U8,
                    IrBinOp::And if k <= 0xffff => IrType::U16,
                    IrBinOp::LShr if k >= 56 => IrType::U8,
                    IrBinOp::LShr if k >= 48 => IrType::U16,
                    _ => IrType::U32,
                });
            }
            // A narrowing cast extends its result its own way (`and #0xffff`,
            // `sxth`, `mov w0, w0`, `sxtw`).
            Instruction::Cast { dest, from_ty, to_ty, .. }
                if narrow(to_ty) && from_ty.is_integer() && from_ty.size() > to_ty.size() =>
            {
                ext[dest.0 as usize] = Some(*to_ty)
            }
            _ => {}
        }
    }
    let fits = |c: &crate::ir::reexports::IrConst, ty: IrType| {
        let Some(k) = c.to_i64() else { return false };
        let bits = ty.size() * 8;
        if ty.is_unsigned() { k >= 0 && k < 1 << bits } else { k >= -(1 << (bits - 1)) && k < 1 << (bits - 1) }
    };
    // Whether a register holding a value extended as `e` holds it extended
    // as `from` too: the same, or zero-extended from fewer bits than an
    // unsigned `from` has, or than a signed one's sign bit.
    let covers = |e: IrType, from: IrType| {
        e == from || (e.is_unsigned() && (e.size() < from.size() || (from.is_unsigned() && e.size() <= from.size())))
    };
    // Copies and phis, to a fixpoint (optimistically: a phi is assumed
    // extended until an incoming value shows otherwise).
    let mut changed = true;
    let mut assumed: Vec<Option<IrType>> = ext.clone();
    for inst in func.blocks.iter().flat_map(|b| &b.instructions) {
        if let Instruction::Phi { dest, ty, .. } = inst {
            if narrow(ty) {
                assumed[dest.0 as usize] = Some(*ty);
            }
        }
    }
    // A wider integer phi whose incoming values are all extended one narrow
    // way holds that extension too (words_kernel's `i < n ? text[i] : ' '`,
    // a char load or a space, merged as a long): pessimistically, only once
    // every incoming value is known.
    let mut rounds = 0;
    while changed && rounds < 64 {
        changed = false;
        rounds += 1;
        for inst in func.blocks.iter().flat_map(|b| &b.instructions) {
            let (dest, ty, ok) = match inst {
                Instruction::Phi { dest, ty, incoming } if !narrow(ty) && ty.is_integer() => {
                    let t = incoming.iter().find_map(|(op, _)| match op {
                        Operand::Value(v) => assumed.get(v.0 as usize).copied().flatten(),
                        Operand::Const(_) => None,
                    });
                    match t {
                        Some(t) => (dest, t, incoming.iter().all(|(op, _)| match op {
                            Operand::Value(v) => assumed.get(v.0 as usize) == Some(&Some(t)),
                            Operand::Const(c) => fits(c, t),
                        })),
                        None => (dest, IrType::Void, false),
                    }
                }
                Instruction::Phi { dest, ty, incoming } => (dest, *ty, narrow(ty) && incoming.iter().all(|(op, _)| match op {
                    Operand::Value(v) => assumed.get(v.0 as usize) == Some(&Some(*ty)),
                    Operand::Const(c) => fits(c, *ty),
                })),
                Instruction::Copy { dest, src: Operand::Value(v) } => match assumed.get(v.0 as usize).copied().flatten() {
                    Some(t) => (dest, t, true),
                    None => (dest, IrType::Void, false),
                },
                // A widening the loop below makes a copy (the char load
                // widened before the merge).
                Instruction::Cast { dest, src: Operand::Value(v), from_ty, to_ty }
                    if from_ty.is_integer() && to_ty.is_integer() && to_ty.size() > from_ty.size() =>
                    match assumed.get(v.0 as usize).copied().flatten().filter(|&e| covers(e, *from_ty)) {
                        Some(t) => (dest, t, true),
                        None => (dest, IrType::Void, false),
                    },
                _ => continue,
            };
            let now = if ok { Some(ty) } else { None };
            let wide_phi = matches!(inst, Instruction::Phi { ty, .. } if !narrow(ty));
            if assumed[dest.0 as usize] != now && (now.is_none() || wide_phi || matches!(inst, Instruction::Copy { .. } | Instruction::Cast { .. })) {
                assumed[dest.0 as usize] = now;
                changed = true;
            }
        }
    }
    ext = assumed;
    // (An elided extension's result is held as its source is, for a mask
    // of it further on.)
    let mut elided = 0;
    for inst in func.blocks.iter_mut().flat_map(|b| &mut b.instructions) {
        match *inst {
            Instruction::Cast { dest, src: Operand::Value(v), from_ty, to_ty }
                if ext.get(v.0 as usize).copied().flatten().is_some_and(|e| covers(e, from_ty))
                    && from_ty.is_integer() && to_ty.is_integer() && to_ty.size() > from_ty.size() =>
            {
                *inst = Instruction::Copy { dest, src: Operand::Value(v) };
                ext[dest.0 as usize] = ext[v.0 as usize];
                elided += 1;
            }
            // A narrowing to the type a value is already extended as (the
            // char merged as a long, cast back to char: `sxtb` of what
            // `ldrsb` loaded).
            Instruction::Cast { dest, src: Operand::Value(v), from_ty, to_ty }
                if narrow(&to_ty) && from_ty.is_integer() && from_ty.size() > to_ty.size()
                    && ext.get(v.0 as usize).copied().flatten().is_some_and(|e| covers(e, to_ty)) =>
            {
                *inst = Instruction::Copy { dest, src: Operand::Value(v) };
                ext[dest.0 as usize] = ext[v.0 as usize];
                elided += 1;
            }
            // A mask of the bits a zero extension already kept (division by
            // a constant masks its 32-bit dividend).
            Instruction::BinOp { dest, op: IrBinOp::And, lhs: Operand::Value(v), rhs: Operand::Const(k), ty: IrType::I64 | IrType::U64 }
                if matches!(k.to_i64(), Some(0xff | 0xffff | 0xffff_ffff))
                    && ext.get(v.0 as usize).copied().flatten().is_some_and(|e| {
                        e.is_unsigned() && (e.size() as i64) * 8 <= 64 - k.to_i64().unwrap_or(0).leading_zeros() as i64
                    }) =>
            {
                *inst = Instruction::Copy { dest, src: Operand::Value(v) };
                ext[dest.0 as usize] = ext[v.0 as usize];
                elided += 1;
            }
            _ => {}
        }
    }
    elided + unsigned_low_ops(func) + unread_truncations(func)
}

/// Turn a narrowing integer cast into a copy when every use reads only the
/// bits it keeps: deflate_fast's `(ush)(strstart - match_start)`, stored as
/// 16 bits and decremented in 16 bits for d_code's widening (which masks
/// again), was masked with `and #0xffff` for neither.
fn unread_truncations(func: &mut IrFunction) -> usize {
    let uses = use_sites(func);
    let mut dropped = Vec::new();
    for (b, block) in func.blocks.iter().enumerate() {
        for (i, inst) in block.instructions.iter().enumerate() {
            let Instruction::Cast { dest, from_ty, to_ty, .. } = inst else { continue };
            if from_ty.is_integer() && to_ty.is_integer() && to_ty.size() < from_ty.size() && from_ty.size() <= 8
                && low_bits_only(func, &uses, dest.0, to_ty.size() * 8, &mut Vec::new())
            {
                dropped.push((b, i));
            }
        }
    }
    for &(b, i) in &dropped {
        if let Instruction::Cast { dest, src, .. } = func.blocks[b].instructions[i] {
            func.blocks[b].instructions[i] = Instruction::Copy { dest, src };
        }
    }
    dropped.len()
}

/// Each value's uses: (block, instruction), or None for a terminator.
fn use_sites(func: &IrFunction) -> Vec<Vec<Option<(usize, usize)>>> {
    let mut uses: Vec<Vec<Option<(usize, usize)>>> = vec![Vec::new(); func.max_value_id() as usize + 1];
    for (b, block) in func.blocks.iter().enumerate() {
        for (i, inst) in block.instructions.iter().enumerate() {
            inst.for_each_used_value(|v| if let Some(u) = uses.get_mut(v as usize) { u.push(Some((b, i))) });
        }
        block.terminator.for_each_used_value(|v| if let Some(u) = uses.get_mut(v as usize) { u.push(None) });
    }
    uses
}

/// Retype signed 32-bit ALU operations whose uses read only their low bits
/// to U32: the backend keeps an I32 result sign-extended (a `sxtw` after
/// each), which no such use reads. compress_block's
/// `s->bi_buf |= (ush)value << s->bi_valid` shifted and or-ed in int, for a
/// 16-bit store and its two bytes.
fn unsigned_low_ops(func: &mut IrFunction) -> usize {
    let uses = use_sites(func);
    let mut retype = Vec::new();
    for (b, block) in func.blocks.iter().enumerate() {
        for (i, inst) in block.instructions.iter().enumerate() {
            let Instruction::BinOp { dest, op, ty: IrType::I32, .. } = inst else { continue };
            // Below bit 31, so that no use is a 32-bit signed value.
            if matches!(op, IrBinOp::Add | IrBinOp::Sub | IrBinOp::Mul | IrBinOp::And | IrBinOp::Or | IrBinOp::Xor | IrBinOp::Shl)
                && low_bits_only(func, &uses, dest.0, 31, &mut Vec::new())
            {
                retype.push((b, i));
            }
        }
    }
    for &(b, i) in &retype {
        if let Instruction::BinOp { ty, .. } = &mut func.blocks[b].instructions[i] {
            *ty = IrType::U32;
        }
    }
    retype.len()
}

/// Whether every use of v reads only its low `bits`, as
/// `narrow::reads_low_bits`, also through copies and phis (bi_buf carried
/// from one send to the next). Values already on the walk are taken as
/// reading low bits: a cycle adds no other use.
fn low_bits_only(func: &IrFunction, uses: &[Vec<Option<(usize, usize)>>], v: u32, bits: usize, seen: &mut Vec<(u32, usize)>) -> bool {
    if seen.contains(&(v, bits)) {
        return true;
    }
    if seen.len() >= 16 {
        return false;
    }
    seen.push((v, bits));
    let fits = |ty: &IrType| ty.is_integer() && ty.size() * 8 <= bits;
    uses[v as usize].iter().all(|u| {
        let Some((b, i)) = *u else { return false };
        match &func.blocks[b].instructions[i] {
            // A narrowing, or a widening that extends from at most `bits`
            // (`and #0xffff` of a U16).
            Instruction::Cast { from_ty, to_ty, .. } => fits(to_ty) || (fits(from_ty) && to_ty.is_integer() && to_ty.size() > from_ty.size()),
            Instruction::Store { val: Operand::Value(x), ptr, ty, .. } => x.0 == v && ptr.0 != v && fits(ty),
            Instruction::Copy { dest, .. } | Instruction::Phi { dest, .. } => low_bits_only(func, uses, dest.0, bits, seen),
            // The low bits of these depend only on the operands' low bits,
            // whatever the width they are computed in.
            Instruction::BinOp { dest, op, lhs, rhs, ty } if ty.is_integer() && ty.size() <= 8 => {
                let low = match (op, lhs, rhs) {
                    (IrBinOp::LShr | IrBinOp::AShr, Operand::Value(l), Operand::Const(k)) if l.0 == v => {
                        return matches!(k.to_i64(), Some(k) if (0..bits as i64).contains(&k)
                            && low_bits_only(func, uses, dest.0, bits - k as usize, seen));
                    }
                    (IrBinOp::Add | IrBinOp::Sub | IrBinOp::Mul | IrBinOp::And | IrBinOp::Or | IrBinOp::Xor, _, _) => true,
                    (IrBinOp::Shl, _, _) => !matches!(rhs, Operand::Value(r) if r.0 == v),
                    _ => false,
                };
                low && low_bits_only(func, uses, dest.0, bits, seen)
            }
            _ => false,
        }
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::common::types::AliasClass;
    use crate::common::types::AddressSpace;
    use crate::ir::reexports::{BasicBlock, BlockId, IrBinOp, IrConst, Terminator, Value};

    /// A byte loaded before a loop and in it, joined by a phi (or by a phi
    /// that also takes `other`), extended in the loop.
    fn byte_phi(other: Operand) -> IrFunction {
        let v = |n| Operand::Value(Value(n));
        let load = |dest| Instruction::Load { dest: Value(dest), ptr: Value(0), ty: IrType::U8, seg_override: AddressSpace::Default, volatile: false, alias: AliasClass::ANY };
        let mut func = IrFunction::new("test".to_string(), IrType::I64, vec![], false);
        func.blocks = vec![
            BasicBlock { label: BlockId(0), instructions: vec![load(1)], terminator: Terminator::Branch(BlockId(1)), source_spans: Vec::new() },
            BasicBlock { label: BlockId(1), instructions: vec![
                Instruction::Phi { dest: Value(2), ty: IrType::U8, incoming: vec![(v(1), BlockId(0)), (other, BlockId(1))] },
                Instruction::Cast { dest: Value(3), src: v(2), from_ty: IrType::U8, to_ty: IrType::I64 },
                load(4),
            ], terminator: Terminator::CondBranch { cond: v(3), true_label: BlockId(1), false_label: BlockId(2) }, source_spans: Vec::new() },
            BasicBlock { label: BlockId(2), instructions: vec![], terminator: Terminator::Return(Some(v(3))), source_spans: Vec::new() },
        ];
        func
    }

    #[test]
    fn extension_of_loaded_bytes_through_a_phi_dropped() {
        let mut func = byte_phi(Operand::Value(Value(4)));
        assert_eq!(elide_extensions(&mut func), 1);
        assert!(matches!(func.blocks[1].instructions[1], Instruction::Copy { dest: Value(3), src: Operand::Value(Value(2)) }));
        // A constant out of the byte's range may not be what the register holds.
        let mut func = byte_phi(Operand::Const(IrConst::I64(300)));
        assert_eq!(elide_extensions(&mut func), 0);
        // Nor may a parameter's.
        let mut func = byte_phi(Operand::Value(Value(9)));
        assert_eq!(elide_extensions(&mut func), 0);
    }

    /// `c = i < n ? text[i] : k` as a long phi of a signed char load (widened)
    /// and a constant, then narrowed back to char: the widening and the
    /// narrowing are both copies when k fits in a char, neither when not.
    fn char_merge(k: i64) -> IrFunction {
        let v = |n| Operand::Value(Value(n));
        let mut func = IrFunction::new("test".to_string(), IrType::I64, vec![], false);
        func.blocks = vec![
            BasicBlock { label: BlockId(0), instructions: vec![
                Instruction::Load { dest: Value(1), ptr: Value(0), ty: IrType::I8, seg_override: AddressSpace::Default, volatile: false, alias: AliasClass::ANY },
                Instruction::Cast { dest: Value(2), src: v(1), from_ty: IrType::I8, to_ty: IrType::I64 },
            ], terminator: Terminator::CondBranch { cond: v(9), true_label: BlockId(1), false_label: BlockId(2) }, source_spans: Vec::new() },
            BasicBlock { label: BlockId(1), instructions: vec![], terminator: Terminator::Branch(BlockId(2)), source_spans: Vec::new() },
            BasicBlock { label: BlockId(2), instructions: vec![
                Instruction::Phi { dest: Value(3), ty: IrType::I64, incoming: vec![(v(2), BlockId(0)), (Operand::Const(IrConst::I64(k)), BlockId(1))] },
                Instruction::Cast { dest: Value(4), src: v(3), from_ty: IrType::I64, to_ty: IrType::I8 },
            ], terminator: Terminator::Return(Some(v(4))), source_spans: Vec::new() },
        ];
        func
    }

    #[test]
    fn char_merged_as_long_and_narrowed_back_is_copied() {
        let mut func = char_merge(32);
        elide_extensions(&mut func);
        assert!(matches!(func.blocks[0].instructions[1], Instruction::Copy { .. }));
        assert!(matches!(func.blocks[2].instructions[1], Instruction::Copy { dest: Value(4), src: Operand::Value(Value(3)) }));
        let mut func = char_merge(200);
        elide_extensions(&mut func);
        assert!(matches!(func.blocks[2].instructions[1], Instruction::Cast { .. }));
    }

    /// `(v1 << v2) | v3` in int, carried around a loop by a phi and stored
    /// as 16 bits, and `use_` of the phi (another 16-bit store, or a
    /// widening to 64 bits).
    fn int_or(use_: Instruction) -> IrFunction {
        let v = |n| Operand::Value(Value(n));
        let mut func = IrFunction::new("test".to_string(), IrType::Void, vec![], false);
        func.blocks = vec![
            BasicBlock { label: BlockId(0), instructions: vec![
                Instruction::Phi { dest: Value(3), ty: IrType::I32, incoming: vec![(Operand::Const(IrConst::I32(0)), BlockId(1)), (v(5), BlockId(0))] },
                Instruction::BinOp { dest: Value(4), op: IrBinOp::Shl, lhs: v(1), rhs: v(2), ty: IrType::I32 },
                Instruction::BinOp { dest: Value(5), op: IrBinOp::Or, lhs: v(3), rhs: v(4), ty: IrType::I32 },
                Instruction::Cast { dest: Value(6), src: v(5), from_ty: IrType::I32, to_ty: IrType::U16 },
                Instruction::Store { val: v(6), ptr: Value(0), ty: IrType::U16, seg_override: AddressSpace::Default, volatile: false, alias: AliasClass::ANY },
                use_,
            ], terminator: Terminator::CondBranch { cond: v(1), true_label: BlockId(0), false_label: BlockId(1) }, source_spans: Vec::new() },
            BasicBlock { label: BlockId(1), instructions: vec![], terminator: Terminator::Return(None), source_spans: Vec::new() },
        ];
        func
    }

    #[test]
    fn int_ops_read_only_low_bits_made_unsigned() {
        let ty = |func: &IrFunction, i: usize| match func.blocks[0].instructions[i] {
            Instruction::BinOp { ty, .. } => ty,
            _ => unreachable!(),
        };
        let store = Instruction::Store { val: Operand::Value(Value(3)), ptr: Value(0), ty: IrType::U16, seg_override: AddressSpace::Default, volatile: false, alias: AliasClass::ANY };
        let mut func = int_or(store);
        // (And the truncation only stored becomes a copy.)
        assert_eq!(elide_extensions(&mut func), 3);
        assert_eq!((ty(&func, 1), ty(&func, 2)), (IrType::U32, IrType::U32));
        assert!(matches!(func.blocks[0].instructions[3], Instruction::Copy { .. }));
        // The phi's sign extension is read.
        let widen = Instruction::Cast { dest: Value(7), src: Operand::Value(Value(3)), from_ty: IrType::I32, to_ty: IrType::I64 };
        let mut func = int_or(widen);
        assert_eq!(elide_extensions(&mut func), 1);
        assert_eq!((ty(&func, 1), ty(&func, 2)), (IrType::I32, IrType::I32));
    }

    /// `(ush)(a - b)` stored, and decremented in 16 bits for a widening;
    /// also compared when `compared`.
    fn distance(compared: bool) -> IrFunction {
        let v = |n| Operand::Value(Value(n));
        let mut insts = vec![
            Instruction::BinOp { dest: Value(5), op: IrBinOp::Sub, lhs: v(1), rhs: v(2), ty: IrType::U32 },
            Instruction::Cast { dest: Value(6), src: v(5), from_ty: IrType::U32, to_ty: IrType::U16 },
            Instruction::Store { val: v(6), ptr: Value(0), ty: IrType::U16, seg_override: AddressSpace::Default, volatile: false, alias: AliasClass::ANY },
            Instruction::BinOp { dest: Value(7), op: IrBinOp::Sub, lhs: v(6), rhs: Operand::Const(IrConst::I64(1)), ty: IrType::U16 },
            Instruction::Cast { dest: Value(8), src: v(7), from_ty: IrType::U16, to_ty: IrType::I64 },
        ];
        if compared {
            insts.push(Instruction::Cmp { dest: Value(9), op: crate::ir::reexports::IrCmpOp::Ult, lhs: v(6), rhs: Operand::Const(IrConst::I64(256)), ty: IrType::U16 });
        }
        let mut func = IrFunction::new("test".to_string(), IrType::I64, vec![], false);
        func.blocks = vec![BasicBlock { label: BlockId(0), instructions: insts, terminator: Terminator::Return(Some(v(8))), source_spans: Vec::new() }];
        func
    }

    #[test]
    fn truncation_read_only_in_its_bits_dropped() {
        let mut func = distance(false);
        elide_extensions(&mut func);
        assert!(matches!(func.blocks[0].instructions[1], Instruction::Copy { dest: Value(6), src: Operand::Value(Value(5)) }));
        // The widening of the 16-bit difference still masks.
        assert!(matches!(func.blocks[0].instructions[4], Instruction::Cast { from_ty: IrType::U16, .. }));
        // A compare reads the bits above.
        let mut func = distance(true);
        elide_extensions(&mut func);
        assert!(matches!(func.blocks[0].instructions[1], Instruction::Cast { .. }));
    }
}
