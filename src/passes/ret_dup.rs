//! Give each predecessor of a small return block its own copy.
//!
//!   bb1: condbr %a, bb9, bb2                  bb1: condbr %a, bb10, bb2
//!   bb2: ...                                  bb2: ...
//!        condbr %b, bb9, bb3                       condbr %b, bb11, bb3
//!   bb3: %c = ...                             bb3: %c = ...
//!        br bb9                           =>       %r = cast I64->I32 %c
//!   bb9: %v = phi [1, bb1], [1, bb2], [%c, bb3]    ret %r
//!        %r = cast I64->I32 %v                bb10: ret 1
//!        ret %r                               bb11: ret 1
//!
//! An `a || b || c` returned from a function merges its early exits into one
//! block with a phi, so each exit sets a register and jumps there to extend
//! and return it. Each predecessor that passes constants into every phi gets
//! a copy of the block with the phis replaced by them, which folds to
//! `ret 1`. When one predecessor is left, the block merges into it.
//!
//! The block must have phis, at most MAX_INSTS pure instructions besides its
//! return, and at most MAX_PREDS predecessors (each copy also gets the
//! epilogue). Predecessors that branch to it from a switch, or on both edges
//! of a conditional branch, keep the shared block. Runs once, late, then
//! folds constants and cleans up the CFG.

use crate::common::fx_hash::FxHashSet;
use crate::common::source::Span;
use crate::ir::reexports::{BasicBlock, BlockId, Instruction, IrBinOp, IrFunction, IrModule, Operand, Terminator, Value};
use super::copy_prop::{replace_operands_in_instruction, replace_operands_in_terminator};

/// Most non-phi instructions in a duplicated return block.
const MAX_INSTS: usize = 2;
/// Most predecessors of a duplicated return block.
const MAX_PREDS: usize = 8;

/// Returns the number of block copies made; `changed[f]` is set for each
/// function that got any.
pub(crate) fn duplicate_returns(module: &mut IrModule, changed: &mut [bool]) -> usize {
    let mut next_label = module.functions.iter().flat_map(|f| &f.blocks).map(|b| b.label.0 + 1).max().unwrap_or(0);
    let mut total = 0;
    for (fi, func) in module.functions.iter_mut().enumerate() {
        if func.is_declaration {
            continue;
        }
        let n = duplicate_in_function(func, &mut next_label);
        if n > 0 {
            changed[fi] = true;
            total += n;
        }
    }
    total
}

fn is_pure(inst: &Instruction) -> bool {
    match inst {
        Instruction::BinOp { op, .. } => !matches!(op, IrBinOp::SDiv | IrBinOp::UDiv | IrBinOp::SRem | IrBinOp::URem),
        Instruction::Cast { .. } | Instruction::Copy { .. } | Instruction::UnaryOp { .. } | Instruction::Cmp { .. } => true,
        _ => false,
    }
}

fn duplicate_in_function(func: &mut IrFunction, next_label: &mut u32) -> usize {
    let mut made = 0;
    for ri in 1..func.blocks.len() {
        let r = &func.blocks[ri];
        if !matches!(r.terminator, Terminator::Return(_)) {
            continue;
        }
        let phis = r.instructions.iter().take_while(|i| matches!(i, Instruction::Phi { .. })).count();
        let body = &r.instructions[phis..];
        if phis == 0 || body.len() > MAX_INSTS || !body.iter().all(is_pure) {
            continue;
        }
        // The block's values may only be used inside it.
        let defined: FxHashSet<u32> = r.instructions.iter().filter_map(|i| i.dest()).map(|d| d.0).collect();
        let used_outside = func.blocks.iter().enumerate().filter(|&(bi, _)| bi != ri).any(|(_, b)| {
            b.instructions.iter().any(|i| i.used_values().iter().any(|v| defined.contains(v)))
                || b.terminator.used_values().iter().any(|v| defined.contains(v))
        });
        if used_outside {
            continue;
        }
        let label = r.label;
        let preds: Vec<usize> = func.blocks.iter().enumerate()
            .filter(|(_, b)| targets(&b.terminator).contains(&label))
            .map(|(bi, _)| bi)
            .collect();
        if preds.len() < 2 || preds.len() > MAX_PREDS {
            continue;
        }
        for p in preds {
            let t = targets(&func.blocks[p].terminator);
            let ok = match &func.blocks[p].terminator {
                Terminator::Branch(_) => true,
                Terminator::CondBranch { .. } => t.iter().filter(|&&l| l == label).count() == 1,
                _ => false,
            };
            let from = func.blocks[p].label;
            let constant = func.blocks[ri].instructions[..phis].iter().all(|i| matches!(i,
                Instruction::Phi { incoming, .. } if incoming.iter().any(|(op, l)| *l == from && matches!(op, Operand::Const(_)))));
            if !ok || !constant {
                continue;
            }
            let copy = copy_for(func, ri, from, BlockId(*next_label));
            *next_label += 1;
            let new_label = copy.label;
            match &mut func.blocks[p].terminator {
                Terminator::Branch(l) => *l = new_label,
                Terminator::CondBranch { true_label, false_label, .. } => {
                    if *true_label == label {
                        *true_label = new_label;
                    } else {
                        *false_label = new_label;
                    }
                }
                _ => unreachable!(),
            }
            for inst in &mut func.blocks[ri].instructions {
                if let Instruction::Phi { incoming, .. } = inst {
                    incoming.retain(|(_, l)| *l != from);
                }
            }
            func.blocks.push(copy);
            made += 1;
        }
    }
    made
}

fn targets(term: &Terminator) -> Vec<BlockId> {
    match term {
        Terminator::Branch(l) => vec![*l],
        Terminator::CondBranch { true_label, false_label, .. } => vec![*true_label, *false_label],
        Terminator::Switch { default, cases, .. } => {
            let mut v = vec![*default];
            v.extend(cases.iter().map(|(_, l)| *l));
            v
        }
        Terminator::IndirectBranch { possible_targets, .. } => possible_targets.clone(),
        _ => Vec::new(),
    }
}

/// Block `ri` as entered from `from`: each phi becomes the value that edge
/// carries, and the other instructions get fresh results.
fn copy_for(func: &mut IrFunction, ri: usize, from: BlockId, label: BlockId) -> BasicBlock {
    let mut map: Vec<Option<Operand>> = Vec::new();
    let set = |map: &mut Vec<Option<Operand>>, v: Value, op: Operand| {
        if map.len() <= v.0 as usize {
            map.resize(v.0 as usize + 1, None);
        }
        map[v.0 as usize] = Some(op);
    };
    let mut instructions = Vec::new();
    let src = func.blocks[ri].instructions.clone();
    for inst in src {
        if let Instruction::Phi { dest, incoming, .. } = &inst {
            let op = incoming.iter().find(|(_, l)| *l == from).map(|(op, _)| *op)
                .expect("a predecessor has an entry in every phi");
            set(&mut map, *dest, op);
            continue;
        }
        let mut inst = inst;
        replace_operands_in_instruction(&mut inst, &map);
        if let Some(d) = inst.dest() {
            let fresh = Value(func.next_value_id);
            func.next_value_id += 1;
            set_dest(&mut inst, fresh);
            set(&mut map, d, Operand::Value(fresh));
        }
        instructions.push(inst);
    }
    let mut terminator = func.blocks[ri].terminator.clone();
    replace_operands_in_terminator(&mut terminator, &map);
    let span = func.blocks[ri].source_spans.last().copied().unwrap_or_else(Span::dummy);
    let source_spans = if func.blocks[ri].source_spans.is_empty() { Vec::new() } else { vec![span; instructions.len()] };
    BasicBlock { label, instructions, terminator, source_spans }
}

fn set_dest(inst: &mut Instruction, to: Value) {
    match inst {
        Instruction::BinOp { dest, .. } | Instruction::Cast { dest, .. } | Instruction::Copy { dest, .. }
        | Instruction::UnaryOp { dest, .. } | Instruction::Cmp { dest, .. } => *dest = to,
        _ => unreachable!("only pure instructions are copied"),
    }
}
