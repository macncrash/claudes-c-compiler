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
//! index), a copy of one, a phi all of whose incoming values are (or
//! constants in its type's range), or a 64-bit logical operation of
//! zero-extended values (crc32's `table[...] ^ (c >> 8)`). Runs late, after the passes that may
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
    // Through copies, phis, widenings and 64-bit logical operations, to a
    // fixpoint. Each value moves only down: from not yet known (`Top`), to
    // extended one way, to not at all. A phi of the narrow type is assumed
    // extended as it until an incoming value shows otherwise, a wider one
    // as its incoming values once one of them is known (words_kernel's
    // `i < n ? text[i] : ' '`, a char load or a space, merged as a long).
    #[derive(Clone, Copy, PartialEq)]
    enum Ext {
        Top,
        As(IrType),
        No,
    }
    let meet = |a: Ext, b: Ext| match (a, b) {
        (Ext::Top, x) | (x, Ext::Top) => x,
        (Ext::As(s), Ext::As(t)) if s == t => a,
        _ => Ext::No,
    };
    let settle = |ext: &[Option<IrType>]| -> Vec<Option<IrType>> {
        let mut state: Vec<Ext> = ext.iter().map(|e| e.map_or(Ext::No, Ext::As)).collect();
        for inst in func.blocks.iter().flat_map(|b| &b.instructions) {
            match inst {
                Instruction::Phi { dest, ty, .. } if narrow(ty) => state[dest.0 as usize] = Ext::As(*ty),
                Instruction::Phi { dest, ty, .. } if ty.is_integer() => state[dest.0 as usize] = Ext::Top,
                Instruction::Copy { dest, src: Operand::Value(_) } => state[dest.0 as usize] = Ext::Top,
                Instruction::Cast { dest, src: Operand::Value(_), from_ty, to_ty }
                    if from_ty.is_integer() && to_ty.is_integer() && to_ty.size() > from_ty.size() =>
                {
                    state[dest.0 as usize] = Ext::Top
                }
                Instruction::BinOp { dest, op: IrBinOp::And | IrBinOp::Or | IrBinOp::Xor | IrBinOp::LShr, ty: IrType::I64 | IrType::U64, .. }
                    if ext[dest.0 as usize].is_none() =>
                {
                    state[dest.0 as usize] = Ext::Top
                }
                Instruction::BinOp { dest, op: IrBinOp::AShr, rhs: Operand::Const(_), ty: IrType::I64 | IrType::U64, .. } => {
                    state[dest.0 as usize] = Ext::Top
                }
                _ => {}
            }
        }
        let of = |state: &[Ext], v: crate::ir::reexports::Value| state.get(v.0 as usize).copied().unwrap_or(Ext::No);
        // The size a zero-extended operand is extended from.
        let zext = |state: &[Ext], o: &Operand| match o {
            Operand::Value(v) => match of(state, *v) {
                Ext::Top => Ext::Top,
                Ext::As(e) if e.is_unsigned() => Ext::As(e),
                _ => Ext::No,
            },
            Operand::Const(c) => match c.to_i64() {
                Some(0..=0xff) => Ext::As(IrType::U8),
                Some(0x100..=0xffff) => Ext::As(IrType::U16),
                Some(0x1_0000..=0xffff_ffff) => Ext::As(IrType::U32),
                _ => Ext::No,
            },
    };
    let mut changed = true;
    let mut rounds = 0;
    while changed && rounds < 64 {
        changed = false;
        rounds += 1;
        for inst in func.blocks.iter().flat_map(|b| &b.instructions) {
            let (dest, now) = match inst {
                Instruction::Phi { dest, ty, incoming } if !narrow(ty) && ty.is_integer() => {
                    let mut t = Ext::Top;
                    for (op, _) in incoming {
                        if let Operand::Value(v) = op {
                            t = meet(t, of(&state, *v));
                        }
                    }
                    if let Ext::As(e) = t {
                        if !incoming.iter().all(|(op, _)| !matches!(op, Operand::Const(c) if !fits(c, e))) {
                            t = Ext::No;
                        }
                    }
                    (dest, t)
                }
                Instruction::Phi { dest, ty, incoming } => {
                    let ok = narrow(ty) && incoming.iter().all(|(op, _)| match op {
                        Operand::Value(v) => matches!(of(&state, *v), Ext::Top) || of(&state, *v) == Ext::As(*ty),
                        Operand::Const(c) => fits(c, *ty),
                    });
                    (dest, if ok { Ext::As(*ty) } else { Ext::No })
                }
                Instruction::Copy { dest, src: Operand::Value(v) } => (dest, of(&state, *v)),
                // A widening the loop below makes a copy (the char load
                // widened before the merge) holds its source as it is; one
                // it keeps extends it by its source type.
                Instruction::Cast { dest, src: Operand::Value(v), from_ty, to_ty }
                    if from_ty.is_integer() && to_ty.is_integer() && to_ty.size() > from_ty.size() =>
                {
                    (dest, match of(&state, *v) {
                        Ext::As(e) if covers(e, *from_ty) => Ext::As(e),
                        Ext::Top => Ext::Top,
                        _ => Ext::As(*from_ty),
                    })
                }
                // A shift right keeps the value within the range of the type
                // it was extended from, the sign with it (a binary search's
                // `(lwr + upr) >> 1`, widened to shift, merged back into an
                // int and widened again as an index).
                Instruction::BinOp { dest, op: IrBinOp::AShr, lhs, rhs: Operand::Const(_), ty: IrType::I64 | IrType::U64 } => {
                    (dest, match lhs {
                        Operand::Value(l) => of(&state, *l),
                        Operand::Const(_) => Ext::No,
                    })
                }
                // A 64-bit logical operation of zero-extended values, or one
                // shifted right by a constant, is zero-extended from the
                // wider of them (crc32's `crc_table[...] ^ (c >> 8)`, merged
                // back into the unsigned `c` and widened for the shift again).
                Instruction::BinOp { dest, op, lhs, rhs, ty: IrType::I64 | IrType::U64 }
                    if matches!(op, IrBinOp::And | IrBinOp::Or | IrBinOp::Xor | IrBinOp::LShr) && ext[dest.0 as usize].is_none() =>
                {
                    let (a, b) = (zext(&state, lhs), zext(&state, rhs));
                    (dest, match (op, a, b) {
                        (IrBinOp::LShr, a, _) if matches!(rhs, Operand::Const(_)) => a,
                        (IrBinOp::LShr, ..) | (_, Ext::No, _) | (_, _, Ext::No) => Ext::No,
                        (_, Ext::Top, _) | (_, _, Ext::Top) => Ext::Top,
                        (_, Ext::As(a), Ext::As(b)) => Ext::As(if a.size() >= b.size() { a } else { b }),
                    })
                }
                _ => continue,
            };
            let old = state[dest.0 as usize];
            let new = meet(old, now);
            if new != old {
                state[dest.0 as usize] = new;
                changed = true;
            }
        }
        // Values left unknown (a cycle of them with nothing known coming
        // in) are taken as not extended, and the rest settled again.
        if !changed && state.contains(&Ext::Top) {
            for s in state.iter_mut().filter(|s| **s == Ext::Top) {
                *s = Ext::No;
            }
            changed = true;
        }
    }
    // Unsettled after the cap, an optimistic phi may still be wrong: keep
    // only what the instructions themselves show.
    if changed {
        return ext.to_vec();
    }
    state.iter().map(|s| match s {
        Ext::As(t) => Some(*t),
        _ => None,
    }).collect()
    };
    // The backend keeps a signed 32-bit operation's result sign-extended
    // (an `sxtw` after it), but drops that `sxtw` where no use reads the
    // high half, as when the only one is a widening folded into an address
    // (`[x, w, sxtw #3]`). So an int operation counts as extended only for a
    // widening that needs a register of its own, used more than once or by
    // a phi: pqdownheap's `j`, a shift or an increment merged by a phi,
    // widened and merged again by loop rotation's phi.
    let mut seeded = ext.clone();
    for inst in func.blocks.iter().flat_map(|b| &b.instructions) {
        if let Instruction::BinOp { dest, op, ty: IrType::I32, .. } = inst {
            if !matches!(op, IrBinOp::UDiv | IrBinOp::URem) {
                seeded[dest.0 as usize] = Some(IrType::I32);
            }
        }
    }
    let int_ops = settle(&seeded);
    ext = settle(&ext);
    let mut uses = vec![0u32; max_id + 1];
    for block in &func.blocks {
        for inst in &block.instructions {
            // A phi or copy needs the value in a register as it is.
            let n = if matches!(inst, Instruction::Phi { .. } | Instruction::Copy { .. }) { 2 } else { 1 };
            inst.for_each_used_value(|v| if let Some(u) = uses.get_mut(v as usize) { *u += n });
        }
        block.terminator.for_each_used_value(|v| if let Some(u) = uses.get_mut(v as usize) { *u += 1 });
    }
    // (An elided extension's result is held as its source is, for a mask
    // of it further on.)
    let mut elided = 0;
    for inst in func.blocks.iter_mut().flat_map(|b| &mut b.instructions) {
        match *inst {
            Instruction::Cast { dest, src: Operand::Value(v), from_ty, to_ty }
                if (ext.get(v.0 as usize).copied().flatten().is_some_and(|e| covers(e, from_ty))
                    || (from_ty == IrType::I32 && uses[dest.0 as usize] > 1 && int_ops[v.0 as usize] == Some(IrType::I32)))
                    && from_ty.is_integer() && to_ty.is_integer() && to_ty.size() > from_ty.size()
                    // Not to 128 bits: the high register is the extension
                    // (the int64 division by a constant multiplies an
                    // int8_t widened to I128 and took its high half as 0).
                    && to_ty.size() <= 8 =>
            {
                *inst = Instruction::Copy { dest, src: Operand::Value(v) };
                ext[dest.0 as usize] = ext[v.0 as usize].or(Some(IrType::I32));
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
            // A narrowing read only in the bits it keeps, or a widening read
            // only in the bits it extends (jsonBlobAppendNode's
            // `a[0] = eType | (szPayload << 4)`, the u8 parameter masked with
            // `and #0xff` for a byte store).
            let kept = to_ty.size().min(from_ty.size());
            if from_ty.is_integer() && to_ty.is_integer() && to_ty.size() != from_ty.size() && from_ty.size() <= 8 && to_ty.size() <= 8
                && (to_ty.size() < from_ty.size() || !uses[dest.0 as usize].is_empty())
                && low_bits_only(func, &uses, dest.0, kept * 8, &mut Vec::new())
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
            // A cast between the signedness of one size is the register as it
            // is, so its uses read what it does (`(int32_t)a` tested whole
            // by a select).
            Instruction::Cast { dest, from_ty, to_ty, .. } if from_ty.is_integer() && to_ty.size() == from_ty.size() =>
                low_bits_only(func, uses, dest.0, bits, seen),
            Instruction::Cast { from_ty, to_ty, .. } => fits(to_ty) || (fits(from_ty) && to_ty.is_integer() && to_ty.size() > from_ty.size()),
            Instruction::Store { val: Operand::Value(x), ptr, ty, .. } => x.0 == v && ptr.0 != v && fits(ty),
            Instruction::Copy { dest, .. } | Instruction::Phi { dest, .. } => low_bits_only(func, uses, dest.0, bits, seen),
            // A compare of 32 or 64 bits within `bits` (narrower ones
            // compare whole w registers, the bits above included).
            Instruction::Cmp { ty, .. } => fits(ty) && ty.size() >= 4,
            // A value chosen (not the condition, tested whole): the
            // select's low bits are the chosen operand's (allocateBtreePage's
            // closest distance, `sqlite3AbsInt32(..)` kept as the minimum).
            Instruction::Select { dest, cond, .. } => !matches!(cond, Operand::Value(c) if c.0 == v)
                && low_bits_only(func, uses, dest.0, bits, seen),
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

    /// crc32's `c = table[...] ^ (c >> 8)`: an unsigned phi widened, shifted
    /// right and xored in 64 bits with `other`, then merged back into it.
    fn crc_loop(other: Operand) -> IrFunction {
        let v = |n| Operand::Value(Value(n));
        let load = |dest| Instruction::Load { dest: Value(dest), ptr: Value(0), ty: IrType::U32, seg_override: AddressSpace::Default, volatile: false, alias: AliasClass::ANY };
        let mut func = IrFunction::new("test".to_string(), IrType::I64, vec![], false);
        func.blocks = vec![
            BasicBlock { label: BlockId(0), instructions: vec![], terminator: Terminator::Branch(BlockId(1)), source_spans: Vec::new() },
            BasicBlock { label: BlockId(1), instructions: vec![
                Instruction::Phi { dest: Value(2), ty: IrType::U32, incoming: vec![(Operand::Const(IrConst::I64(0xffff_ffff)), BlockId(0)), (v(6), BlockId(1))] },
                Instruction::Cast { dest: Value(3), src: v(2), from_ty: IrType::U32, to_ty: IrType::I64 },
                Instruction::BinOp { dest: Value(4), op: IrBinOp::LShr, lhs: v(3), rhs: Operand::Const(IrConst::I64(8)), ty: IrType::I64 },
                load(5),
                Instruction::Cast { dest: Value(7), src: v(5), from_ty: IrType::U32, to_ty: IrType::I64 },
                Instruction::BinOp { dest: Value(8), op: IrBinOp::Xor, lhs: v(7), rhs: other, ty: IrType::I64 },
                Instruction::BinOp { dest: Value(6), op: IrBinOp::Xor, lhs: v(8), rhs: v(4), ty: IrType::I64 },
            ], terminator: Terminator::CondBranch { cond: v(9), true_label: BlockId(1), false_label: BlockId(2) }, source_spans: Vec::new() },
            BasicBlock { label: BlockId(2), instructions: vec![], terminator: Terminator::Return(Some(v(2))), source_spans: Vec::new() },
        ];
        func
    }

    #[test]
    fn unsigned_merged_from_logical_ops_of_zero_extended_values_not_widened() {
        let widened = |func: &IrFunction| matches!(func.blocks[1].instructions[1], Instruction::Cast { .. });
        let mut func = crc_loop(Operand::Const(IrConst::I64(0x1234)));
        elide_extensions(&mut func);
        assert!(!widened(&func));
        // A parameter's high bits, or a negative constant's, are not known zero.
        let mut func = crc_loop(Operand::Value(Value(9)));
        elide_extensions(&mut func);
        assert!(widened(&func));
        let mut func = crc_loop(Operand::Const(IrConst::I64(-1)));
        elide_extensions(&mut func);
        assert!(widened(&func));
    }

    /// pqdownheap's `j`: an int phi of `k << 1` and `j + 1`, widened (v4)
    /// and read `reads` times.
    fn int_phi_widened(reads: usize) -> IrFunction {
        let v = |n| Operand::Value(Value(n));
        let mut func = IrFunction::new("test".to_string(), IrType::I64, vec![], false);
        let mut body = vec![
            Instruction::Phi { dest: Value(2), ty: IrType::I32, incoming: vec![(v(1), BlockId(0)), (v(3), BlockId(1))] },
            Instruction::BinOp { dest: Value(3), op: IrBinOp::Add, lhs: v(2), rhs: Operand::Const(IrConst::I32(1)), ty: IrType::I32 },
            Instruction::Cast { dest: Value(4), src: v(2), from_ty: IrType::I32, to_ty: IrType::I64 },
        ];
        for r in 0..reads {
            body.push(Instruction::GetElementPtr { dest: Value(10 + r as u32), base: Value(0), offset: v(4), ty: IrType::Ptr });
        }
        func.blocks = vec![
            BasicBlock { label: BlockId(0), instructions: vec![
                Instruction::BinOp { dest: Value(1), op: IrBinOp::Shl, lhs: v(9), rhs: Operand::Const(IrConst::I32(1)), ty: IrType::I32 },
            ], terminator: Terminator::Branch(BlockId(1)), source_spans: Vec::new() },
            BasicBlock { label: BlockId(1), instructions: body, terminator: Terminator::CondBranch { cond: v(8), true_label: BlockId(1), false_label: BlockId(2) }, source_spans: Vec::new() },
            BasicBlock { label: BlockId(2), instructions: vec![], terminator: Terminator::Return(Some(v(3))), source_spans: Vec::new() },
        ];
        func
    }

    /// A binary search's `mid = (lwr + upr) >> 1`: the int sum widened and
    /// shifted in 64 bits by `op`, merged into the int `mid`, widened again
    /// as an index (v3).
    fn halved_midpoint(op: IrBinOp) -> IrFunction {
        let v = |n| Operand::Value(Value(n));
        let one = || Operand::Const(IrConst::I64(1));
        let mut func = IrFunction::new("test".to_string(), IrType::I64, vec![], false);
        func.blocks = vec![
            BasicBlock { label: BlockId(0), instructions: vec![
                Instruction::Cast { dest: Value(8), src: v(9), from_ty: IrType::I32, to_ty: IrType::I64 },
                Instruction::BinOp { dest: Value(5), op, lhs: v(8), rhs: one(), ty: IrType::I64 },
            ], terminator: Terminator::Branch(BlockId(1)), source_spans: Vec::new() },
            BasicBlock { label: BlockId(1), instructions: vec![
                Instruction::Phi { dest: Value(2), ty: IrType::I32, incoming: vec![(v(5), BlockId(0)), (v(6), BlockId(1))] },
                Instruction::Cast { dest: Value(3), src: v(2), from_ty: IrType::I32, to_ty: IrType::I64 },
                Instruction::BinOp { dest: Value(4), op: IrBinOp::Add, lhs: v(3), rhs: v(10), ty: IrType::I64 },
                Instruction::Cast { dest: Value(7), src: v(4), from_ty: IrType::I32, to_ty: IrType::I64 },
                Instruction::BinOp { dest: Value(6), op, lhs: v(7), rhs: one(), ty: IrType::I64 },
            ], terminator: Terminator::CondBranch { cond: v(3), true_label: BlockId(1), false_label: BlockId(2) }, source_spans: Vec::new() },
            BasicBlock { label: BlockId(2), instructions: vec![], terminator: Terminator::Return(Some(v(3))), source_spans: Vec::new() },
        ];
        func
    }

    #[test]
    fn int_shifted_right_after_widening_merged_and_widened_again_is_copied() {
        let mut func = halved_midpoint(IrBinOp::AShr);
        elide_extensions(&mut func);
        assert!(matches!(func.blocks[1].instructions[1], Instruction::Copy { dest: Value(3), .. }));
        // A logical shift of a negative int leaves it out of an int's range.
        let mut func = halved_midpoint(IrBinOp::LShr);
        elide_extensions(&mut func);
        assert!(matches!(func.blocks[1].instructions[1], Instruction::Cast { dest: Value(3), .. }));
    }

    #[test]
    fn int_operations_widened_into_a_register_of_their_own_are_copied() {
        let mut func = int_phi_widened(2);
        elide_extensions(&mut func);
        assert!(matches!(func.blocks[1].instructions[2], Instruction::Copy { dest: Value(4), .. }));
        // Read once, the widening folds into the access (`[x, w, sxtw]`)
        // and the operations' own `sxtw` goes: keep it there.
        let mut func = int_phi_widened(1);
        elide_extensions(&mut func);
        assert!(matches!(func.blocks[1].instructions[2], Instruction::Cast { .. }));
    }

    /// jsonBlobAppendNode's `a[0] = eType | (sz << 4)`: a u8 (v1) widened,
    /// or'ed with a 64-bit value and stored as `store_ty`.
    fn byte_or_stored(store_ty: IrType) -> IrFunction {
        let v = |n| Operand::Value(Value(n));
        let mut func = IrFunction::new("test".to_string(), IrType::Void, vec![], false);
        func.blocks = vec![BasicBlock { label: BlockId(0), instructions: vec![
            Instruction::Cast { dest: Value(3), src: v(1), from_ty: IrType::U8, to_ty: IrType::I64 },
            Instruction::BinOp { dest: Value(4), op: IrBinOp::Or, lhs: v(3), rhs: v(2), ty: IrType::I64 },
            Instruction::Store { val: v(4), ptr: Value(0), ty: store_ty, seg_override: AddressSpace::Default, volatile: false, alias: AliasClass::ANY },
        ], terminator: Terminator::Return(None), source_spans: Vec::new() }];
        func
    }

    #[test]
    fn widening_read_only_in_its_source_bits_dropped() {
        let mut func = byte_or_stored(IrType::U8);
        elide_extensions(&mut func);
        assert!(matches!(func.blocks[0].instructions[0], Instruction::Copy { dest: Value(3), .. }));
        // A 16-bit store reads the zeros the extension put above the byte.
        let mut func = byte_or_stored(IrType::U16);
        elide_extensions(&mut func);
        assert!(matches!(func.blocks[0].instructions[0], Instruction::Cast { .. }));
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

    /// `d = (int)x; m = d < m0 ? d : m0; *p = m`, with `cmp_ty` the
    /// compare's type; `d` is the select's condition too when `as_cond`.
    fn min_of_truncated(cmp_ty: IrType, as_cond: bool) -> IrFunction {
        let v = |n| Operand::Value(Value(n));
        let insts = vec![
            Instruction::Cast { dest: Value(3), src: v(1), from_ty: IrType::I64, to_ty: IrType::I32 },
            Instruction::Cmp { dest: Value(4), op: crate::ir::reexports::IrCmpOp::Slt, lhs: v(3), rhs: v(2), ty: cmp_ty },
            Instruction::Select { dest: Value(5), cond: v(if as_cond { 3 } else { 4 }), true_val: v(3), false_val: v(2), ty: IrType::I32 },
            Instruction::Store { val: v(5), ptr: Value(0), ty: IrType::I32, seg_override: AddressSpace::Default, volatile: false, alias: AliasClass::ANY },
        ];
        let mut func = IrFunction::new("test".to_string(), IrType::Void, vec![], false);
        func.blocks = vec![BasicBlock { label: BlockId(0), instructions: insts, terminator: Terminator::Return(None), source_spans: Vec::new() }];
        func
    }

    #[test]
    fn truncation_compared_and_selected_in_its_bits_dropped() {
        let mut func = min_of_truncated(IrType::I32, false);
        elide_extensions(&mut func);
        assert!(matches!(func.blocks[0].instructions[0], Instruction::Copy { dest: Value(3), .. }));
        // A byte compare reads the whole w register; a select's condition
        // is tested whole.
        for (ty, as_cond) in [(IrType::I8, false), (IrType::I32, true)] {
            let mut func = min_of_truncated(ty, as_cond);
            elide_extensions(&mut func);
            assert!(matches!(func.blocks[0].instructions[0], Instruction::Cast { .. }), "{ty:?} {as_cond}");
        }
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
