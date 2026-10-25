//! Small constant-length memcpy and memset calls done in place.
//!
//!   call memcpy(%d, %s, 56)          =>   memcpy %d <- %s, 56   (ldp/stp chunks)
//!   call memset(%d, 0, 24)           =>   store 0 -> %d; store 0 -> %d+8; store 0 -> %d+16
//!
//! Lowering does this for the fortified `__builtin___memcpy_chk` calls the
//! system headers make of every memcpy, but only for lengths that are
//! constant there. A length that becomes constant after optimization (a
//! size computed in an inlined helper, a field's size through a macro) left
//! a call, where clang copies or clears in place: SQLite clears 32- to
//! 120-byte objects this way in dozens of places. Lengths up to 128 bytes;
//! memset only on targets where unaligned 8-byte stores are fine (AArch64,
//! x86-64). Runs once, after the main optimization loop.

use crate::common::types::{AddressSpace, IrType};
use crate::ir::reexports::{Instruction, IrBinOp, IrConst, IrFunction, Operand, Value};

/// Largest length copied or cleared in place.
const MAX_INLINE: i64 = 128;

pub(crate) fn inline_function(func: &mut IrFunction, wide_stores: bool) -> usize {
    let mut next_id = func.next_value_id.max(func.max_value_id() + 1);
    let mut fresh = || { next_id += 1; Value(next_id - 1) };
    let mut inlined = 0;
    for block in &mut func.blocks {
        let mut k = 0;
        while k < block.instructions.len() {
            let Some(expansion) = expand(&block.instructions[k], wide_stores, &mut fresh) else {
                k += 1;
                continue;
            };
            let n = expansion.len();
            if !block.source_spans.is_empty() {
                let span = block.source_spans[k];
                block.source_spans.splice(k..=k, std::iter::repeat_n(span, n));
            }
            block.instructions.splice(k..=k, expansion);
            k += n;
            inlined += 1;
        }
    }
    func.next_value_id = next_id;
    inlined
}

/// The instructions replacing a small memcpy or memset call, if it is one.
fn expand(inst: &Instruction, wide_stores: bool, fresh: &mut dyn FnMut() -> Value) -> Option<Vec<Instruction>> {
    let Instruction::Call { func, info } = inst else { return None };
    let kind = func.as_str();
    if !matches!(kind, "memcpy" | "memset") || info.args.len() != 3 {
        return None;
    }
    let Operand::Const(len) = info.args[2] else { return None };
    let len = len.to_i64()?;
    let Operand::Value(dest) = info.args[0] else { return None };
    if !(0..=MAX_INLINE).contains(&len) {
        return None;
    }
    let mut out = Vec::new();
    match kind {
        "memcpy" => {
            let Operand::Value(src) = info.args[1] else { return None };
            if len > 0 {
                out.push(Instruction::Memcpy { dest, src, size: len as usize });
            }
        }
        _ => {
            if !wide_stores {
                return None;
            }
            // The byte, repeated in each of eight.
            let pattern = match info.args[1] {
                Operand::Const(c) => Operand::Const(IrConst::I64((c.to_i64()? & 0xff) * 0x0101_0101_0101_0101)),
                Operand::Value(v) => {
                    let from = *info.arg_types.get(1)?;
                    if !matches!(from, IrType::I32 | IrType::U32 | IrType::I64 | IrType::U64) {
                        return None;
                    }
                    let wide = if from.size() == 8 { v } else {
                        let w = fresh();
                        out.push(Instruction::Cast { dest: w, src: Operand::Value(v), from_ty: from, to_ty: IrType::I64 });
                        w
                    };
                    let byte = fresh();
                    out.push(Instruction::BinOp { dest: byte, op: IrBinOp::And, lhs: Operand::Value(wide),
                        rhs: Operand::Const(IrConst::I64(0xff)), ty: IrType::I64 });
                    let splat = fresh();
                    out.push(Instruction::BinOp { dest: splat, op: IrBinOp::Mul, lhs: Operand::Value(byte),
                        rhs: Operand::Const(IrConst::I64(0x0101_0101_0101_0101)), ty: IrType::I64 });
                    Operand::Value(splat)
                }
            };
            let mut offset = 0i64;
            for (width, ty) in [(8, IrType::I64), (4, IrType::I32), (2, IrType::I16), (1, IrType::I8)] {
                while len - offset >= width {
                    let val = match pattern {
                        Operand::Const(IrConst::I64(p)) => Operand::Const(IrConst::from_i64(p, ty)),
                        _ if ty == IrType::I64 => pattern,
                        Operand::Value(p) => {
                            let t = fresh();
                            out.push(Instruction::Cast { dest: t, src: Operand::Value(p), from_ty: IrType::I64, to_ty: ty });
                            Operand::Value(t)
                        }
                        _ => return None,
                    };
                    let ptr = if offset == 0 { dest } else {
                        let a = fresh();
                        out.push(Instruction::GetElementPtr { dest: a, base: dest, offset: Operand::Const(IrConst::I64(offset)), ty: IrType::I8 });
                        a
                    };
                    out.push(Instruction::Store { val, ptr, ty, seg_override: AddressSpace::Default, volatile: false });
                    offset += width;
                }
            }
        }
    }
    // Both return their destination.
    if let Some(r) = info.dest {
        out.push(Instruction::Copy { dest: r, src: Operand::Value(dest) });
    }
    Some(out)
}

/// A copy of exactly a scalar local's size into or out of it, with no other
/// local of another type on the other side, becomes a load and a store of
/// the local's own type, which mem2reg can then promote:
///
///   memcpy %value <- %p, 4      =>   %t = load U32 %p; store U32 %t -> %value
///
/// `memcpy(&v, p, sizeof v)` is how portable code reads an unaligned word
/// (zlib's hash insertion); the copy kept `v` on the stack, written and
/// read back for every byte hashed. The local's own type keeps a `double`
/// read this way a double. Only where unaligned scalar loads and stores
/// are fine (AArch64, x86-64). Runs before the first mem2reg.
pub(crate) fn scalarize_local_copies(func: &mut IrFunction) -> usize {
    use crate::common::fx_hash::{FxHashMap, FxHashSet};
    let scalar = |ty: &IrType| matches!(ty, IrType::I8 | IrType::U8 | IrType::I16 | IrType::U16 | IrType::I32
        | IrType::U32 | IrType::I64 | IrType::U64 | IrType::Ptr | IrType::F32 | IrType::F64);
    let mut locals: FxHashMap<u32, IrType> = FxHashMap::default();
    // Copies and casts (`(void *)&v`, `(size_t)4`), which lowering puts
    // between a call and its arguments.
    let bits = |t: &IrType| t.is_integer() || *t == IrType::Ptr;
    let mut moves: FxHashMap<u32, Operand> = FxHashMap::default();
    let mut defs: FxHashMap<u32, u32> = FxHashMap::default();
    for inst in func.blocks.iter().flat_map(|b| &b.instructions) {
        if let Some(d) = inst.dest() {
            *defs.entry(d.0).or_insert(0) += 1;
        }
        match inst {
            Instruction::Alloca { dest, ty, volatile: false, .. } if scalar(ty) => { locals.insert(dest.0, *ty); }
            Instruction::Copy { dest, src } => { moves.insert(dest.0, *src); }
            Instruction::GetElementPtr { dest, base, offset: Operand::Const(c), .. } if c.to_i64() == Some(0) => {
                moves.insert(dest.0, Operand::Value(*base));
            }
            // A pointer or integer passed on unchanged, or a constant.
            Instruction::Cast { dest, src, from_ty, to_ty }
                if matches!(src, Operand::Const(_)) || (bits(from_ty) && bits(to_ty) && from_ty.size() == to_ty.size()) => {
                moves.insert(dest.0, *src);
            }
            _ => {}
        }
    }
    moves.retain(|v, _| defs.get(v) == Some(&1));
    if locals.is_empty() {
        return 0;
    }
    let resolve = |op: Operand, via: &mut Vec<u32>| {
        let mut op = op;
        for _ in 0..4 {
            match op {
                Operand::Value(v) => match moves.get(&v.0) {
                    Some(&next) => {
                        via.push(v.0);
                        op = next;
                    }
                    None => break,
                },
                Operand::Const(_) => break,
            }
        }
        op
    };
    // Values something reads (a call's result is its destination).
    let mut used: FxHashSet<u32> = FxHashSet::default();
    for block in &func.blocks {
        for inst in &block.instructions {
            inst.for_each_used_value(|v| { used.insert(v); });
        }
        block.terminator.for_each_used_value(|v| { used.insert(v); });
    }
    let mut next_id = func.next_value_id.max(func.max_value_id() + 1);
    let mut count = 0;
    let mut bypassed: FxHashSet<u32> = FxHashSet::default();
    for block in &mut func.blocks {
        let mut k = 0;
        while k < block.instructions.len() {
            k += 1;
            let mut via = Vec::new();
            let (dest, src, size) = match &block.instructions[k - 1] {
                Instruction::Memcpy { dest, src, size } => (Operand::Value(*dest), Operand::Value(*src), *size),
                // A plain call, before mem_inline makes it a Memcpy.
                Instruction::Call { func: name, info } if name == "memcpy" && info.args.len() == 3
                    && info.dest.is_none_or(|r| !used.contains(&r.0)) => {
                    let len = match resolve(info.args[2], &mut via) { Operand::Const(c) => c.to_i64(), _ => None };
                    match len {
                        Some(n @ 1..=8) => (info.args[0], info.args[1], n as usize),
                        _ => continue,
                    }
                }
                _ => continue,
            };
            let k = k - 1;
            let (Operand::Value(dest), Operand::Value(src)) = (resolve(dest, &mut via), resolve(src, &mut via)) else { continue };
            let (d, s) = (locals.get(&dest.0).copied(), locals.get(&src.0).copied());
            let ty = match (d, s) {
                (Some(a), Some(b)) if a != b => continue,
                (Some(t), _) | (None, Some(t)) => t,
                (None, None) => continue,
            };
            if ty.size() != size {
                continue;
            }
            bypassed.extend(via);
            let t = Value(next_id);
            next_id += 1;
            block.instructions[k] = Instruction::Load { dest: t, ptr: src, ty, seg_override: AddressSpace::Default, volatile: false };
            block.instructions.insert(k + 1, Instruction::Store {
                val: Operand::Value(t), ptr: dest, ty, seg_override: AddressSpace::Default, volatile: false,
            });
            if !block.source_spans.is_empty() {
                let span = block.source_spans[k.min(block.source_spans.len() - 1)];
                block.source_spans.insert((k + 1).min(block.source_spans.len()), span);
            }
            count += 1;
        }
    }
    func.next_value_id = next_id;
    // The casts the copies were reached through, now unread, would still
    // count as uses of the local's address.
    loop {
        let mut used: FxHashSet<u32> = FxHashSet::default();
        for block in &func.blocks {
            for inst in &block.instructions {
                inst.for_each_used_value(|v| { used.insert(v); });
            }
            block.terminator.for_each_used_value(|v| { used.insert(v); });
        }
        let mut removed = false;
        for block in &mut func.blocks {
            let keep: Vec<bool> = block.instructions.iter()
                .map(|i| !i.dest().is_some_and(|d| bypassed.contains(&d.0) && !used.contains(&d.0)))
                .collect();
            if keep.iter().all(|&k| k) {
                continue;
            }
            removed = true;
            if block.source_spans.len() == keep.len() {
                let mut it = keep.iter();
                block.source_spans.retain(|_| *it.next().unwrap());
            }
            let mut it = keep.iter();
            block.instructions.retain(|_| *it.next().unwrap());
        }
        if !removed {
            break;
        }
    }
    count
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::ir::reexports::{BasicBlock, BlockId, CallInfo, Terminator};

    fn call(name: &str, args: Vec<Operand>, arg_types: Vec<IrType>) -> Instruction {
        Instruction::Call { func: name.to_string(), info: CallInfo {
            dest: Some(Value(9)), args, arg_types, return_type: IrType::Ptr, is_variadic: false, num_fixed_args: 3,
            struct_arg_sizes: vec![None; 3], struct_arg_aligns: vec![], struct_arg_classes: Vec::new(),
            struct_arg_riscv_float_classes: Vec::new(), is_sret: false, is_fastcall: false, ret_eightbyte_classes: Vec::new(),
        } }
    }

    fn func_with(inst: Instruction) -> IrFunction {
        let mut func = IrFunction::new("f".to_string(), IrType::Void, vec![], false);
        func.blocks = vec![BasicBlock { label: BlockId(0), instructions: vec![inst], terminator: Terminator::Return(None), source_spans: Vec::new() }];
        func.next_value_id = 10;
        func
    }

    #[test]
    fn copies_of_a_whole_local_become_a_load_and_a_store() {
        let alloca = |d: u32, ty: IrType| Instruction::Alloca { dest: Value(d), ty, size: 8, align: 0, volatile: false };
        // memcpy(&value, p, 4) through a zero GEP and a constant cast, its
        // result unused: the GEP and cast go too.
        let mut f = func_with(alloca(1, IrType::U32));
        f.blocks[0].instructions.extend([
            Instruction::GetElementPtr { dest: Value(3), base: Value(1), offset: Operand::Const(IrConst::I64(0)), ty: IrType::Ptr },
            Instruction::Cast { dest: Value(4), src: Operand::Const(IrConst::I32(4)), from_ty: IrType::I32, to_ty: IrType::U64 },
            call("memcpy", vec![Operand::Value(Value(3)), Operand::Value(Value(2)), Operand::Value(Value(4))],
                vec![IrType::Ptr, IrType::Ptr, IrType::U64]),
        ]);
        assert_eq!(scalarize_local_copies(&mut f), 1);
        assert!(matches!(f.blocks[0].instructions[..], [
            Instruction::Alloca { .. },
            Instruction::Load { ptr: Value(2), ty: IrType::U32, .. },
            Instruction::Store { ptr: Value(1), ty: IrType::U32, .. },
        ]), "{:?}", f.blocks[0].instructions);
        // A double keeps its type; a copy of part of a local, or between
        // locals of different types, stays.
        let mut f = func_with(alloca(1, IrType::F64));
        f.blocks[0].instructions.push(Instruction::Memcpy { dest: Value(1), src: Value(2), size: 8 });
        assert_eq!(scalarize_local_copies(&mut f), 1);
        assert!(matches!(f.blocks[0].instructions[1], Instruction::Load { ty: IrType::F64, .. }));
        let mut f = func_with(alloca(1, IrType::U64));
        f.blocks[0].instructions.push(Instruction::Memcpy { dest: Value(1), src: Value(2), size: 4 });
        assert_eq!(scalarize_local_copies(&mut f), 0);
        let mut f = func_with(alloca(1, IrType::U32));
        f.blocks[0].instructions.extend([alloca(2, IrType::F32), Instruction::Memcpy { dest: Value(1), src: Value(2), size: 4 }]);
        assert_eq!(scalarize_local_copies(&mut f), 0);
    }

    #[test]
    fn small_copies_and_clears_done_in_place() {
        let v = |n| Operand::Value(Value(n));
        let k = |n| Operand::Const(IrConst::I64(n));
        let mut f = func_with(call("memcpy", vec![v(1), v(2), k(56)], vec![IrType::Ptr, IrType::Ptr, IrType::U64]));
        assert_eq!(inline_function(&mut f, true), 1);
        assert!(matches!(f.blocks[0].instructions[..], [
            Instruction::Memcpy { dest: Value(1), src: Value(2), size: 56 },
            Instruction::Copy { dest: Value(9), src: Operand::Value(Value(1)) },
        ]));
        // 13 bytes of 0xab: an 8-, a 4- and a 1-byte store.
        let mut f = func_with(call("memset", vec![v(1), Operand::Const(IrConst::I32(0xab)), k(13)],
            vec![IrType::Ptr, IrType::I32, IrType::U64]));
        assert_eq!(inline_function(&mut f, true), 1);
        let stores: Vec<(IrType, i64)> = f.blocks[0].instructions.iter().filter_map(|i| match i {
            Instruction::Store { ty, val: Operand::Const(c), .. } => Some((*ty, c.to_i64().unwrap())),
            _ => None,
        }).collect();
        assert_eq!(stores, [(IrType::I64, 0xabababababababab_u64 as i64), (IrType::I32, 0xabababab_u32 as i32 as i64),
            (IrType::I8, 0xab_u8 as i8 as i64)]);
        // Too long, a variable length, or memset where stores must be aligned: left as calls.
        for (name, len, wide) in [("memcpy", k(129), true), ("memset", v(3), true), ("memset", k(16), false)] {
            let mut f = func_with(call(name, vec![v(1), v(2), len], vec![IrType::Ptr, IrType::I32, IrType::U64]));
            assert_eq!(inline_function(&mut f, wide), 0);
        }
    }
}
