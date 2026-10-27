//! Compare at the merge a flag that each predecessor computed with the
//! same comparison and the merge only branches on.
//!
//!   a:  %c1 = cmp eq %x1, %y1             a:  br m
//!       br m
//!   b:  %c2 = cmp eq %x2, %y2       =>    b:  br m
//!       br m
//!   m:  %f = phi [%c1, a], [%c2, b]       m:  %x = phi [%x1, a], [%x2, b]
//!       condbr %f, t, e                       %y = phi [%y1, a], [%y2, b]
//!                                             %f = cmp eq %x, %y
//!                                             condbr %f, t, e
//!
//! deflate_fast's `bflush`, set by `_tr_tally_lit` on the literal path and
//! by `_tr_tally_dist` on the match path and tested once both meet, was
//! materialized with `cset` on each path and tested with `cbz`; compared
//! at the merge, the compare feeds the branch. An operand the predecessors
//! share (the same value or constant) needs no phi.

use crate::common::types::IrType;
use crate::ir::reexports::{Instruction, IrCmpOp, IrFunction, Operand, Terminator, Value};

pub(crate) fn sink_compares(func: &mut IrFunction) -> usize {
    let max_id = func.max_value_id() as usize;
    let mut uses = vec![0u32; max_id + 1];
    let mut def: Vec<Option<(usize, usize)>> = vec![None; max_id + 1];
    for (b, block) in func.blocks.iter().enumerate() {
        for (i, inst) in block.instructions.iter().enumerate() {
            inst.for_each_used_value(|v| if let Some(u) = uses.get_mut(v as usize) { *u += 1 });
            if let Some(d) = inst.dest() {
                if let Some(slot) = def.get_mut(d.0 as usize) {
                    *slot = Some((b, i));
                }
            }
        }
        block.terminator.for_each_used_value(|v| if let Some(u) = uses.get_mut(v as usize) { *u += 1 });
    }
    let mut next_id = func.next_value_id.max(func.max_value_id() + 1);
    let mut sunk = 0;
    for m in 0..func.blocks.len() {
        let Terminator::CondBranch { cond: Operand::Value(f), .. } = func.blocks[m].terminator else { continue };
        if uses.get(f.0 as usize) != Some(&1) {
            continue;
        }
        let Some(pi) = func.blocks[m].instructions.iter().position(|inst| matches!(inst, Instruction::Phi { dest, .. } if *dest == f)) else { continue };
        let Instruction::Phi { incoming, .. } = &func.blocks[m].instructions[pi] else { unreachable!() };
        // Each incoming value: a compare read only by this phi.
        let mut shape: Option<(IrCmpOp, IrType)> = None;
        let mut sides = Vec::new();
        for (op, pred) in incoming {
            let Operand::Value(c) = op else { break };
            let n = incoming.iter().filter(|(o, _)| matches!(o, Operand::Value(x) if x == c)).count() as u32;
            let Some(Some((cb, ci))) = def.get(c.0 as usize) else { break };
            let Instruction::Cmp { op, lhs, rhs, ty, .. } = &func.blocks[*cb].instructions[*ci] else { break };
            if uses[c.0 as usize] != n || !ty.is_integer() || shape.is_some_and(|s| s != (*op, *ty)) {
                break;
            }
            shape = Some((*op, *ty));
            sides.push((*lhs, *rhs, *pred));
        }
        let Some((op, ty)) = shape else { continue };
        if sides.len() != incoming.len() {
            continue;
        }
        let mut new_phis = Vec::new();
        let mut merged = |pick: fn(&(Operand, Operand, crate::ir::reexports::BlockId)) -> &Operand| {
            let first = pick(&sides[0]);
            if sides.iter().all(|s| same(pick(s), first)) {
                return *first;
            }
            let dest = Value(next_id);
            next_id += 1;
            new_phis.push(Instruction::Phi { dest, ty, incoming: sides.iter().map(|s| (*pick(s), s.2)).collect() });
            Operand::Value(dest)
        };
        let lhs = merged(|s| &s.0);
        let rhs = merged(|s| &s.1);
        let block = &mut func.blocks[m];
        // The compare goes after the block's other phis, the new phis
        // before it.
        let nphis = block.instructions.iter().take_while(|i| matches!(i, Instruction::Phi { .. })).count();
        block.instructions.remove(pi);
        let cmp = Instruction::Cmp { dest: f, op, lhs, rhs, ty };
        let at = nphis - 1;
        let added = new_phis.len();
        block.instructions.splice(at..at, new_phis.into_iter().chain(std::iter::once(cmp)));
        if !block.source_spans.is_empty() {
            let span = block.source_spans.remove(pi);
            block.source_spans.splice(at..at, std::iter::repeat_n(span, added + 1));
        }
        sunk += 1;
    }
    func.next_value_id = next_id;
    sunk
}

fn same(a: &Operand, b: &Operand) -> bool {
    match (a, b) {
        (Operand::Value(x), Operand::Value(y)) => x == y,
        (Operand::Const(x), Operand::Const(y)) => x.to_i64().is_some() && x.to_i64() == y.to_i64() && std::mem::discriminant(x) == std::mem::discriminant(y),
        _ => false,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::ir::reexports::{BasicBlock, BlockId, IrConst};

    /// `f = sel ? a == b : c == 5` branched on at the merge; the second
    /// compare also returned when `other_use`.
    fn flag(other_use: bool) -> IrFunction {
        let v = |n| Operand::Value(Value(n));
        let cmp = |dest, lhs, rhs| Instruction::Cmp { dest: Value(dest), op: IrCmpOp::Eq, lhs, rhs, ty: IrType::I32 };
        let mut func = IrFunction::new("test".to_string(), IrType::I32, vec![], false);
        let ret = if other_use { Some(v(11)) } else { Some(Operand::Const(IrConst::I32(1))) };
        func.blocks = vec![
            BasicBlock { label: BlockId(0), instructions: vec![], terminator: Terminator::CondBranch { cond: v(0), true_label: BlockId(1), false_label: BlockId(2) }, source_spans: Vec::new() },
            BasicBlock { label: BlockId(1), instructions: vec![cmp(10, v(1), v(2))], terminator: Terminator::Branch(BlockId(3)), source_spans: Vec::new() },
            BasicBlock { label: BlockId(2), instructions: vec![cmp(11, v(3), Operand::Const(IrConst::I32(5)))], terminator: Terminator::Branch(BlockId(3)), source_spans: Vec::new() },
            BasicBlock { label: BlockId(3), instructions: vec![
                Instruction::Phi { dest: Value(12), ty: IrType::I8, incoming: vec![(v(10), BlockId(1)), (v(11), BlockId(2))] },
            ], terminator: Terminator::CondBranch { cond: v(12), true_label: BlockId(4), false_label: BlockId(5) }, source_spans: Vec::new() },
            BasicBlock { label: BlockId(4), instructions: vec![], terminator: Terminator::Return(ret), source_spans: Vec::new() },
            BasicBlock { label: BlockId(5), instructions: vec![], terminator: Terminator::Return(Some(Operand::Const(IrConst::I32(0)))), source_spans: Vec::new() },
        ];
        func.next_value_id = 13;
        func
    }

    #[test]
    fn flag_compared_where_the_paths_meet() {
        let mut func = flag(false);
        assert_eq!(sink_compares(&mut func), 1);
        let [Instruction::Phi { dest: x, incoming: xs, .. }, Instruction::Phi { dest: y, incoming: ys, .. },
             Instruction::Cmp { dest: Value(12), op: IrCmpOp::Eq, lhs: Operand::Value(l), rhs: Operand::Value(r), ty: IrType::I32 }] =
            &func.blocks[3].instructions[..] else { panic!("{:?}", func.blocks[3].instructions) };
        assert_eq!((x, y), (l, r));
        assert!(matches!(xs[..], [(Operand::Value(Value(1)), BlockId(1)), (Operand::Value(Value(3)), BlockId(2))]));
        assert!(matches!(ys[..], [(Operand::Value(Value(2)), BlockId(1)), (Operand::Const(IrConst::I32(5)), BlockId(2))]));
        // A compare read elsewhere too stays where it is.
        let mut func = flag(true);
        assert_eq!(sink_compares(&mut func), 0);
    }
}
