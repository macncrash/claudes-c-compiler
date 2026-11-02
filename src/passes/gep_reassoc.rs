//! Move constant GEP offsets past variable ones, into the memory accesses.
//! (And, in a loop, an invariant subtrahend of an index into the base:
//! see hoist_invariant_subtrahends.)
//!
//!   %f = gep %p, 120                          %t = gep %p, %i
//!   %a = gep %f, %i                       =>  %a = gep %t, 120
//!   load %a / store -> %a                     load %a / store -> %a
//!
//! The backend folds a constant-offset GEP into the loads and stores that
//! use it ([t, #120]), but only when those are its only uses. A field
//! address indexed by a variable (`p->board[sq]`) is a constant GEP used by
//! a variable one, so it was computed once and kept in a register for every
//! access; after the rewrite the field offset folds into each access and the
//! struct pointer is the only base left live.
//!
//! Only GEPs whose every use is the address of a load or store (the ones
//! the backend folds) are rewritten. On AArch64 a constant GEP indexed by
//! two or more of them in one block stays their base instead (an access
//! takes a constant or a scaled index, not both). Runs once, after the main pipeline, so
//! that GVN and LICM see the original form.

use crate::common::fx_hash::{FxHashMap, FxHashSet};
use crate::common::types::{AddressSpace, IrType};
use crate::ir::analysis::CfgAnalysis;
use crate::ir::reexports::{Instruction, IrBinOp, IrConst, IrFunction, Operand, Value};

/// aarch64: a constant GEP indexed by two or more variable GEPs in one
/// block stays their base (see reassociate_constant_bases).
pub(crate) fn reassociate_function(func: &mut IrFunction, aarch64: bool) -> usize {
    let split = split_index_constants(func);
    reassociate_constant_bases(func, aarch64) + split
}

/// Whether an AArch64 add takes k as its immediate (12 bits, optionally
/// shifted left by 12).
fn add_imm(k: i64) -> bool {
    (0..=0xfff).contains(&k) || (k & 0xfff == 0 && (0..=0xfff).contains(&(k >> 12)))
}

/// A constant GEP whose offset no add immediate holds becomes one from an
/// earlier GEP of the same base in its block whose offset is within one:
/// the braided CRC's table rows 5 to 7, at 5120 to 7168, were each a movz
/// and an add every iteration, and are now the row before plus 1024.
/// AArch64; runs last, after remat may have recomputed such GEPs.
pub(crate) fn chain_large_offsets(func: &mut IrFunction) -> usize {
    let mut changes = 0;
    for block in &mut func.blocks {
        // (base, offset, dest) of the constant GEPs so far.
        let mut seen: Vec<(Value, i64, Value)> = Vec::new();
        for inst in &mut block.instructions {
            let Instruction::GetElementPtr { dest, base, offset: Operand::Const(c), ty } = *inst else { continue };
            let Some(k) = c.to_i64() else { continue };
            if !add_imm(k) && !add_imm(-k) {
                if let Some(&(_, o, from)) = seen.iter()
                    .filter(|&&(b, o, _)| b == base && o < k && add_imm(k - o))
                    .min_by_key(|&&(_, o, _)| k - o) {
                    *inst = Instruction::GetElementPtr { dest, base: from, offset: Operand::Const(IrConst::I64(k - o)), ty };
                    changes += 1;
                }
            }
            seen.push((base, k, dest));
        }
    }
    changes
}

fn reassociate_constant_bases(func: &mut IrFunction, aarch64: bool) -> usize {
    // Constant-offset GEPs: dest -> (base, offset).
    let mut const_gep: FxHashMap<u32, (Value, Operand)> = FxHashMap::default();
    for inst in func.blocks.iter().flat_map(|b| &b.instructions) {
        if let Instruction::GetElementPtr { dest, base, offset: offset @ Operand::Const(_), .. } = inst {
            const_gep.insert(dest.0, (*base, *offset));
        }
    }
    if const_gep.is_empty() {
        return 0;
    }

    // Values used anywhere other than as a foldable load or store address.
    let mut other_use: FxHashSet<u32> = FxHashSet::default();
    for block in &func.blocks {
        for inst in &block.instructions {
            match inst {
                Instruction::Load { ty, seg_override, .. }
                    if !matches!(ty, IrType::I128 | IrType::U128) && *seg_override == AddressSpace::Default => {}
                Instruction::Store { val, ty, seg_override, .. }
                    if !matches!(ty, IrType::I128 | IrType::U128) && *seg_override == AddressSpace::Default => {
                    if let Operand::Value(v) = val {
                        other_use.insert(v.0);
                    }
                }
                _ => inst.for_each_used_value(|v| { other_use.insert(v); }),
            }
        }
        block.terminator.for_each_used_value(|v| { other_use.insert(v); });
    }

    let mut next_id = if func.next_value_id > 0 { func.next_value_id } else { func.max_value_id() + 1 };
    let mut changes = 0;
    // On AArch64 an access takes a constant offset or a (scaled) index, not
    // both, so the rewrite trades the constant GEP for an add at each access.
    // That pays only when the constant GEP is otherwise held in a register
    // across code that does not use it. Two or more accesses in a block
    // through one constant GEP keep it as their base, built in that block:
    // the braided CRC's table rows (crc_braid_table[k] + 4 * byte, five
    // bytes per row) take one add per row instead of one per byte.
    let shared: FxHashSet<(usize, u32)> = if aarch64 {
        let mut count: FxHashMap<(usize, u32), u32> = FxHashMap::default();
        for (b, block) in func.blocks.iter().enumerate() {
            for inst in &block.instructions {
                if let Instruction::GetElementPtr { dest, base, offset: Operand::Value(_), .. } = inst {
                    if const_gep.contains_key(&base.0) && !other_use.contains(&dest.0) {
                        *count.entry((b, base.0)).or_insert(0) += 1;
                    }
                }
            }
        }
        count.into_iter().filter(|&(_, n)| n >= 2).map(|(k, _)| k).collect()
    } else {
        FxHashSet::default()
    };
    let def_block: FxHashMap<u32, usize> = func.blocks.iter().enumerate()
        .flat_map(|(b, block)| block.instructions.iter().filter_map(move |i| i.dest().map(|d| (d.0, b))))
        .filter(|(d, _)| const_gep.contains_key(d))
        .collect();
    for (b, block) in func.blocks.iter_mut().enumerate() {
        // Each shared constant GEP defined elsewhere, rebuilt here.
        let mut local: FxHashMap<u32, Value> = FxHashMap::default();
        let mut i = 0;
        while i < block.instructions.len() {
            if let Instruction::GetElementPtr { dest, base, offset: index @ Operand::Value(_), ty } = block.instructions[i] {
                if let Some(&(root, offset)) = const_gep.get(&base.0) {
                    if shared.contains(&(b, base.0)) {
                        if def_block.get(&base.0) != Some(&b) {
                            let here = match local.get(&base.0) {
                                Some(&v) => v,
                                None => {
                                    let v = Value(next_id);
                                    next_id += 1;
                                    block.instructions.insert(i, Instruction::GetElementPtr { dest: v, base: root, offset, ty: IrType::Ptr });
                                    if !block.source_spans.is_empty() {
                                        let span = block.source_spans[i];
                                        block.source_spans.insert(i, span);
                                    }
                                    i += 1;
                                    local.insert(base.0, v);
                                    changes += 1;
                                    v
                                }
                            };
                            block.instructions[i] = Instruction::GetElementPtr { dest, base: here, offset: index, ty };
                        }
                    } else if !other_use.contains(&dest.0) {
                        let inner = Value(next_id);
                        next_id += 1;
                        block.instructions[i] = Instruction::GetElementPtr { dest, base: inner, offset, ty };
                        block.instructions.insert(i, Instruction::GetElementPtr { dest: inner, base: root, offset: index, ty });
                        if !block.source_spans.is_empty() {
                            let span = block.source_spans[i];
                            block.source_spans.insert(i, span);
                        }
                        changes += 1;
                        i += 1;
                    }
                }
            }
            i += 1;
        }
    }
    func.next_value_id = next_id;
    if changes > 0 {
        merge_dominated_geps(func);
    }
    changes
}

/// `gep %p, (%x + C) << k` (or `gep %p, %x + C`), its index read only there
/// and the address only by loads, stores and constant GEPs that are:
///
///   %a = add %x, 257                          %t = shl %x, 2
///   %o = shl %a, 2                       =>   %i = gep %p, %t
///   %d = gep %p, %o                           %d = gep %i, 1028
///   %e = gep %d, 2                            %e = gep %i, 1030
///
/// The constant then folds into each access (`tree[code + 257].Len` loads
/// `[t, #1030]` after `add t, p, x, lsl #2`), instead of being added to
/// the index first. The add and shift left unused go with DCE.
fn split_index_constants(func: &mut IrFunction) -> usize {
    let mut binops: FxHashMap<u32, (IrBinOp, Operand, Operand)> = FxHashMap::default();
    let mut uses: FxHashMap<u32, u32> = FxHashMap::default();
    // Values used other than as a load or store address or the base of a
    // constant GEP, and the constant GEPs of each base.
    let mut other_use: FxHashSet<u32> = FxHashSet::default();
    let mut const_users: FxHashMap<u32, Vec<u32>> = FxHashMap::default();
    for block in &func.blocks {
        for inst in &block.instructions {
            inst.for_each_used_value(|v| *uses.entry(v).or_insert(0) += 1);
            match inst {
                Instruction::BinOp { dest, op, lhs, rhs, ty: IrType::I64 | IrType::U64 } => {
                    binops.insert(dest.0, (*op, *lhs, *rhs));
                    inst.for_each_used_value(|v| { other_use.insert(v); });
                }
                Instruction::Load { ty, seg_override, .. }
                    if !matches!(ty, IrType::I128 | IrType::U128) && *seg_override == AddressSpace::Default => {}
                Instruction::Store { val, ty, seg_override, .. }
                    if !matches!(ty, IrType::I128 | IrType::U128) && *seg_override == AddressSpace::Default => {
                    if let Operand::Value(v) = val {
                        other_use.insert(v.0);
                    }
                }
                Instruction::GetElementPtr { dest, base, offset: Operand::Const(_), .. } => {
                    const_users.entry(base.0).or_default().push(dest.0);
                }
                _ => inst.for_each_used_value(|v| { other_use.insert(v); }),
            }
        }
        block.terminator.for_each_used_value(|v| { *uses.entry(v).or_insert(0) += 1; other_use.insert(v); });
    }
    let accessed_only = |v: u32| !other_use.contains(&v)
        && const_users.get(&v).is_none_or(|gs| gs.iter().all(|g| !other_use.contains(g) && !const_users.contains_key(g)));
    let single = |v: &Value| uses.get(&v.0) == Some(&1);
    // An index (x + C) or ((x + C) << k): (x, C, k).
    let split = |o: &Value| -> Option<(Value, i64, u32)> {
        let add = |v: &Value| -> Option<(Value, i64)> {
            match binops.get(&v.0)? {
                (IrBinOp::Add, Operand::Value(x), Operand::Const(c)) | (IrBinOp::Add, Operand::Const(c), Operand::Value(x)) => Some((*x, c.to_i64()?)),
                _ => None,
            }
        };
        if !single(o) {
            return None;
        }
        if let Some((x, c)) = add(o) {
            return Some((x, c, 0));
        }
        let (IrBinOp::Shl, Operand::Value(a), Operand::Const(k)) = binops.get(&o.0)? else { return None };
        let k = k.to_i64().filter(|k| (0..8).contains(k))? as u32;
        let (x, c) = add(a).filter(|_| single(a))?;
        Some((x, c, k))
    };

    let mut next_id = func.next_value_id.max(func.max_value_id() + 1);
    // Address -> (inner GEP, its constant), for its constant GEPs.
    let mut rebased: FxHashMap<u32, (Value, i64)> = FxHashMap::default();
    for block in &mut func.blocks {
        let mut i = 0;
        while i < block.instructions.len() {
            if let Instruction::GetElementPtr { dest, base, offset: Operand::Value(o), ty } = block.instructions[i] {
                if let Some((x, c, k)) = split(&o).filter(|_| accessed_only(dest.0)) {
                    let constant = c.wrapping_shl(k);
                    if constant.unsigned_abs() < 1 << 20 {
                        let mut new = Vec::new();
                        let index = if k == 0 {
                            x
                        } else {
                            let t = Value(next_id);
                            next_id += 1;
                            new.push(Instruction::BinOp { dest: t, op: IrBinOp::Shl, lhs: Operand::Value(x), rhs: Operand::Const(IrConst::I64(k as i64)), ty: IrType::I64 });
                            t
                        };
                        let inner = Value(next_id);
                        next_id += 1;
                        new.push(Instruction::GetElementPtr { dest: inner, base, offset: Operand::Value(index), ty });
                        block.instructions[i] = Instruction::GetElementPtr { dest, base: inner, offset: Operand::Const(IrConst::I64(constant)), ty };
                        let n = new.len();
                        if !block.source_spans.is_empty() {
                            let span = block.source_spans[i];
                            block.source_spans.splice(i..i, std::iter::repeat_n(span, n));
                        }
                        block.instructions.splice(i..i, new);
                        rebased.insert(dest.0, (inner, constant));
                        i += n;
                    }
                }
            }
            i += 1;
        }
    }
    func.next_value_id = next_id;
    if rebased.is_empty() {
        return 0;
    }
    for inst in func.blocks.iter_mut().flat_map(|b| &mut b.instructions) {
        if let Instruction::GetElementPtr { base, offset, .. } = inst {
            let Operand::Const(c2) = offset else { continue };
            if let (Some(&(inner, constant)), Some(c2v)) = (rebased.get(&base.0), c2.to_i64()) {
                *base = inner;
                *offset = Operand::Const(IrConst::I64(constant + c2v));
            }
        }
    }
    rebased.len()
}

/// The rewrite makes one `gep %p, %i` per access (`rays[d][sq]` for each
/// ray d), and GVN has already run, so a GEP with the same base and offset
/// as one in a dominating block (or earlier in its own) becomes a copy of it.
fn merge_dominated_geps(func: &mut IrFunction) {
    let cfg = CfgAnalysis::build(func);
    // Depth-first over the dominator tree, with the GEPs seen on the path.
    let mut seen: FxHashMap<(u32, u32), Value> = FxHashMap::default();
    let mut stack: Vec<(usize, bool)> = vec![(0, false)];
    let mut added: Vec<Vec<(u32, u32)>> = vec![Vec::new(); func.blocks.len()];
    while let Some((b, leaving)) = stack.pop() {
        if leaving {
            for key in added[b].drain(..) {
                seen.remove(&key);
            }
            continue;
        }
        for inst in &mut func.blocks[b].instructions {
            let Instruction::GetElementPtr { dest, base, offset: Operand::Value(offset), .. } = *inst else { continue };
            let key = (base.0, offset.0);
            match seen.get(&key) {
                Some(&first) => *inst = Instruction::Copy { dest, src: Operand::Value(first) },
                None => {
                    seen.insert(key, dest);
                    added[b].push(key);
                }
            }
        }
        stack.push((b, true));
        stack.extend(cfg.dom_children[b].iter().map(|&c| (c, false)));
    }
}

/// A loop's access at a variable offset less a loop-invariant one, from a
/// loop-invariant base, gets that base less it from the preheader:
///
///   loop:                                    pre:  %b2 = sub %b, %d
///     %o2 = sub %o, %d                  =>
///     %a = gep %b, %o2                       loop: %a = gep %b2, %o
///
/// lz_decompress's `out[o - dist]`, a subtraction per byte copied. Only when
/// the subtrahend has no other use in the loop, so the new base takes the
/// register it held there; the innermost loop holding the access.
pub(crate) fn hoist_invariant_subtrahends(func: &mut IrFunction) -> usize {
    use super::loop_analysis;
    if func.blocks.len() < 2 {
        return 0;
    }
    let cfg = CfgAnalysis::build(func);
    let mut loops = loop_analysis::merge_loops_by_header(
        loop_analysis::find_natural_loops(cfg.num_blocks, &cfg.preds, &cfg.succs, &cfg.idom));
    loops.sort_by_key(|lp| lp.body.len());
    let mut innermost: Vec<Option<usize>> = vec![None; func.blocks.len()];
    for (l, lp) in loops.iter().enumerate() {
        for &b in &lp.body {
            innermost[b].get_or_insert(l);
        }
    }
    let max_id = func.max_value_id() as usize;
    let mut def_block: Vec<Option<usize>> = vec![None; max_id + 1];
    let mut uses: Vec<Vec<usize>> = vec![Vec::new(); max_id + 1];
    for (b, block) in func.blocks.iter().enumerate() {
        for inst in &block.instructions {
            if let Some(d) = inst.dest() {
                def_block[d.0 as usize] = Some(b);
            }
            inst.for_each_used_value(|v| if let Some(u) = uses.get_mut(v as usize) { u.push(b) });
        }
        block.terminator.for_each_used_value(|v| if let Some(u) = uses.get_mut(v as usize) { u.push(b) });
    }
    // (loop, base, subtrahend) -> the new base.
    let mut made: FxHashMap<(usize, u32, u32), Value> = FxHashMap::default();
    let mut next_id = func.next_value_id.max(max_id as u32 + 1);
    let mut rewrites: Vec<(usize, usize, Value, Operand)> = Vec::new();
    let mut pre_insts: Vec<(usize, Instruction)> = Vec::new();
    for (b, &l) in innermost.iter().enumerate() {
        let Some(l) = l else { continue };
        let body = &loops[l].body;
        let Some(pre) = loop_analysis::find_preheader(loops[l].header, body, &cfg.preds) else { continue };
        let outside = |v: Value| def_block.get(v.0 as usize).copied().flatten().is_none_or(|d| !body.contains(&d));
        for (k, inst) in func.blocks[b].instructions.iter().enumerate() {
            let Instruction::GetElementPtr { base, offset: Operand::Value(off), .. } = *inst else { continue };
            if !outside(base) || uses[off.0 as usize].len() != 1 {
                continue;
            }
            let Some(db) = def_block[off.0 as usize].filter(|d| body.contains(d)) else { continue };
            let Some(Instruction::BinOp { op: IrBinOp::Sub, lhs, rhs: Operand::Value(d), ty: IrType::I64 | IrType::U64, .. }) =
                func.blocks[db].instructions.iter().find(|i| i.dest() == Some(off)).cloned() else { continue };
            if !outside(d) || uses[d.0 as usize].iter().filter(|u| body.contains(u)).count() != 1 {
                continue;
            }
            let nb = *made.entry((l, base.0, d.0)).or_insert_with(|| {
                let nb = Value(next_id);
                next_id += 1;
                pre_insts.push((pre, Instruction::BinOp {
                    dest: nb, op: IrBinOp::Sub, lhs: Operand::Value(base), rhs: Operand::Value(d), ty: IrType::I64,
                }));
                nb
            });
            rewrites.push((b, k, nb, lhs));
        }
    }
    for &(b, k, nb, lhs) in &rewrites {
        if let Instruction::GetElementPtr { base, offset, .. } = &mut func.blocks[b].instructions[k] {
            *base = nb;
            *offset = lhs;
        }
    }
    for (pre, inst) in pre_insts {
        let block = &mut func.blocks[pre];
        if block.source_spans.len() == block.instructions.len() && !block.source_spans.is_empty() {
            let span = *block.source_spans.last().unwrap();
            block.source_spans.push(span);
        }
        block.instructions.push(inst);
    }
    func.next_value_id = next_id;
    rewrites.len()
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::common::types::AliasClass;
    use crate::ir::reexports::{BasicBlock, BlockId, Terminator};

    /// %3 = %1 + 257; %4 = %3 << 2; %5 = gep %0, %4; %6 = gep %5, 2;
    /// loads of %5 and %6, and `escape` stores %5 itself.
    fn indexed(escape: bool) -> IrFunction {
        let c = |k| Operand::Const(IrConst::I64(k));
        let load = |dest, ptr| Instruction::Load { dest: Value(dest), ptr: Value(ptr), ty: IrType::I16, seg_override: AddressSpace::Default, volatile: false, alias: AliasClass::ANY };
        let mut instructions = vec![
            Instruction::BinOp { dest: Value(3), op: IrBinOp::Add, lhs: Operand::Value(Value(1)), rhs: c(257), ty: IrType::I64 },
            Instruction::BinOp { dest: Value(4), op: IrBinOp::Shl, lhs: Operand::Value(Value(3)), rhs: c(2), ty: IrType::I64 },
            Instruction::GetElementPtr { dest: Value(5), base: Value(0), offset: Operand::Value(Value(4)), ty: IrType::Ptr },
            Instruction::GetElementPtr { dest: Value(6), base: Value(5), offset: c(2), ty: IrType::Ptr },
            load(7, 5),
            load(8, 6),
        ];
        if escape {
            instructions.push(Instruction::Store { val: Operand::Value(Value(5)), ptr: Value(2), ty: IrType::Ptr, seg_override: AddressSpace::Default, volatile: false, alias: AliasClass::ANY });
        }
        let mut func = IrFunction::new("test".to_string(), IrType::I32, vec![], false);
        func.blocks = vec![BasicBlock { label: BlockId(0), instructions, terminator: Terminator::Return(Some(Operand::Value(Value(8)))), source_spans: Vec::new() }];
        func.next_value_id = 9;
        func
    }

    #[test]
    fn index_constant_moves_into_the_accesses() {
        let mut func = indexed(false);
        assert_eq!(split_index_constants(&mut func), 1);
        let insts = &func.blocks[0].instructions;
        assert!(matches!(insts[2], Instruction::BinOp { dest: Value(9), op: IrBinOp::Shl, lhs: Operand::Value(Value(1)), .. }), "{:?}", insts[2]);
        assert!(matches!(insts[3], Instruction::GetElementPtr { dest: Value(10), base: Value(0), offset: Operand::Value(Value(9)), .. }), "{:?}", insts[3]);
        assert!(matches!(insts[4], Instruction::GetElementPtr { dest: Value(5), base: Value(10), offset: Operand::Const(IrConst::I64(1028)), .. }), "{:?}", insts[4]);
        assert!(matches!(insts[5], Instruction::GetElementPtr { dest: Value(6), base: Value(10), offset: Operand::Const(IrConst::I64(1030)), .. }), "{:?}", insts[5]);
        // An address that is itself stored keeps its index.
        let mut func = indexed(true);
        assert_eq!(split_index_constants(&mut func), 0);
    }

    #[test]
    fn invariant_subtrahend_of_an_index_moves_into_the_base() {
        let mut func = crate::passes::lftr::tests::copy_loop(0, false);
        assert_eq!(hoist_invariant_subtrahends(&mut func), 1);
        // out - dist before the loop, indexed by o.
        let Instruction::BinOp { dest: nb, op: IrBinOp::Sub, lhs: Operand::Value(Value(0)), rhs: Operand::Value(Value(2)), .. } =
            func.blocks[0].instructions[0] else { panic!("{:?}", func.blocks[0].instructions) };
        assert!(matches!(func.blocks[2].instructions[1],
            Instruction::GetElementPtr { dest: Value(14), base, offset: Operand::Value(Value(10)), .. } if base == nb));
        // Not when dist is read in the loop too: both would be live there.
        let mut func = crate::passes::lftr::tests::copy_loop(0, false);
        func.blocks[2].instructions.push(Instruction::Store {
            val: Operand::Value(Value(2)), ptr: Value(18), ty: IrType::I64,
            seg_override: AddressSpace::Default, volatile: false, alias: AliasClass::ANY,
        });
        assert_eq!(hoist_invariant_subtrahends(&mut func), 0);
    }

    #[test]
    fn large_offsets_taken_from_an_earlier_gep() {
        let c = |k| Operand::Const(IrConst::I64(k));
        let gep = |dest, base, k| Instruction::GetElementPtr { dest: Value(dest), base: Value(base), offset: c(k), ty: IrType::Ptr };
        let mut f = IrFunction::new("test".to_string(), IrType::Void, vec![], false);
        f.blocks = vec![BasicBlock { label: BlockId(0), instructions: vec![
            gep(1, 0, 4096), gep(2, 0, 5120), gep(3, 0, 6144), gep(4, 0, 70000),
        ], terminator: Terminator::Return(None), source_spans: Vec::new() }];
        assert_eq!(chain_large_offsets(&mut f), 2);
        assert!(matches!(f.blocks[0].instructions[..], [
            Instruction::GetElementPtr { base: Value(0), .. },
            Instruction::GetElementPtr { base: Value(1), offset: Operand::Const(IrConst::I64(1024)), .. },
            Instruction::GetElementPtr { base: Value(2), offset: Operand::Const(IrConst::I64(1024)), .. },
            Instruction::GetElementPtr { base: Value(0), offset: Operand::Const(IrConst::I64(70000)), .. },
        ]), "{:?}", f.blocks[0].instructions);
    }
}
