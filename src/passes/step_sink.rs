//! Move a loop counter's step below the counter's last use in its block.
//!
//!   loop: %i = phi [0, pre], [%n, latch]      loop: %i = phi [0, pre], [%n, latch]
//!         %n = add %i, 1                  =>        %x = load [p, %i]
//!         %x = load [p, %i]                         %n = add %i, 1
//!
//! `p[i++]` steps the counter before the old value is read, so both are
//! live at once: the copy phi elimination puts on the back edge needs its
//! own register and, when the latch branches conditionally, its own block
//! (a move and a jump every iteration). After the move the step can take
//! the counter's register and the copy disappears.
//!
//! Applies to a phi's incoming value that is an add or subtract of the phi
//! and a constant in the phi's block, when every other use of the phi is an
//! instruction of that block (not a phi or the terminator). The step goes
//! as late as possible: just before its first read in the block, or at the
//! end (before a compare the branch tests), which must be after the phi's
//! last use. (Right after that use, an
//! address computed from the counter could not fold into its access.)

use crate::ir::reexports::{Instruction, IrBinOp, IrFunction, Operand, Value};

pub(crate) fn sink_steps(func: &mut IrFunction) -> usize {
    let max_id = func.max_value_id() as usize;
    // Uses of each value: (block, instruction index), None for a terminator.
    let mut uses: Vec<Vec<Option<(usize, usize)>>> = vec![Vec::new(); max_id + 1];
    for (b, block) in func.blocks.iter().enumerate() {
        for (i, inst) in block.instructions.iter().enumerate() {
            inst.for_each_used_value(|v| uses[v as usize].push(Some((b, i))));
        }
        block.terminator.for_each_used_value(|v| uses[v as usize].push(None));
    }
    let mut moved = 0;
    for b in 0..func.blocks.len() {
        // (phi, step) pairs in this block.
        let mut pairs: Vec<(Value, Value)> = Vec::new();
        for inst in &func.blocks[b].instructions {
            let Instruction::Phi { dest, incoming, .. } = inst else { continue };
            for (op, _) in incoming {
                if let Operand::Value(s) = op {
                    pairs.push((*dest, *s));
                }
            }
        }
        for (phi, step) in pairs {
            let insts = &func.blocks[b].instructions;
            let Some(at) = insts.iter().position(|i| match i {
                Instruction::BinOp { dest, op: IrBinOp::Add | IrBinOp::Sub, lhs: Operand::Value(l), rhs: Operand::Const(_), .. } => *dest == step && *l == phi,
                Instruction::BinOp { dest, op: IrBinOp::Add, lhs: Operand::Const(_), rhs: Operand::Value(r), .. } => *dest == step && *r == phi,
                _ => false,
            }) else { continue };
            // Every other use of the phi: a non-phi instruction of this block.
            let mut last = None;
            let mut ok = true;
            for u in &uses[phi.0 as usize] {
                match *u {
                    Some((ub, ui)) if ub == b && !matches!(insts[ui], Instruction::Phi { .. }) => {
                        if ui != at {
                            last = last.max(Some(ui));
                        }
                    }
                    _ => ok = false,
                }
            }
            let Some(last_use) = last.filter(|&l| ok && l > at) else { continue };
            // As late as the step's own first read in the block allows (an
            // address computed from the counter then folds into its access).
            // Not between the block's last compare and the branch on it.
            let n = insts.len();
            let end = match (insts.last(), &func.blocks[b].terminator) {
                (Some(Instruction::Cmp { dest, .. }), crate::ir::reexports::Terminator::CondBranch { cond: Operand::Value(c), .. })
                    if dest == c => n - 1,
                _ => n,
            };
            let first_read = uses[step.0 as usize].iter()
                .filter_map(|u| u.filter(|&(ub, ui)| ub == b && ui > at).map(|(_, ui)| ui))
                .min()
                .unwrap_or(end)
                .min(end);
            if first_read <= last_use {
                continue;
            }
            let last = first_read - 1;
            let block = &mut func.blocks[b];
            let spans = block.source_spans.len() == block.instructions.len();
            let inst = block.instructions.remove(at);
            block.instructions.insert(last, inst);
            if spans {
                let span = block.source_spans.remove(at);
                block.source_spans.insert(last, span);
            }
            // Positions between moved up by one.
            for list in uses.iter_mut() {
                for u in list.iter_mut().flatten() {
                    if u.0 == b {
                        if u.1 == at {
                            u.1 = last;
                        } else if u.1 > at && u.1 <= last {
                            u.1 -= 1;
                        }
                    }
                }
            }
            moved += 1;
        }
    }
    moved
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::common::types::{AddressSpace, IrType};
    use crate::ir::reexports::{BasicBlock, BlockId, IrCmpOp, IrConst, Terminator};

    /// `do s += p[i++]; while (i != n)`: the step first, then the index.
    fn scan(extra_use: bool) -> IrFunction {
        let v = |n| Operand::Value(Value(n));
        let mut body = vec![
            Instruction::Phi { dest: Value(10), ty: IrType::I64, incoming: vec![(Operand::Const(IrConst::I64(0)), BlockId(0)), (v(11), BlockId(1))] },
            Instruction::BinOp { dest: Value(11), op: IrBinOp::Add, lhs: v(10), rhs: Operand::Const(IrConst::I64(1)), ty: IrType::I64 },
            Instruction::GetElementPtr { dest: Value(12), base: Value(0), offset: v(10), ty: IrType::Ptr },
            Instruction::Load { dest: Value(13), ptr: Value(12), ty: IrType::U8, seg_override: AddressSpace::Default, volatile: false },
            Instruction::Cmp { dest: Value(14), op: IrCmpOp::Ne, lhs: v(11), rhs: v(1), ty: IrType::I64 },
        ];
        if extra_use {
            body.insert(2, Instruction::BinOp { dest: Value(15), op: IrBinOp::Mul, lhs: v(11), rhs: v(1), ty: IrType::I64 });
        }
        let mut func = IrFunction::new("test".to_string(), IrType::I32, vec![], false);
        func.blocks = vec![
            BasicBlock { label: BlockId(0), instructions: vec![], terminator: Terminator::Branch(BlockId(1)), source_spans: Vec::new() },
            BasicBlock { label: BlockId(1), instructions: body, terminator: Terminator::CondBranch { cond: v(14), true_label: BlockId(1), false_label: BlockId(2) }, source_spans: Vec::new() },
            BasicBlock { label: BlockId(2), instructions: vec![], terminator: Terminator::Return(None), source_spans: Vec::new() },
        ];
        func
    }

    #[test]
    fn step_moves_after_the_counters_last_use() {
        let mut func = scan(false);
        assert_eq!(sink_steps(&mut func), 1);
        // After the load (the address folds into it), before the compare.
        assert!(matches!(func.blocks[1].instructions[..], [
            Instruction::Phi { .. }, Instruction::GetElementPtr { .. }, Instruction::Load { .. },
            Instruction::BinOp { dest: Value(11), .. }, Instruction::Cmp { .. },
        ]), "{:?}", func.blocks[1].instructions);
        // Read before the counter's last use: it stays.
        let mut func = scan(true);
        assert_eq!(sink_steps(&mut func), 0);
    }
}
