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
//! half), a copy of one, or a phi all of whose incoming values are (or
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
use crate::ir::reexports::{Instruction, IrFunction, Operand};

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
            _ => {}
        }
    }
    let fits = |c: &crate::ir::reexports::IrConst, ty: IrType| {
        let Some(k) = c.to_i64() else { return false };
        let bits = ty.size() * 8;
        if ty.is_unsigned() { k >= 0 && k < 1 << bits } else { k >= -(1 << (bits - 1)) && k < 1 << (bits - 1) }
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
    while changed {
        changed = false;
        for inst in func.blocks.iter().flat_map(|b| &b.instructions) {
            let (dest, ty, ok) = match inst {
                Instruction::Phi { dest, ty, incoming } => (dest, *ty, narrow(ty) && incoming.iter().all(|(op, _)| match op {
                    Operand::Value(v) => assumed.get(v.0 as usize) == Some(&Some(*ty)),
                    Operand::Const(c) => fits(c, *ty),
                })),
                Instruction::Copy { dest, src: Operand::Value(v) } => match assumed.get(v.0 as usize).copied().flatten() {
                    Some(t) => (dest, t, true),
                    None => (dest, IrType::Void, false),
                },
                _ => continue,
            };
            let now = if ok { Some(ty) } else { None };
            if assumed[dest.0 as usize] != now && (now.is_none() || matches!(inst, Instruction::Copy { .. })) {
                assumed[dest.0 as usize] = now;
                changed = true;
            }
        }
    }
    ext = assumed;
    let mut elided = 0;
    for inst in func.blocks.iter_mut().flat_map(|b| &mut b.instructions) {
        let Instruction::Cast { dest, src: Operand::Value(v), from_ty, to_ty } = *inst else { continue };
        if ext.get(v.0 as usize) == Some(&Some(from_ty)) && to_ty.is_integer() && to_ty.size() > from_ty.size() {
            *inst = Instruction::Copy { dest, src: Operand::Value(v) };
            elided += 1;
        }
    }
    elided + unsigned_low_ops(func)
}

/// Retype signed 32-bit ALU operations whose uses read only their low bits
/// to U32: the backend keeps an I32 result sign-extended (a `sxtw` after
/// each), which no such use reads. compress_block's
/// `s->bi_buf |= (ush)value << s->bi_valid` shifted and or-ed in int, for a
/// 16-bit store and its two bytes.
fn unsigned_low_ops(func: &mut IrFunction) -> usize {
    use crate::ir::reexports::IrBinOp;
    let max_id = func.max_value_id() as usize;
    let mut uses: Vec<Vec<Option<(usize, usize)>>> = vec![Vec::new(); max_id + 1];
    for (b, block) in func.blocks.iter().enumerate() {
        for (i, inst) in block.instructions.iter().enumerate() {
            inst.for_each_used_value(|v| if let Some(u) = uses.get_mut(v as usize) { u.push(Some((b, i))) });
        }
        block.terminator.for_each_used_value(|v| if let Some(u) = uses.get_mut(v as usize) { u.push(None) });
    }
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
    use crate::ir::reexports::IrBinOp;
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
            Instruction::Cast { to_ty, .. } => fits(to_ty),
            Instruction::Store { val: Operand::Value(x), ptr, ty, .. } => x.0 == v && ptr.0 != v && fits(ty),
            Instruction::Copy { dest, .. } | Instruction::Phi { dest, .. } => low_bits_only(func, uses, dest.0, bits, seen),
            Instruction::BinOp { dest, op, lhs, rhs, ty: IrType::I64 | IrType::U64 | IrType::I32 | IrType::U32 } => {
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
        assert_eq!(elide_extensions(&mut func), 2);
        assert_eq!((ty(&func, 1), ty(&func, 2)), (IrType::U32, IrType::U32));
        // The phi's sign extension is read.
        let widen = Instruction::Cast { dest: Value(7), src: Operand::Value(Value(3)), from_ty: IrType::I32, to_ty: IrType::I64 };
        let mut func = int_or(widen);
        assert_eq!(elide_extensions(&mut func), 0);
        assert_eq!((ty(&func, 1), ty(&func, 2)), (IrType::I32, IrType::I32));
    }
}
