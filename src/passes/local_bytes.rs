//! Loads of a local's bytes that are known constants.
//!
//!   const union { U32 u; BYTE c[4]; } one = { 1 };
//!   return one.c[0];
//!
//! is how portable C asks which way round the machine is: lz4, zstd and xxhash
//! all do, in their hottest loops, and take a branch on the answer. mem2reg
//! leaves a union alone, since its bytes are read at another width than they
//! are written, so the local was built in memory on every call, and both of the
//! branch's sides were kept. Here, for a local whose address goes nowhere, a
//! block's constant stores are followed byte by byte, and an integer load of
//! bytes all known becomes the constant. Later passes fold what depends on it.
//!
//! Bytes are in little-endian order, the order of every target.

use crate::common::fx_hash::{FxHashMap, FxHashSet};
use crate::common::types::IrType;
use crate::ir::reexports::{Instruction, IrConst, IrFunction, IrModule, Operand};

/// Largest local followed: unions and small structs, not arrays of data.
const MAX_SIZE: usize = 32;

pub(crate) fn fold_module(module: &mut IrModule) -> usize {
    let mut folded = 0;
    for func in module.functions.iter_mut().filter(|f| !f.is_declaration) {
        folded += fold_function(func);
    }
    folded
}

/// An integer type this can rebuild from bytes.
fn int_size(ty: IrType) -> Option<usize> {
    match ty {
        IrType::I8 | IrType::U8 => Some(1),
        IrType::I16 | IrType::U16 => Some(2),
        IrType::I32 | IrType::U32 => Some(4),
        IrType::I64 | IrType::U64 => Some(8),
        _ => None,
    }
}

fn fold_function(func: &mut IrFunction) -> usize {
    let insts = || func.blocks.iter().flat_map(|b| &b.instructions);
    let size: FxHashMap<u32, usize> = insts()
        .filter_map(|inst| match inst {
            Instruction::Alloca { dest, size, volatile: false, .. } if (1..=MAX_SIZE).contains(size) => Some((dest.0, *size)),
            _ => None,
        })
        .collect();
    if size.is_empty() {
        return 0;
    }
    // Pointers into a local: value -> (local, byte offset).
    let mut place: FxHashMap<u32, (u32, i64)> = size.keys().map(|&id| (id, (id, 0))).collect();
    loop {
        let mut changed = false;
        for inst in insts() {
            if let Instruction::GetElementPtr { dest, base, offset: Operand::Const(c), .. } = inst {
                if let (Some(&(local, at)), Some(k), false) = (place.get(&base.0), c.to_i64(), place.contains_key(&dest.0)) {
                    place.insert(dest.0, (local, at + k));
                    changed = true;
                }
            }
        }
        if !changed {
            break;
        }
    }
    // Any other use of a pointer lets something else reach the local.
    let mut escaped: FxHashSet<u32> = FxHashSet::default();
    for block in &func.blocks {
        for inst in &block.instructions {
            let mut others: Vec<u32> = Vec::new();
            match inst {
                Instruction::Alloca { .. } => {}
                Instruction::GetElementPtr { base, offset: Operand::Const(_), .. } if place.contains_key(&base.0) => {}
                Instruction::Load { volatile: false, .. } => {}
                Instruction::Store { val, volatile: false, .. } => {
                    if let Operand::Value(v) = val {
                        others.push(v.0);
                    }
                }
                _ => inst.for_each_used_value(|v| others.push(v)),
            }
            escaped.extend(others.iter().filter_map(|v| place.get(v).map(|p| p.0)));
        }
        block.terminator.for_each_used_value(|v| {
            if let Some(p) = place.get(&v) {
                escaped.insert(p.0);
            }
        });
    }
    // This runs before constants are folded, when an initializer's is a copy
    // or a cast of one. A cast to no more bytes keeps the low ones, all a store
    // takes; a wider one would have to extend, so is left out.
    let mut defs: FxHashMap<u32, u32> = FxHashMap::default();
    for inst in insts() {
        if let Some(d) = inst.dest() {
            *defs.entry(d.0).or_default() += 1;
        }
    }
    let constants: FxHashMap<u32, i64> = insts()
        .filter_map(|inst| match inst {
            Instruction::Copy { dest, src: Operand::Const(c) } => Some((dest.0, c.to_i64()?)),
            Instruction::Cast { dest, src: Operand::Const(c), from_ty, to_ty }
                if int_size(*to_ty).is_some() && int_size(*from_ty) >= int_size(*to_ty) => Some((dest.0, c.to_i64()?)),
            _ => None,
        })
        .filter(|(id, _)| defs.get(id) == Some(&1))
        .collect();
    let mut folded = 0;
    for block in &mut func.blocks {
        // What this block has stored so far.
        let mut bytes: FxHashMap<u32, Vec<Option<u8>>> = FxHashMap::default();
        for inst in &mut block.instructions {
            match inst {
                Instruction::Store { val, ptr, ty, .. } => {
                    let Some(&(local, at)) = place.get(&ptr.0) else { continue };
                    if escaped.contains(&local) {
                        continue;
                    }
                    let image = bytes.entry(local).or_insert_with(|| vec![None; size[&local]]);
                    let width = ty.size();
                    if at < 0 || at as usize + width > image.len() {
                        image.fill(None);
                        continue;
                    }
                    let known = match val {
                        Operand::Const(c) if int_size(*ty).is_some() => c.to_i64(),
                        Operand::Value(v) if int_size(*ty).is_some() => constants.get(&v.0).copied(),
                        _ => None,
                    };
                    for k in 0..width {
                        image[at as usize + k] = known.map(|v| (v >> (8 * k)) as u8);
                    }
                }
                Instruction::Load { dest, ptr, ty, .. } => {
                    let Some(&(local, at)) = place.get(&ptr.0) else { continue };
                    let (Some(image), Some(width)) = (bytes.get(&local), int_size(*ty)) else { continue };
                    if at < 0 || at as usize + width > image.len() {
                        continue;
                    }
                    let mut value: u64 = 0;
                    let mut all = true;
                    for k in 0..width {
                        match image[at as usize + k] {
                            Some(b) => value |= (b as u64) << (8 * k),
                            None => all = false,
                        }
                    }
                    if all {
                        *inst = Instruction::Copy { dest: *dest, src: Operand::Const(IrConst::from_i64(value as i64, *ty)) };
                        folded += 1;
                    }
                }
                _ => {}
            }
        }
    }
    folded
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::common::types::{AddressSpace, AliasClass};
    use crate::ir::reexports::{BasicBlock, BlockId, Terminator, Value};

    fn alloca(v: u32, size: usize) -> Instruction {
        Instruction::Alloca { dest: Value(v), ty: IrType::Ptr, size, align: 8, volatile: false }
    }

    fn store(ptr: u32, val: Operand, ty: IrType) -> Instruction {
        Instruction::Store { val, ptr: Value(ptr), ty, seg_override: AddressSpace::Default, volatile: false, alias: AliasClass::ANY }
    }

    fn load(dest: u32, ptr: u32, ty: IrType) -> Instruction {
        Instruction::Load { dest: Value(dest), ptr: Value(ptr), ty, seg_override: AddressSpace::Default, volatile: false, alias: AliasClass::ANY }
    }

    fn gep(dest: u32, base: u32, at: i64) -> Instruction {
        Instruction::GetElementPtr { dest: Value(dest), base: Value(base), offset: Operand::Const(IrConst::I64(at)), ty: IrType::I8 }
    }

    fn k(v: i64, ty: IrType) -> Operand {
        Operand::Const(IrConst::from_i64(v, ty))
    }

    fn run(instructions: Vec<Instruction>) -> (usize, Vec<Instruction>) {
        let mut f = IrFunction::new("f".to_string(), IrType::Void, vec![], false);
        f.blocks.push(BasicBlock { label: BlockId(0), instructions, terminator: Terminator::Return(None), source_spans: vec![] });
        let n = fold_function(&mut f);
        (n, f.blocks.remove(0).instructions)
    }

    fn constant(inst: &Instruction) -> Option<i64> {
        match inst {
            Instruction::Copy { src: Operand::Const(c), .. } => c.to_i64(),
            _ => None,
        }
    }

    #[test]
    fn test_the_byte_order_question_is_answered() {
        let (n, out) = run(vec![alloca(1, 4), store(1, k(1, IrType::I32), IrType::I32), load(2, 1, IrType::U8)]);
        assert_eq!(n, 1);
        assert_eq!(constant(&out[2]), Some(1));
    }

    #[test]
    fn test_bytes_at_an_offset_and_across_stores() {
        let (n, out) = run(vec![
            alloca(1, 8), gep(2, 1, 4), store(1, k(0x11223344, IrType::I32), IrType::I32), store(2, k(0x55667788, IrType::I32), IrType::I32),
            gep(3, 1, 3), load(4, 3, IrType::U16),
        ]);
        assert_eq!(n, 1);
        assert_eq!(constant(&out[5]), Some(0x8811));
    }

    #[test]
    fn test_a_constant_behind_a_cast_or_a_copy() {
        // The cast truncates: its low bytes are what is stored.
        let cast = Instruction::Cast { dest: Value(5), src: Operand::Const(IrConst::I64(0x1_0000_0001)), from_ty: IrType::I64, to_ty: IrType::U32 };
        let (n, out) = run(vec![alloca(1, 4), cast, store(1, Operand::Value(Value(5)), IrType::U32), load(2, 1, IrType::U32)]);
        assert_eq!(n, 1);
        assert_eq!(constant(&out[3]), Some(1));
        let copy = Instruction::Copy { dest: Value(5), src: Operand::Const(IrConst::I64(7)) };
        let (n, _) = run(vec![alloca(1, 4), copy, store(1, Operand::Value(Value(5)), IrType::U32), load(2, 1, IrType::U8)]);
        assert_eq!(n, 1);
        // A widening cast is not followed: it would have to extend.
        let cast = Instruction::Cast { dest: Value(5), src: Operand::Const(IrConst::I8(-1)), from_ty: IrType::I8, to_ty: IrType::U32 };
        let (n, _) = run(vec![alloca(1, 4), cast, store(1, Operand::Value(Value(5)), IrType::U32), load(2, 1, IrType::U32)]);
        assert_eq!(n, 0);
        // A value defined twice is no constant.
        let a = Instruction::Copy { dest: Value(5), src: Operand::Const(IrConst::I64(7)) };
        let b = Instruction::Copy { dest: Value(5), src: Operand::Value(Value(9)) };
        let (n, _) = run(vec![alloca(1, 4), a, b, store(1, Operand::Value(Value(5)), IrType::U32), load(2, 1, IrType::U8)]);
        assert_eq!(n, 0);
    }

    #[test]
    fn test_signed_bytes_keep_their_sign() {
        let (_, out) = run(vec![alloca(1, 4), store(1, k(0xff, IrType::I32), IrType::I32), load(2, 1, IrType::I8)]);
        assert_eq!(constant(&out[2]), Some(-1));
    }

    #[test]
    fn test_what_is_not_known_stays_a_load() {
        // A byte never stored.
        let (n, _) = run(vec![alloca(1, 8), store(1, k(1, IrType::I32), IrType::I32), load(2, 1, IrType::U64)]);
        assert_eq!(n, 0);
        // A store of a value overwrites what was known.
        let (n, _) = run(vec![alloca(1, 4), store(1, k(1, IrType::I32), IrType::I32), store(1, Operand::Value(Value(9)), IrType::I8), load(2, 1, IrType::U8)]);
        assert_eq!(n, 0);
    }

    #[test]
    fn test_a_local_that_escapes_is_left_alone() {
        let esc = store(9, Operand::Value(Value(1)), IrType::Ptr);
        let (n, _) = run(vec![alloca(1, 4), esc, store(1, k(1, IrType::I32), IrType::I32), load(2, 1, IrType::U8)]);
        assert_eq!(n, 0);
        let copy = Instruction::Memcpy { dest: Value(1), src: Value(9), size: 4 };
        let (n, _) = run(vec![alloca(1, 4), store(1, k(1, IrType::I32), IrType::I32), copy, load(2, 1, IrType::U8)]);
        assert_eq!(n, 0);
    }

    #[test]
    fn test_nothing_is_known_across_blocks() {
        let mut f = IrFunction::new("f".to_string(), IrType::Void, vec![], false);
        f.blocks.push(BasicBlock { label: BlockId(0), instructions: vec![alloca(1, 4), store(1, k(1, IrType::I32), IrType::I32)],
            terminator: Terminator::Branch(BlockId(1)), source_spans: vec![] });
        f.blocks.push(BasicBlock { label: BlockId(1), instructions: vec![load(2, 1, IrType::U8)], terminator: Terminator::Return(None), source_spans: vec![] });
        assert_eq!(fold_function(&mut f), 0);
    }
}
