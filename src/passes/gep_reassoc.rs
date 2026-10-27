//! Move constant GEP offsets past variable ones, into the memory accesses.
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
//! the backend folds) are rewritten. Runs once, after the main pipeline, so
//! that GVN and LICM see the original form.

use crate::common::fx_hash::{FxHashMap, FxHashSet};
use crate::common::types::{AddressSpace, IrType};
use crate::ir::analysis::CfgAnalysis;
use crate::ir::reexports::{Instruction, IrBinOp, IrConst, IrFunction, Operand, Value};

pub(crate) fn reassociate_function(func: &mut IrFunction) -> usize {
    let split = split_index_constants(func);
    reassociate_constant_bases(func) + split
}

fn reassociate_constant_bases(func: &mut IrFunction) -> usize {
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
    for block in &mut func.blocks {
        let mut i = 0;
        while i < block.instructions.len() {
            if let Instruction::GetElementPtr { dest, base, offset: index @ Operand::Value(_), ty } = block.instructions[i] {
                if let Some(&(root, offset)) = const_gep.get(&base.0) {
                    if !other_use.contains(&dest.0) {
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
}
