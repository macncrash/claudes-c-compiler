//! Scalar replacement of local aggregates.
//!
//! A local struct or array reached only by loads and stores at constant
//! offsets, and by whole copies into or out of it, becomes one local per
//! field, which mem2reg then promotes to registers:
//!
//!   %r = alloca 24                     %r0 = alloca U32; %r4 = alloca U32 ...
//!   %p = gep %r, 4                     %t = load U32 %r4
//!   %t = load U32 %p              =>   store U32 %u -> %r4
//!   store U32 %u -> %p                 %a = load U32 %r0; store U32 %a -> %d
//!   memcpy %d <- %r, 24                ... one load and store per field
//!
//! A function's result struct (`HashResult r = {0}; ... r.misses += ...;
//! return r;`) otherwise lived on the stack, each counter loaded and stored
//! back at every iteration of the loops updating it.
//!
//! Fields are the narrowest ranges accessed; a constant store spanning
//! several of them exactly (the zeroing an initializer starts with) is split
//! into one store per field. Any other overlap, an access of a size other
//! than 1, 2, 4 or 8 bytes, a variable offset, or an address that escapes
//! (passed to a call, stored, compared) leaves the local alone. With a
//! whole copy, bytes no access reaches are copied too, in pieces of their
//! own. Only on little-endian targets, where a constant's bytes split low
//! first.

use crate::common::fx_hash::{FxHashMap, FxHashSet};
use crate::common::types::{AddressSpace, AliasClass, IrType};
use crate::ir::reexports::{Instruction, IrConst, IrFunction, Operand, Value};

/// Locals up to this many bytes, split into at most MAX_FIELDS fields.
const MAX_SIZE: usize = 256;
const MAX_FIELDS: usize = 16;

#[derive(Clone, Copy)]
struct Access {
    off: usize,
    size: usize,
    ty: IrType,
    /// A store of an integer constant (splittable across fields).
    konst: bool,
}

struct Slot {
    off: usize,
    size: usize,
    ty: IrType,
    value: Value,
}

fn int_type(size: usize) -> IrType {
    match size {
        1 => IrType::U8,
        2 => IrType::U16,
        4 => IrType::U32,
        _ => IrType::U64,
    }
}

/// Split the aggregate locals of `func`; returns how many were split.
pub(crate) fn split_function(func: &mut IrFunction) -> usize {
    let params: FxHashSet<u32> = func.param_alloca_values.iter().map(|v| v.0).collect();
    let mut sizes: FxHashMap<u32, usize> = FxHashMap::default();
    for inst in func.blocks.first().map(|b| b.instructions.as_slice()).unwrap_or(&[]) {
        if let Instruction::Alloca { dest, size, volatile: false, .. } = inst {
            if *size <= MAX_SIZE && !params.contains(&dest.0) {
                sizes.insert(dest.0, *size);
            }
        }
    }
    if sizes.is_empty() {
        return 0;
    }
    // Addresses within a candidate: (local, offset). Values defined more
    // than once are not followed.
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
                Instruction::GetElementPtr { dest, base, offset, .. } => match offset {
                    Operand::Const(c) => match c.to_i64_exact() {
                        Some(k) => (*dest, *base, k),
                        None => continue,
                    },
                    Operand::Value(_) => continue,
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
    // Every use of an address.
    let mut accesses: FxHashMap<u32, Vec<Access>> = FxHashMap::default();
    let mut copied: FxHashSet<u32> = FxHashSet::default();
    let within = |a: u32, k: i64, n: usize| k >= 0 && (k as usize) + n <= sizes[&a];
    for block in &func.blocks {
        for inst in &block.instructions {
            match inst {
                Instruction::Load { ptr, ty, volatile, seg_override, .. } => {
                    if let Some(&(a, k)) = addr.get(&ptr.0) {
                        let n = ty.size();
                        if *volatile || *seg_override != AddressSpace::Default || !matches!(n, 1 | 2 | 4 | 8)
                            || !within(a, k, n) {
                            bad.insert(a);
                        } else {
                            accesses.entry(a).or_default().push(Access { off: k as usize, size: n, ty: *ty, konst: false });
                        }
                    }
                }
                Instruction::Store { val, ptr, ty, volatile, seg_override, .. } => {
                    if let Operand::Value(v) = val {
                        if let Some(&(a, _)) = addr.get(&v.0) {
                            bad.insert(a);
                        }
                    }
                    if let Some(&(a, k)) = addr.get(&ptr.0) {
                        let n = ty.size();
                        if *volatile || *seg_override != AddressSpace::Default || !matches!(n, 1 | 2 | 4 | 8)
                            || !within(a, k, n) {
                            bad.insert(a);
                        } else {
                            let konst = matches!(val, Operand::Const(c) if c.to_i64_exact().is_some()) && ty.is_integer();
                            accesses.entry(a).or_default().push(Access { off: k as usize, size: n, ty: *ty, konst });
                        }
                    }
                }
                Instruction::GetElementPtr { base, offset, .. } => {
                    if let Some(&(a, _)) = addr.get(&base.0) {
                        if !matches!(offset, Operand::Const(c) if c.to_i64_exact().is_some()) {
                            bad.insert(a);
                        }
                    }
                    if let Operand::Value(v) = offset {
                        if let Some(&(a, _)) = addr.get(&v.0) {
                            bad.insert(a);
                        }
                    }
                }
                Instruction::Copy { .. } => {}
                Instruction::Memcpy { dest, src, size } => {
                    let (d, s) = (addr.get(&dest.0).copied(), addr.get(&src.0).copied());
                    match (d, s) {
                        (Some((a, _)), Some((b, _))) => {
                            bad.insert(a);
                            bad.insert(b);
                        }
                        (Some((a, k)), None) | (None, Some((a, k))) => {
                            if k != 0 || *size != sizes[&a] {
                                bad.insert(a);
                            } else {
                                copied.insert(a);
                            }
                        }
                        (None, None) => {}
                    }
                }
                _ => {
                    inst.for_each_used_value(|v| {
                        if let Some(&(a, _)) = addr.get(&v) {
                            bad.insert(a);
                        }
                    });
                }
            }
        }
        block.terminator.for_each_used_value(|v| {
            if let Some(&(a, _)) = addr.get(&v) {
                bad.insert(a);
            }
        });
    }
    // Fields of each local.
    let mut next_id = func.next_value_id.max(func.max_value_id() + 1);
    let mut plans: FxHashMap<u32, Vec<Slot>> = FxHashMap::default();
    let mut order: Vec<u32> = sizes.keys().copied().filter(|a| !bad.contains(a)).collect();
    order.sort_unstable();
    for a in order {
        let list = accesses.remove(&a).unwrap_or_default();
        if list.is_empty() {
            continue;
        }
        // Already one scalar: mem2reg's.
        if list.iter().all(|x| x.off == 0 && x.size == sizes[&a]) && !copied.contains(&a) {
            continue;
        }
        // A word copied whole is one load and store; split, a store per
        // field (zlib's inflate_table fills its tables with a 4-byte code).
        if copied.contains(&a) && sizes[&a] <= 8 {
            continue;
        }
        let Some(slots) = plan(&list, sizes[&a], copied.contains(&a)) else { continue };
        if slots.len() > MAX_FIELDS {
            continue;
        }
        let slots = slots.into_iter().map(|(off, size, ty)| {
            next_id += 1;
            Slot { off, size, ty, value: Value(next_id - 1) }
        }).collect();
        plans.insert(a, slots);
    }
    if plans.is_empty() {
        return 0;
    }
    // Rewrite.
    let slot_of = |plans: &FxHashMap<u32, Vec<Slot>>, a: u32, off: usize, size: usize| -> Option<(Value, IrType)> {
        plans.get(&a)?.iter().find(|s| s.off == off && s.size == size).map(|s| (s.value, s.ty))
    };
    let mut fresh = || {
        next_id += 1;
        Value(next_id - 1)
    };
    let mut dead: FxHashSet<u32> = FxHashSet::default();
    for (&v, &(a, _)) in &addr {
        if plans.contains_key(&a) {
            dead.insert(v);
        }
    }
    for bi in 0..func.blocks.len() {
        let block = &mut func.blocks[bi];
        let spans = block.source_spans.len() == block.instructions.len();
        let old = std::mem::take(&mut block.instructions);
        let old_spans = std::mem::take(&mut block.source_spans);
        let mut out = Vec::with_capacity(old.len());
        let mut out_spans = Vec::new();
        for (k, inst) in old.into_iter().enumerate() {
            let before = out.len();
            match inst {
                Instruction::Alloca { dest, .. } if plans.contains_key(&dest.0) => {
                    for s in &plans[&dest.0] {
                        out.push(Instruction::Alloca { dest: s.value, ty: s.ty, size: s.size, align: s.size, volatile: false });
                    }
                }
                Instruction::GetElementPtr { dest, .. } | Instruction::Copy { dest, .. } if dead.contains(&dest.0) => {}
                Instruction::Load { dest, ptr, ty, seg_override, volatile, alias } => {
                    match addr.get(&ptr.0).filter(|(a, _)| plans.contains_key(a)) {
                        Some(&(a, off)) => {
                            let (slot, _) = slot_of(&plans, a, off as usize, ty.size()).expect("load of a field");
                            out.push(Instruction::Load { dest, ptr: slot, ty, seg_override, volatile, alias });
                        }
                        None => out.push(Instruction::Load { dest, ptr, ty, seg_override, volatile, alias }),
                    }
                }
                Instruction::Store { val, ptr, ty, seg_override, volatile, alias } => {
                    match addr.get(&ptr.0).filter(|(a, _)| plans.contains_key(a)) {
                        Some(&(a, off)) => {
                            let (off, n) = (off as usize, ty.size());
                            match slot_of(&plans, a, off, n) {
                                Some((slot, _)) => out.push(Instruction::Store { val, ptr: slot, ty, seg_override, volatile, alias }),
                                None => {
                                    // A constant over several fields.
                                    let Operand::Const(c) = val else { unreachable!("split store of a constant") };
                                    let bits = c.to_i64_exact().expect("integer constant") as u64;
                                    for s in plans[&a].iter().filter(|s| s.off >= off && s.off + s.size <= off + n) {
                                        let shift = 8 * (s.off - off);
                                        let part = if s.size == 8 { bits >> shift } else { (bits >> shift) & ((1u64 << (8 * s.size)) - 1) };
                                        out.push(Instruction::Store { val: Operand::Const(IrConst::from_i64(part as i64, s.ty)),
                                            ptr: s.value, ty: s.ty, seg_override, volatile, alias });
                                    }
                                }
                            }
                        }
                        None => out.push(Instruction::Store { val, ptr, ty, seg_override, volatile, alias }),
                    }
                }
                Instruction::Memcpy { dest, src, size } => {
                    let d = addr.get(&dest.0).filter(|(a, _)| plans.contains_key(a)).map(|x| x.0);
                    let s = addr.get(&src.0).filter(|(a, _)| plans.contains_key(a)).map(|x| x.0);
                    let (a, other, into) = match (d, s) {
                        (Some(a), None) => (a, src, true),
                        (None, Some(a)) => (a, dest, false),
                        _ => {
                            out.push(Instruction::Memcpy { dest, src, size });
                            continue_spans(&mut out_spans, &old_spans, spans, k, out.len() - before);
                            continue;
                        }
                    };
                    for s in &plans[&a] {
                        let p = if s.off == 0 { other } else {
                            let g = fresh();
                            out.push(Instruction::GetElementPtr { dest: g, base: other, offset: Operand::Const(IrConst::I64(s.off as i64)), ty: IrType::I8 });
                            g
                        };
                        let t = fresh();
                        let (from, to) = if into { (p, s.value) } else { (s.value, p) };
                        out.push(Instruction::Load { dest: t, ptr: from, ty: s.ty, seg_override: AddressSpace::Default, volatile: false, alias: AliasClass::ANY });
                        out.push(Instruction::Store { val: Operand::Value(t), ptr: to, ty: s.ty, seg_override: AddressSpace::Default, volatile: false, alias: AliasClass::ANY });
                    }
                }
                other => out.push(other),
            }
            continue_spans(&mut out_spans, &old_spans, spans, k, out.len() - before);
        }
        block.instructions = out;
        block.source_spans = if spans { out_spans } else { old_spans };
    }
    func.next_value_id = next_id;
    plans.len()
}

fn continue_spans<T: Copy>(out: &mut Vec<T>, old: &[T], spans: bool, k: usize, n: usize) {
    if spans {
        for _ in 0..n {
            out.push(old[k]);
        }
    }
}

/// The fields of a local of `size` bytes accessed as `list`: (offset, size,
/// type), in offset order, or None where the accesses do not split.
fn plan(list: &[Access], size: usize, copied: bool) -> Option<Vec<(usize, usize, IrType)>> {
    let mut ranges: Vec<(usize, usize)> = list.iter().map(|x| (x.off, x.size)).collect();
    ranges.sort_unstable();
    ranges.dedup();
    // Fields: the ranges containing no other.
    let mut fields: Vec<(usize, usize)> = ranges.iter().copied()
        .filter(|&(o, n)| !ranges.iter().any(|&(p, m)| (p, m) != (o, n) && p >= o && p + m <= o + n))
        .collect();
    // Bytes only a wider constant store reaches (padding an initializer
    // zeroes) are pieces of their own.
    let mut pieces = Vec::new();
    for x in list.iter().filter(|x| x.konst && !fields.contains(&(x.off, x.size))) {
        let mut at = x.off;
        for &(o, n) in fields.iter().filter(|&&(o, n)| o < x.off + x.size && o + n > x.off) {
            if o > at {
                pieces.extend(aligned_pieces(at, o));
            }
            at = at.max(o + n);
        }
        if at < x.off + x.size {
            pieces.extend(aligned_pieces(at, x.off + x.size));
        }
    }
    fields.extend(pieces);
    fields.sort_unstable();
    fields.dedup();
    if fields.windows(2).any(|w| w[0].0 + w[0].1 > w[1].0) {
        return None;
    }
    // A wider range is a constant store exactly tiled by fields.
    for x in list {
        if fields.contains(&(x.off, x.size)) {
            continue;
        }
        if !x.konst {
            return None;
        }
        let mut at = x.off;
        for &(o, n) in fields.iter().filter(|&&(o, n)| o >= x.off && o + n <= x.off + x.size) {
            if o != at {
                return None;
            }
            at += n;
        }
        if at != x.off + x.size {
            return None;
        }
    }
    let mut out: Vec<(usize, usize, IrType)> = Vec::new();
    for &(o, n) in &fields {
        let mut ty: Option<IrType> = None;
        for x in list.iter().filter(|x| (x.off, x.size) == (o, n)) {
            match ty {
                None => ty = Some(x.ty),
                Some(t) if t == x.ty => {}
                // Integers of one size are one field; anything else mixed
                // (a float read as its bits) is not.
                Some(t) if t.is_integer() && x.ty.is_integer() => {}
                Some(_) => return None,
            }
        }
        let ty = ty.unwrap_or(int_type(n));
        if !(ty.is_integer() || ty.is_float() || ty == IrType::Ptr) || ty.is_long_double() {
            return None;
        }
        out.push((o, n, ty));
    }
    if copied {
        // The bytes between fields, in aligned pieces.
        let mut gaps = Vec::new();
        let mut at = 0;
        for &(o, n, _) in out.iter().chain(std::iter::once(&(size, 0, IrType::U8))) {
            if at < o {
                gaps.extend(aligned_pieces(at, o).into_iter().map(|(p, m)| (p, m, int_type(m))));
            }
            at = at.max(o + n);
        }
        out.extend(gaps);
        out.sort_unstable_by_key(|x| x.0);
    }
    Some(out)
}

/// [from, to) in naturally aligned pieces of 8, 4, 2 or 1 bytes.
fn aligned_pieces(from: usize, to: usize) -> Vec<(usize, usize)> {
    let mut out = Vec::new();
    let mut at = from;
    while at < to {
        let n = [8, 4, 2, 1].into_iter().find(|&w| at.is_multiple_of(w) && at + w <= to).unwrap();
        out.push((at, n));
        at += n;
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::ir::reexports::{BasicBlock, BlockId, Terminator};

    fn store(val: Operand, ptr: u32, ty: IrType) -> Instruction {
        Instruction::Store { val, ptr: Value(ptr), ty, seg_override: AddressSpace::Default, volatile: false, alias: AliasClass::ANY }
    }

    fn load(dest: u32, ptr: u32, ty: IrType) -> Instruction {
        Instruction::Load { dest: Value(dest), ptr: Value(ptr), ty, seg_override: AddressSpace::Default, volatile: false, alias: AliasClass::ANY }
    }

    fn gep(dest: u32, base: u32, off: i64) -> Instruction {
        Instruction::GetElementPtr { dest: Value(dest), base: Value(base), offset: Operand::Const(IrConst::I64(off)), ty: IrType::I8 }
    }

    fn func(insts: Vec<Instruction>) -> IrFunction {
        let mut f = IrFunction::new("f".to_string(), IrType::Void, vec![], false);
        f.blocks = vec![BasicBlock { label: BlockId(0), instructions: insts, terminator: Terminator::Return(None), source_spans: Vec::new() }];
        f.next_value_id = 100;
        f
    }

    /// { u32 a, b; u64 c } zeroed by 8-byte stores, its fields updated
    /// and the whole copied out (to a pointer): three locals, the zeroing split, the copy
    /// a load and a store per field.
    #[test]
    fn struct_with_zeroing_and_copy_out_splits() {
        let zero = Operand::Const(IrConst::I64(0));
        let mut f = func(vec![
            Instruction::Alloca { dest: Value(1), ty: IrType::Ptr, size: 16, align: 8, volatile: false },
            load(2, 50, IrType::Ptr),
            store(zero, 1, IrType::I64),
            gep(3, 1, 8),
            store(zero, 3, IrType::I64),
            gep(4, 1, 4),
            load(5, 4, IrType::U32),
            store(Operand::Value(Value(5)), 4, IrType::U32),
            load(6, 3, IrType::U64),
            store(Operand::Value(Value(6)), 3, IrType::U64),
            Instruction::Memcpy { dest: Value(2), src: Value(1), size: 16 },
        ]);
        assert_eq!(split_function(&mut f), 1);
        let insts = &f.blocks[0].instructions;
        let allocas: Vec<usize> = insts.iter().filter_map(|i| match i {
            Instruction::Alloca { size, .. } => Some(*size), _ => None }).collect();
        assert_eq!(allocas, vec![4, 4, 8]);
        // Nothing reads the old local or its addresses.
        for i in insts {
            i.for_each_used_value(|v| assert!(![1, 3, 4].contains(&v), "{i:?}"));
        }
        let memcpys = insts.iter().filter(|i| matches!(i, Instruction::Memcpy { .. })).count();
        assert_eq!(memcpys, 0);
        let zeros = insts.iter().filter(|i| matches!(i,
            Instruction::Store { val: Operand::Const(c), .. } if c.to_i64_exact() == Some(0))).count();
        assert_eq!(zeros, 3);
    }

    /// An address passed on, a variable index, or a partial overlap
    /// that is not a constant store keeps the local whole.
    #[test]
    fn escaping_or_overlapping_locals_stay() {
        let alloca = Instruction::Alloca { dest: Value(1), ty: IrType::Ptr, size: 16, align: 8, volatile: false };
        let mut escaped = func(vec![alloca.clone(), gep(2, 1, 4), load(3, 2, IrType::U32),
            store(Operand::Value(Value(2)), 50, IrType::Ptr)]);
        assert_eq!(split_function(&mut escaped), 0);
        let mut indexed = func(vec![alloca.clone(), Instruction::GetElementPtr { dest: Value(2), base: Value(1),
            offset: Operand::Value(Value(60)), ty: IrType::I8 }, load(3, 2, IrType::U32)]);
        assert_eq!(split_function(&mut indexed), 0);
        let mut overlap = func(vec![alloca, gep(2, 1, 4), load(3, 1, IrType::U64), load(4, 2, IrType::U32)]);
        assert_eq!(split_function(&mut overlap), 0);
    }
}
