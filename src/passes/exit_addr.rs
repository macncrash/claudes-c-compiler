//! Recompute after a loop the element addresses it also uses inside.
//!
//!   loop:  %o = Shl %i, 2                    loop:  %o = Shl %i, 2
//!          %a = gep %p, %o                          %a = gep %p, %o
//!          %x = load %a                             %x = load %a
//!          condbr %c, loop, exit       =>           condbr %c, loop, exit
//!   exit:  store %y -> %a                    exit:  %o2 = Shl %i, 2
//!                                                   %a2 = gep %p, %o2
//!                                                   store %y -> %a2
//!
//! The backend folds an address into the access that uses it
//! (`ldr w0, [x1, x2, lsl #2]`) only when that access is its last use. A
//! scan that stops at an element and then writes it (a partition's
//! `do i++; while (a[i] < p)` before the swap) keeps the address live out
//! of the loop, so every iteration computed it in a register of its own.
//! Recomputing it where the loop is left costs an instruction or two once
//! per exit instead.
//!
//! Only addresses whose every use in their innermost loop is a load or store
//! the backend can fold (index scaled by the access size, or not at all) are
//! split, and only when nothing outside the loop reads them through a phi.
//! Runs late, after the passes that would merge the copies again (AArch64).

use crate::common::fx_hash::{FxHashMap, FxHashSet};
use crate::common::types::{AddressSpace, IrType};
use crate::ir::analysis;
use crate::ir::reexports::{Instruction, IrBinOp, IrConst, IrFunction, Operand, Value};
use super::copy_prop::{replace_operands_in_instruction, replace_operands_in_terminator};
use super::loop_analysis;

pub(crate) fn split_function(func: &mut IrFunction) -> usize {
    if func.blocks.len() < 2 {
        return 0;
    }
    let cfg = analysis::CfgAnalysis::build(func);
    let loops = loop_analysis::merge_loops_by_header(
        loop_analysis::find_natural_loops(cfg.num_blocks, &cfg.preds, &cfg.succs, &cfg.idom));
    if loops.is_empty() {
        return 0;
    }
    // The innermost loop of each block.
    let mut innermost: Vec<Option<usize>> = vec![None; func.blocks.len()];
    for (l, lp) in loops.iter().enumerate() {
        for &b in &lp.body {
            if innermost[b].is_none_or(|m| loops[m].body.len() > lp.body.len()) {
                innermost[b] = Some(l);
            }
        }
    }

    // Each value's uses: (block, instruction index, or None for the
    // terminator), and the values read by some phi.
    let mut uses: FxHashMap<u32, Vec<(usize, Option<usize>)>> = FxHashMap::default();
    let mut phi_read: FxHashSet<u32> = FxHashSet::default();
    let mut shl: FxHashMap<u32, (Operand, i64)> = FxHashMap::default();
    for (b, block) in func.blocks.iter().enumerate() {
        for (k, inst) in block.instructions.iter().enumerate() {
            if matches!(inst, Instruction::Phi { .. }) {
                inst.for_each_used_value(|v| { phi_read.insert(v); });
            } else {
                inst.for_each_used_value(|v| uses.entry(v).or_default().push((b, Some(k))));
            }
            if let Instruction::BinOp { dest, op: IrBinOp::Shl, lhs, rhs: Operand::Const(c), ty: IrType::I64 | IrType::U64 } = inst {
                if let Some(k) = c.to_i64() {
                    shl.insert(dest.0, (*lhs, k));
                }
            }
        }
        block.terminator.for_each_used_value(|v| uses.entry(v).or_default().push((b, None)));
    }

    // Clones to insert: (block, before index, instructions), and per block
    // the address each clone replaces.
    let mut inserts: Vec<(usize, usize, Vec<Instruction>)> = Vec::new();
    let mut renames: FxHashMap<usize, Vec<(Value, Value)>> = FxHashMap::default();
    let mut next_id = func.next_value_id.max(func.max_value_id() + 1);
    for (b, block) in func.blocks.iter().enumerate() {
        let Some(l) = innermost[b] else { continue };
        let body = &loops[l].body;
        for inst in &block.instructions {
            let Instruction::GetElementPtr { dest, base, offset: Operand::Value(off), ty } = *inst else { continue };
            if phi_read.contains(&dest.0) {
                continue;
            }
            let Some(all) = uses.get(&dest.0) else { continue };
            // The index's scale, if it is a shift used only here.
            let scaled = shl.get(&off.0).filter(|_| uses.get(&off.0).is_some_and(|u| u.len() == 1) && !phi_read.contains(&off.0));
            let scale = scaled.map_or(0, |&(_, k)| k);
            let foldable = |&(ub, uk): &(usize, Option<usize>)| {
                let Some(uk) = uk else { return false };
                let (ty, ptr) = match &func.blocks[ub].instructions[uk] {
                    Instruction::Load { ty, ptr, seg_override: AddressSpace::Default, .. } => (*ty, *ptr),
                    Instruction::Store { ty, ptr, val, seg_override: AddressSpace::Default, .. }
                        if !matches!(val, Operand::Value(v) if *v == dest) => (*ty, *ptr),
                    _ => return false,
                };
                ptr == dest && !ty.is_long_double() && !ty.is_128bit()
                    && (scale == 0 || 1i64 << scale == ty.size() as i64)
            };
            let (inside, outside): (Vec<&(usize, Option<usize>)>, Vec<_>) = all.iter().partition(|(ub, _)| body.contains(ub));
            if outside.is_empty() || inside.is_empty() || !inside.iter().all(|&u| foldable(u)) {
                continue;
            }
            // One copy per block, before its first use there.
            let mut first: FxHashMap<usize, usize> = FxHashMap::default();
            for &&(ub, uk) in &outside {
                let at = uk.unwrap_or(func.blocks[ub].instructions.len());
                let e = first.entry(ub).or_insert(at);
                *e = (*e).min(at);
            }
            for (ub, at) in first {
                let mut insts = Vec::with_capacity(2);
                let offset = match scaled {
                    Some(&(x, k)) => {
                        let o = Value(next_id);
                        next_id += 1;
                        insts.push(Instruction::BinOp { dest: o, op: IrBinOp::Shl, lhs: x, rhs: Operand::Const(IrConst::I64(k)), ty: IrType::I64 });
                        Operand::Value(o)
                    }
                    None => Operand::Value(off),
                };
                let a = Value(next_id);
                next_id += 1;
                insts.push(Instruction::GetElementPtr { dest: a, base, offset, ty });
                inserts.push((ub, at, insts));
                renames.entry(ub).or_default().push((dest, a));
            }
        }
    }
    func.next_value_id = next_id;
    if inserts.is_empty() {
        return 0;
    }

    for (b, pairs) in renames {
        let mut map: Vec<Option<Operand>> = vec![None; pairs.iter().map(|(v, _)| v.0 as usize + 1).max().unwrap_or(0)];
        for (v, a) in pairs {
            map[v.0 as usize] = Some(Operand::Value(a));
        }
        let block = &mut func.blocks[b];
        for inst in &mut block.instructions {
            replace_operands_in_instruction(inst, &map);
        }
        replace_operands_in_terminator(&mut block.terminator, &map);
    }
    // Last position first, so earlier ones stay put.
    inserts.sort_by(|x, y| (x.0, y.1).cmp(&(y.0, x.1)));
    let count = inserts.len();
    for (b, at, insts) in inserts {
        let block = &mut func.blocks[b];
        if !block.source_spans.is_empty() {
            let span = block.source_spans[at.min(block.source_spans.len() - 1)];
            block.source_spans.splice(at..at, std::iter::repeat_n(span, insts.len()));
        }
        block.instructions.splice(at..at, insts);
    }
    count
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::ir::reexports::{BasicBlock, BlockId, IrCmpOp, Terminator};

    /// `do i++; while (a[i] < k); a[i] = k;` with the index scaled by `shift`.
    fn scan_then_store(shift: i64) -> IrFunction {
        let mut func = IrFunction::new("test".to_string(), IrType::Void, vec![], false);
        let block = |l, instructions, terminator| BasicBlock { label: BlockId(l), instructions, terminator, source_spans: Vec::new() };
        let v = |n| Operand::Value(Value(n));
        func.blocks = vec![
            block(0, vec![], Terminator::Branch(BlockId(1))),
            block(1, vec![
                Instruction::Phi { dest: Value(10), ty: IrType::I64, incoming: vec![(Operand::Const(IrConst::I64(-1)), BlockId(0)), (v(11), BlockId(1))] },
                Instruction::BinOp { dest: Value(11), op: IrBinOp::Add, lhs: v(10), rhs: Operand::Const(IrConst::I64(1)), ty: IrType::I64 },
                Instruction::BinOp { dest: Value(12), op: IrBinOp::Shl, lhs: v(11), rhs: Operand::Const(IrConst::I64(shift)), ty: IrType::I64 },
                Instruction::GetElementPtr { dest: Value(13), base: Value(0), offset: v(12), ty: IrType::Ptr },
                Instruction::Load { dest: Value(14), ptr: Value(13), ty: IrType::I32, seg_override: AddressSpace::Default, volatile: false },
                Instruction::Cmp { dest: Value(15), op: IrCmpOp::Slt, lhs: v(14), rhs: v(1), ty: IrType::I32 },
            ], Terminator::CondBranch { cond: v(15), true_label: BlockId(1), false_label: BlockId(2) }),
            block(2, vec![
                Instruction::Store { val: v(1), ptr: Value(13), ty: IrType::I32, seg_override: AddressSpace::Default, volatile: false },
            ], Terminator::Return(None)),
        ];
        func.next_value_id = 16;
        func
    }

    #[test]
    fn address_recomputed_after_the_loop() {
        let mut func = scan_then_store(2);
        assert_eq!(split_function(&mut func), 1);
        // The loop's address now only feeds its load.
        assert_eq!(func.blocks[1].instructions.len(), 6);
        let [Instruction::BinOp { dest: o, op: IrBinOp::Shl, lhs: Operand::Value(Value(11)), .. },
             Instruction::GetElementPtr { dest: a, base: Value(0), offset: Operand::Value(off), .. },
             Instruction::Store { ptr, .. }] = func.blocks[2].instructions[..]
        else { panic!("{:?}", func.blocks[2].instructions) };
        assert!(off == o && ptr == a);
    }

    #[test]
    fn address_kept_when_the_scale_does_not_fold() {
        // An 8-byte stride for a 4-byte load: no `lsl #3` form to fold into.
        let mut func = scan_then_store(3);
        assert_eq!(split_function(&mut func), 0);
    }
}
