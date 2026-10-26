//! Drop extensions of 8- and 16-bit values that their loads already made
//! (AArch64).
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
//! A value is taken as extended when it is an 8- or 16-bit load, a copy of
//! one, or a phi all of whose incoming values are (or constants in its
//! type's range). Runs late, after the passes that may replace a load with
//! another value of its type (GVN forwards stored values, whose high bits
//! an 8- or 16-bit operation may leave unspecified).

use crate::common::types::IrType;
use crate::ir::reexports::{Instruction, IrFunction, Operand};

pub(crate) fn elide_extensions(func: &mut IrFunction) -> usize {
    let max_id = func.max_value_id() as usize;
    let narrow = |ty: &IrType| ty.is_integer() && matches!(ty.size(), 1 | 2);
    // Values whose register holds them extended by their type.
    let mut ext: Vec<Option<IrType>> = vec![None; max_id + 1];
    for inst in func.blocks.iter().flat_map(|b| &b.instructions) {
        if let Instruction::Load { dest, ty, .. } = inst {
            if narrow(ty) {
                ext[dest.0 as usize] = Some(*ty);
            }
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
    elided
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::common::types::AddressSpace;
    use crate::ir::reexports::{BasicBlock, BlockId, IrConst, Terminator, Value};

    /// A byte loaded before a loop and in it, joined by a phi (or by a phi
    /// that also takes `other`), extended in the loop.
    fn byte_phi(other: Operand) -> IrFunction {
        let v = |n| Operand::Value(Value(n));
        let load = |dest| Instruction::Load { dest: Value(dest), ptr: Value(0), ty: IrType::U8, seg_override: AddressSpace::Default, volatile: false };
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
}
