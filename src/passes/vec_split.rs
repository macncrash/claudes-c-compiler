//! Local arrays of vectors split into one local per vector.
//!
//! A vector lives in an alloca, and the backend can keep such an alloca in a
//! register, but only a whole one. BLAKE3's NEON state is `uint32x4_t v[16]`:
//! once its round function is inlined every access is at a constant offset,
//! yet all of it went through memory, 13 times clang's time.
//!
//!   %v = alloca 256                 %v0 = alloca 16; %v1 = alloca 16 ...
//!   %p = gep %v, 16
//!   memcpy %t, %p, 16        =>     memcpy %t, %v1, 16
//!   %q = vec add %p, %r             %q = vec add %v1, %r
//!
//! sroa.rs does this for scalars. Here every use stays inside one vector, so
//! splitting is only a renaming of addresses: a 16-byte vector operation at a
//! multiple of 16, or a load or store within one (the zeroing an initializer
//! starts with). A memcpy of whole vectors, as of the uint32x4x2_t an
//! intrinsic returns, becomes one per vector. Anything else, a variable index
//! (BLAKE3's message words, by table), an access across two, an address that
//! escapes, leaves the array as it is. So does every array of a function with
//! no vector operation: a split only pays for what can reach a register. (An
//! array itself may have none yet: an inlined intrinsic reads a copy of its
//! element until vec_copies, which runs later, removes the copy.)

use crate::common::fx_hash::{FxHashMap, FxHashSet};
use crate::common::types::{AddressSpace, IrType};
use crate::ir::reexports::{Instruction, IntrinsicOp, IrConst, IrFunction, Operand, Value};

const VECTOR: usize = 16;

/// Arrays up to this many bytes. Past the register pool's size little of it
/// could stay in registers anyway.
const MAX_SIZE: usize = 1024;

/// Split the vector arrays of `func`; returns how many were split.
pub(crate) fn split_function(func: &mut IrFunction) -> usize {
    let has_vectors = func.blocks.iter().flat_map(|b| &b.instructions)
        .any(|i| matches!(i, Instruction::Intrinsic { op: IntrinsicOp::VecOp { .. }, .. }));
    if !has_vectors {
        return 0;
    }
    let params: FxHashSet<u32> = func.param_alloca_values.iter().map(|v| v.0).collect();
    let mut sizes: FxHashMap<u32, usize> = FxHashMap::default();
    // In any block: those of inlined functions are where the call was, and an
    // alloca is a place in the frame wherever it is written.
    for inst in func.blocks.iter().flat_map(|b| &b.instructions) {
        if let Instruction::Alloca { dest, size, volatile: false, .. } = inst {
            if *size > VECTOR && *size <= MAX_SIZE && size.is_multiple_of(VECTOR) && !params.contains(&dest.0) {
                sizes.insert(dest.0, *size);
            }
        }
    }
    if sizes.is_empty() {
        return 0;
    }

    // Addresses within a candidate, as (array, offset). A value defined more
    // than once could be something else at each use.
    let mut defs: FxHashMap<u32, u32> = FxHashMap::default();
    for inst in func.blocks.iter().flat_map(|b| &b.instructions) {
        if let Some(d) = inst.dest() {
            *defs.entry(d.0).or_insert(0) += 1;
        }
    }
    let mut addr: FxHashMap<u32, (u32, i64)> = sizes.keys().map(|&a| (a, (a, 0))).collect();
    let mut bad: FxHashSet<u32> = FxHashSet::default();
    loop {
        let mut changed = false;
        for inst in func.blocks.iter().flat_map(|b| &b.instructions) {
            let (dest, base, off) = match inst {
                Instruction::GetElementPtr { dest, base, offset: Operand::Const(c), .. } => match c.to_i64() {
                    Some(k) => (*dest, *base, k),
                    None => continue,
                },
                Instruction::Copy { dest, src: Operand::Value(src) } => (*dest, *src, 0),
                _ => continue,
            };
            if addr.contains_key(&dest.0) {
                continue;
            }
            if let Some(&(a, k)) = addr.get(&base.0) {
                if defs.get(&dest.0) != Some(&1) {
                    bad.insert(a);
                    continue;
                }
                addr.insert(dest.0, (a, k + off));
                changed = true;
            }
        }
        if !changed {
            break;
        }
    }

    // Every use. Each must stay inside one vector, and most be all of it.
    let whole = |p: u32| addr.get(&p).is_none_or(|&(a, k)| {
        k >= 0 && (k as usize).is_multiple_of(VECTOR) && k as usize + VECTOR <= sizes[&a]
    });
    let vectors = |p: u32, n: usize| addr.get(&p).is_none_or(|&(a, k)| {
        n > 0 && n.is_multiple_of(VECTOR) && k >= 0 && (k as usize).is_multiple_of(VECTOR) && k as usize + n <= sizes[&a]
    });
    let inside = |p: u32, n: usize| addr.get(&p).is_none_or(|&(a, k)| {
        k >= 0 && k as usize % VECTOR + n <= VECTOR && k as usize + n <= sizes[&a]
    });
    for block in &func.blocks {
        for inst in &block.instructions {
            let mut reject = |v: u32| {
                if let Some(&(a, _)) = addr.get(&v) {
                    bad.insert(a);
                }
            };
            match inst {
                Instruction::Alloca { .. } => {}
                Instruction::GetElementPtr { dest, base, offset, .. } => {
                    if !addr.contains_key(&dest.0) {
                        reject(base.0);
                    }
                    if let Operand::Value(v) = offset {
                        reject(v.0);
                    }
                }
                Instruction::Copy { dest, src: Operand::Value(src) } => {
                    if !addr.contains_key(&dest.0) {
                        reject(src.0);
                    }
                }
                Instruction::Load { ptr, ty, volatile, seg_override, .. } => {
                    if *volatile || *seg_override != AddressSpace::Default || !inside(ptr.0, ty.size()) {
                        reject(ptr.0);
                    }
                }
                Instruction::Store { val, ptr, ty, volatile, seg_override, .. } => {
                    if let Operand::Value(v) = val {
                        reject(v.0);
                    }
                    if *volatile || *seg_override != AddressSpace::Default || !inside(ptr.0, ty.size()) {
                        reject(ptr.0);
                    }
                }
                Instruction::Memcpy { dest, src, size } => {
                    for p in [dest.0, src.0] {
                        if !vectors(p, *size) {
                            reject(p);
                        }
                    }
                }
                Instruction::Intrinsic { op: IntrinsicOp::VecOp { bytes, splat, .. }, dest_ptr, args, .. } => {
                    let fits = *bytes as usize == VECTOR;
                    for (i, arg) in args.iter().enumerate() {
                        let Operand::Value(v) = arg else { continue };
                        if !addr.contains_key(&v.0) {
                            continue;
                        }
                        if !fits || splat & (1 << i) != 0 || !whole(v.0) {
                            reject(v.0);
                        }
                    }
                    if let Some(d) = dest_ptr {
                        if addr.contains_key(&d.0) && (!fits || !whole(d.0)) {
                            reject(d.0);
                        }
                    }
                }
                other => other.for_each_used_value(&mut reject),
            }
        }
        block.terminator.for_each_used_value(|v| {
            if let Some(&(a, _)) = addr.get(&v) {
                bad.insert(a);
            }
        });
    }
    let split: FxHashSet<u32> = sizes.keys().copied().filter(|a| !bad.contains(a)).collect();
    if split.is_empty() {
        return 0;
    }

    // One local per vector, used or not: the unused cost no code.
    let mut next_id = func.next_value_id.max(func.max_value_id() + 1);
    let mut fields: FxHashMap<(u32, usize), Value> = FxHashMap::default();
    let mut ordered: Vec<u32> = split.iter().copied().collect();
    ordered.sort_unstable();
    for &a in &ordered {
        for i in 0..sizes[&a] / VECTOR {
            fields.insert((a, i), Value(next_id));
            next_id += 1;
        }
    }
    let renamed = |v: Value| -> Value {
        match addr.get(&v.0) {
            Some(&(a, k)) if split.contains(&a) => fields[&(a, k as usize / VECTOR)],
            _ => v,
        }
    };
    let is_gone = |d: &Value| addr.get(&d.0).is_some_and(|(a, _)| split.contains(a));
    for block in &mut func.blocks {
        let spans = block.source_spans.len() == block.instructions.len();
        let old = std::mem::take(&mut block.instructions);
        let old_spans = std::mem::take(&mut block.source_spans);
        for (k, mut inst) in old.into_iter().enumerate() {
            let mut replaced: Vec<Instruction> = Vec::new();
            match &mut inst {
                Instruction::Alloca { dest, ty, .. } if split.contains(&dest.0) => {
                    for i in 0..sizes[&dest.0] / VECTOR {
                        replaced.push(Instruction::Alloca { dest: fields[&(dest.0, i)], ty: *ty, size: VECTOR, align: VECTOR, volatile: false });
                    }
                }
                Instruction::GetElementPtr { dest, .. } | Instruction::Copy { dest, .. } if is_gone(dest) => {}
                Instruction::Load { ptr, .. } | Instruction::Store { ptr, .. } if is_gone(ptr) => {
                    // At its place within its vector.
                    let (_, off) = addr[&ptr.0];
                    let field = renamed(*ptr);
                    *ptr = field;
                    if !(off as usize).is_multiple_of(VECTOR) {
                        *ptr = Value(next_id);
                        next_id += 1;
                        replaced.push(Instruction::GetElementPtr {
                            dest: *ptr,
                            base: field,
                            offset: Operand::Const(IrConst::I64(off % VECTOR as i64)),
                            ty: IrType::Ptr,
                        });
                    }
                    replaced.push(inst);
                }
                Instruction::Memcpy { dest, src, size } if is_gone(dest) || is_gone(src) => {
                    // Vector by vector: the address of each within its side.
                    let (dest, src) = (*dest, *src);
                    for i in 0..*size / VECTOR {
                        let mut at = |p: Value| -> Value {
                            match addr.get(&p.0) {
                                Some(&(a, k)) if split.contains(&a) => fields[&(a, k as usize / VECTOR + i)],
                                _ if i == 0 => p,
                                _ => {
                                    let next = Value(next_id);
                                    next_id += 1;
                                    replaced.push(Instruction::GetElementPtr {
                                        dest: next,
                                        base: p,
                                        offset: Operand::Const(IrConst::I64((i * VECTOR) as i64)),
                                        ty: IrType::Ptr,
                                    });
                                    next
                                }
                            }
                        };
                        let (d, s) = (at(dest), at(src));
                        replaced.push(Instruction::Memcpy { dest: d, src: s, size: VECTOR });
                    }
                }
                Instruction::Intrinsic { op: IntrinsicOp::VecOp { splat, .. }, dest_ptr, args, .. } => {
                    let splat = *splat;
                    if let Some(d) = dest_ptr {
                        *d = renamed(*d);
                    }
                    for (i, arg) in args.iter_mut().enumerate() {
                        if let Operand::Value(v) = arg {
                            if splat & (1 << i) == 0 {
                                *v = renamed(*v);
                            }
                        }
                    }
                    replaced.push(inst);
                }
                _ => replaced.push(inst),
            }
            for r in replaced {
                block.instructions.push(r);
                if spans {
                    block.source_spans.push(old_spans[k]);
                }
            }
        }
    }
    func.next_value_id = next_id;
    split.len()
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::common::types::AliasClass;
    use crate::ir::reexports::{BasicBlock, BlockId, Terminator, VecElem, VecOpKind};

    fn alloca(id: u32, size: usize) -> Instruction {
        Instruction::Alloca { dest: Value(id), ty: IrType::Ptr, size, align: 16, volatile: false }
    }

    fn gep(id: u32, base: u32, off: i64) -> Instruction {
        Instruction::GetElementPtr { dest: Value(id), base: Value(base), offset: Operand::Const(IrConst::I64(off)), ty: IrType::Ptr }
    }

    fn memcpy(dest: u32, src: u32, size: usize) -> Instruction {
        Instruction::Memcpy { dest: Value(dest), src: Value(src), size }
    }

    fn vec_add(dest: u32, a: u32, b: u32, bytes: u8, splat: u8) -> Instruction {
        Instruction::Intrinsic {
            dest: None,
            op: IntrinsicOp::VecOp { op: VecOpKind::Add, elem: VecElem::U32, bytes, splat },
            dest_ptr: Some(Value(dest)),
            args: vec![Operand::Value(Value(a)), Operand::Value(Value(b))],
        }
    }

    fn store(ptr: u32, ty: IrType, volatile: bool) -> Instruction {
        Instruction::Store { val: Operand::Const(IrConst::I64(0)), ptr: Value(ptr), ty,
            seg_override: AddressSpace::Default, volatile, alias: AliasClass::ANY }
    }

    fn load(ptr: u32, ty: IrType, volatile: bool) -> Instruction {
        Instruction::Load { dest: Value(50), ptr: Value(ptr), ty,
            seg_override: AddressSpace::Default, volatile, alias: AliasClass::ANY }
    }

    fn run(instructions: Vec<Instruction>) -> (usize, Vec<Instruction>) {
        let mut func = IrFunction::new("test".to_string(), IrType::Void, vec![], false);
        func.next_value_id = 100;
        func.blocks.push(BasicBlock { label: BlockId(0), instructions, terminator: Terminator::Return(None), source_spans: Vec::new() });
        let n = split_function(&mut func);
        (n, func.blocks.remove(0).instructions)
    }

    #[test]
    fn test_elements_become_locals() {
        let (n, out) = run(vec![
            alloca(1, 32), alloca(2, 16), gep(3, 1, 16),
            memcpy(1, 2, 16), vec_add(3, 1, 2, 16, 0),
        ]);
        assert_eq!(n, 1);
        // The two fields, the other local, the memcpy, the add: no array, no GEP.
        assert_eq!(out.len(), 5);
        assert!(!out.iter().any(|i| matches!(i, Instruction::GetElementPtr { .. })));
        assert!(out.iter().all(|i| !matches!(i, Instruction::Alloca { size, .. } if *size != 16)));
        let Instruction::Memcpy { dest, .. } = &out[3] else { panic!() };
        let Instruction::Intrinsic { dest_ptr: Some(d), args, .. } = &out[4] else { panic!() };
        assert_ne!(dest.0, 1);
        assert_ne!(d.0, dest.0);
        assert!(matches!(args[0], Operand::Value(v) if v == *dest));
    }

    #[test]
    fn test_through_a_copy_and_gep_of_gep() {
        let (n, _) = run(vec![
            alloca(1, 64), alloca(2, 16), Instruction::Copy { dest: Value(3), src: Operand::Value(Value(1)) },
            gep(4, 3, 16), gep(5, 4, 32), vec_add(5, 3, 2, 16, 0),
        ]);
        assert_eq!(n, 1);
    }

    /// Each of these leaves the array alone.
    #[test]
    fn test_uses_that_keep_the_array() {
        let keep = |tail: Vec<Instruction>| {
            let mut all = vec![alloca(1, 32), alloca(2, 16), vec_add(1, 1, 2, 16, 0)];
            all.extend(tail);
            assert_eq!(run(all).0, 0);
        };
        // A variable index.
        keep(vec![Instruction::GetElementPtr { dest: Value(3), base: Value(1), offset: Operand::Value(Value(9)), ty: IrType::Ptr }]);
        // Half a vector, across two, or past the end.
        keep(vec![gep(3, 1, 8), memcpy(2, 3, 16)]);
        keep(vec![gep(3, 1, 32), memcpy(2, 3, 16)]);
        keep(vec![gep(3, 1, -16), memcpy(2, 3, 16)]);
        // Copied in part, or past the end.
        keep(vec![memcpy(2, 1, 8)]);
        keep(vec![memcpy(2, 1, 24)]);
        keep(vec![gep(3, 1, 16), memcpy(2, 3, 32)]);
        // Across two vectors, and past the end.
        keep(vec![gep(3, 1, 12), store(3, IrType::I64, false)]);
        keep(vec![gep(3, 1, 12), load(3, IrType::I64, false)]);
        keep(vec![gep(3, 1, 32), store(3, IrType::I8, false)]);
        // volatile.
        keep(vec![gep(3, 1, 20), store(3, IrType::I32, true)]);
        keep(vec![gep(3, 1, 20), load(3, IrType::I32, true)]);
        // Its address stored away.
        keep(vec![Instruction::Store { val: Operand::Value(Value(1)), ptr: Value(2), ty: IrType::Ptr,
            seg_override: AddressSpace::Default, volatile: false, alias: AliasClass::ANY }]);
        // An eight-byte operation, and a splat of what is at the address.
        keep(vec![vec_add(2, 1, 2, 8, 0)]);
        keep(vec![vec_add(2, 1, 2, 16, 1)]);
    }

    /// A copy of two vectors is two copies, to each vector and to its place
    /// in what is not split.
    #[test]
    fn test_copies_of_several_vectors() {
        let (n, out) = run(vec![alloca(1, 32), alloca(2, 16), vec_add(1, 1, 2, 16, 0), memcpy(9, 1, 32)]);
        assert_eq!(n, 1);
        let field = |i: usize| match &out[i] { Instruction::Alloca { dest, .. } => *dest, _ => panic!() };
        let tail = &out[out.len() - 3..];
        assert!(matches!(&tail[0], Instruction::Memcpy { dest, src, size: 16 } if dest.0 == 9 && *src == field(0)));
        let Instruction::GetElementPtr { dest: next, base, offset: Operand::Const(c), .. } = &tail[1] else { panic!() };
        assert_eq!((base.0, c.to_i64()), (9, Some(16)));
        assert!(matches!(&tail[2], Instruction::Memcpy { dest, src, size: 16 } if dest == next && *src == field(1)));
        // Both sides split, part way into one.
        let (n, out) = run(vec![alloca(1, 48), alloca(2, 32), alloca(3, 16), vec_add(1, 1, 3, 16, 0),
                                gep(4, 1, 16), memcpy(2, 4, 32)]);
        assert_eq!(n, 2);
        let copies: Vec<(u32, u32)> = out.iter().filter_map(|i| match i {
            Instruction::Memcpy { dest, src, size: 16 } => Some((dest.0, src.0)),
            _ => None,
        }).collect();
        // Fields are numbered in array order: %1's three, then %2's two.
        assert_eq!(copies, vec![(103, 101), (104, 102)]);
    }

    /// What is within one vector goes to that vector, at its place there.
    #[test]
    fn test_loads_and_stores_within_a_vector() {
        let (n, out) = run(vec![
            alloca(1, 32), alloca(2, 16), vec_add(1, 1, 2, 16, 0),
            gep(3, 1, 24), store(3, IrType::I64, false), gep(4, 1, 16), load(4, IrType::I64, false),
        ]);
        assert_eq!(n, 1);
        let field = |i: usize| match &out[i] { Instruction::Alloca { dest, .. } => *dest, _ => panic!() };
        // The second vector's: 8 bytes into it for the store, its start for the load.
        let second = field(1);
        let Instruction::GetElementPtr { dest, base, offset: Operand::Const(c), .. } = &out[4] else { panic!() };
        assert_eq!((*base, c.to_i64()), (second, Some(8)));
        assert!(matches!(&out[5], Instruction::Store { ptr, .. } if ptr == dest));
        assert!(matches!(&out[6], Instruction::Load { ptr, .. } if *ptr == second));
    }

    /// In a function doing vector work an array needs none of its own.
    #[test]
    fn test_only_where_there_are_vector_operations() {
        assert_eq!(run(vec![alloca(1, 32), alloca(2, 16), memcpy(1, 2, 16)]).0, 0);
        assert_eq!(run(vec![alloca(1, 32), alloca(2, 16), alloca(3, 16), memcpy(1, 2, 16), vec_add(2, 2, 3, 16, 0)]).0, 1);
    }

    #[test]
    fn test_a_returned_address_keeps_it() {
        let mut func = IrFunction::new("test".to_string(), IrType::Ptr, vec![], false);
        func.next_value_id = 100;
        func.blocks.push(BasicBlock {
            label: BlockId(0),
            instructions: vec![alloca(1, 32), alloca(2, 16), vec_add(1, 1, 2, 16, 0)],
            terminator: Terminator::Return(Some(Operand::Value(Value(1)))),
            source_spans: Vec::new(),
        });
        assert_eq!(split_function(&mut func), 0);
    }
}
