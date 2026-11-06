//! Floating-point loop accumulators updated in place.
//!
//! Phi elimination leaves a sum in a loop as
//!
//!   %t = fma %a, %b, %sum
//!   ...
//!   %sum = copy %t
//!
//! and the register allocator, which sees one interval for %sum over the whole
//! loop, gives %t a register of its own. The copy then sits on the chain from
//! one iteration's sum to the next: an `fmov` costs cycles that a `mov` between
//! integer registers does not, so the ocean's six sums, four cycles an
//! iteration each, took six. Where %t has no other use, and nothing reads or
//! writes %sum between the two, %t's definition writes %sum and the copy goes.
//!
//! The IR is no longer SSA here, so a value with several definitions is fine.
//! This runs after the profile point, so that it does not change the blocks a
//! profile is matched against.

use crate::common::fx_hash::FxHashMap;
use crate::common::types::IrType;
use crate::ir::reexports::{Instruction, IrFunction, IrModule, Operand, Value};

pub(crate) fn coalesce_module(module: &mut IrModule) {
    for func in module.functions.iter_mut().filter(|f| !f.is_declaration) {
        coalesce_function(func);
    }
}

/// The destination of the instructions a float value comes from.
fn dest_mut(inst: &mut Instruction) -> Option<&mut Value> {
    match inst {
        Instruction::BinOp { dest, .. } | Instruction::UnaryOp { dest, .. } | Instruction::Load { dest, .. } | Instruction::Cast { dest, .. } => Some(dest),
        Instruction::Intrinsic { dest, .. } => dest.as_mut(),
        _ => None,
    }
}

fn coalesce_function(func: &mut IrFunction) -> usize {
    let mut uses: FxHashMap<u32, u32> = FxHashMap::default();
    let mut defs: FxHashMap<u32, u32> = FxHashMap::default();
    for block in &func.blocks {
        for inst in &block.instructions {
            inst.for_each_used_value(|v| *uses.entry(v).or_default() += 1);
            if let Some(d) = inst.dest() {
                *defs.entry(d.0).or_default() += 1;
            }
        }
        block.terminator.for_each_used_value(|v| *uses.entry(v).or_default() += 1);
    }
    let mut merged = 0;
    for block in &mut func.blocks {
        let mut i = 0;
        while i < block.instructions.len() {
            let Instruction::Copy { dest: sum, src: Operand::Value(t) } = block.instructions[i] else {
                i += 1;
                continue;
            };
            let only_use = uses.get(&t.0) == Some(&1) && defs.get(&t.0) == Some(&1);
            let def = block.instructions[..i].iter().rposition(|inst| inst.dest() == Some(t));
            let Some(def) = def.filter(|_| only_use && sum != t) else {
                i += 1;
                continue;
            };
            let is_float = matches!(block.instructions[def].result_type(), Some(IrType::F32 | IrType::F64));
            let clear = block.instructions[def + 1..i].iter().all(|inst| {
                let mut reads = false;
                inst.for_each_used_value(|v| reads |= v == sum.0);
                !reads && inst.dest() != Some(sum)
            });
            match dest_mut(&mut block.instructions[def]).filter(|_| is_float && clear) {
                Some(dest) => {
                    *dest = sum;
                    block.instructions.remove(i);
                    if i < block.source_spans.len() {
                        block.source_spans.remove(i);
                    }
                    merged += 1;
                }
                None => i += 1,
            }
        }
    }
    merged
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::ir::reexports::{BasicBlock, BlockId, IrBinOp, Terminator};

    fn add(dest: u32, lhs: u32, rhs: u32, ty: IrType) -> Instruction {
        Instruction::BinOp { dest: Value(dest), op: IrBinOp::Add, lhs: Operand::Value(Value(lhs)), rhs: Operand::Value(Value(rhs)), ty }
    }

    fn copy(dest: u32, src: u32) -> Instruction {
        Instruction::Copy { dest: Value(dest), src: Operand::Value(Value(src)) }
    }

    fn run(instructions: Vec<Instruction>, terminator: Terminator) -> (usize, IrFunction) {
        let mut f = IrFunction::new("f".to_string(), IrType::Void, vec![], false);
        f.blocks.push(BasicBlock { label: BlockId(0), instructions, terminator, source_spans: vec![] });
        (coalesce_function(&mut f), f)
    }

    #[test]
    fn test_a_sum_is_updated_in_place() {
        // %2 = %1 + %9; %8 = %9 + %9; %1 = copy %2
        let (n, f) = run(vec![add(2, 1, 9, IrType::F32), add(8, 9, 9, IrType::F32), copy(1, 2)], Terminator::Return(None));
        assert_eq!(n, 1);
        assert_eq!(f.blocks[0].instructions.len(), 2);
        assert_eq!(f.blocks[0].instructions[0].dest(), Some(Value(1)));
    }

    #[test]
    fn test_a_sum_read_after_the_new_value_keeps_its_copy() {
        // The old sum is still wanted between the two.
        let (n, _) = run(vec![add(2, 1, 9, IrType::F64), add(8, 1, 9, IrType::F64), copy(1, 2)], Terminator::Return(None));
        assert_eq!(n, 0);
    }

    #[test]
    fn test_a_sum_written_in_between_keeps_its_copy() {
        let (n, _) = run(vec![add(2, 3, 9, IrType::F64), add(1, 9, 9, IrType::F64), copy(1, 2)], Terminator::Return(None));
        assert_eq!(n, 0);
    }

    #[test]
    fn test_a_new_value_used_elsewhere_keeps_its_copy() {
        let (n, _) = run(vec![add(2, 1, 9, IrType::F32), copy(1, 2), add(8, 2, 9, IrType::F32)], Terminator::Return(None));
        assert_eq!(n, 0);
        let (n, _) = run(vec![add(2, 1, 9, IrType::F32), copy(1, 2)], Terminator::Return(Some(Operand::Value(Value(2)))));
        assert_eq!(n, 0);
    }

    #[test]
    fn test_integers_and_other_blocks_are_left_alone() {
        let (n, _) = run(vec![add(2, 1, 9, IrType::I64), copy(1, 2)], Terminator::Return(None));
        assert_eq!(n, 0);
        let (n, _) = run(vec![copy(1, 2)], Terminator::Return(None));
        assert_eq!(n, 0);
    }
}
