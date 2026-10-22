//! Hoist an instruction to where its operands are defined when that ends
//! two or more live ranges early.
//!
//!   bb0: %promo = ...                         bb0: %promo = ...
//!        %type = ...                               %type = ...
//!        condbr ...                                %pt = select %promo, %promo, %type
//!   ...                                   =>       condbr ...
//!   bb4: %pt = select %promo, %promo, %type   ...
//!                                             bb4: (uses %pt)
//!
//! When an instruction is the only use of two or more values, all of them
//! stay live from their definitions down to it, while its own result would
//! need one register. Moving it to the end of the block that defines its
//! last operand shortens those live ranges, and since the instruction's
//! block post-dominates that one (and the two are not in a loop together),
//! it still runs exactly once per run of that block.
//!
//! Only pure instructions that cannot trap move. Runs once, late.

use crate::common::fx_hash::{FxHashMap, FxHashSet};
use crate::common::source::Span;
use crate::ir::analysis::CfgAnalysis;
use crate::ir::reexports::{Instruction, IrBinOp, IrFunction, Operand, Terminator};

pub(crate) fn hoist_function(func: &mut IrFunction) -> usize {
    if func.blocks.len() < 2 {
        return 0;
    }
    let cfg = CfgAnalysis::build(func);

    // Defining block of each value, and how many instructions or
    // terminators use it.
    let mut def_block: FxHashMap<u32, usize> = FxHashMap::default();
    let mut users: FxHashMap<u32, u32> = FxHashMap::default();
    for (bi, block) in func.blocks.iter().enumerate() {
        for inst in &block.instructions {
            if let Some(d) = inst.dest() {
                def_block.insert(d.0, bi);
            }
            let mut seen: FxHashSet<u32> = FxHashSet::default();
            inst.for_each_used_value(|v| {
                if seen.insert(v) {
                    *users.entry(v).or_insert(0) += 1;
                }
            });
        }
        block.terminator.for_each_used_value(|v| *users.entry(v).or_insert(0) += 1);
    }

    // Moves: (from block, instruction index, to block).
    let mut moves: Vec<(usize, usize, usize)> = Vec::new();
    for (bi, block) in func.blocks.iter().enumerate() {
        for (ii, inst) in block.instructions.iter().enumerate() {
            if !is_hoistable(inst) {
                continue;
            }
            let mut operands: Vec<u32> = Vec::new();
            inst.for_each_used_value(|v| if !operands.contains(&v) { operands.push(v) });
            let dying = operands.iter().filter(|v| users.get(v) == Some(&1)).count();
            if dying < 2 {
                continue;
            }
            // The operands' definitions all dominate this block, so they lie
            // on its dominator chain; the target is the deepest of them.
            let Some(target) = deepest_on_dom_chain(&cfg, bi, operands.iter().map(|v| def_block.get(v).copied().unwrap_or(0)))
            else { continue };
            if target == bi || !runs_once_after(&cfg, func, target, bi) {
                continue;
            }
            moves.push((bi, ii, target));
        }
    }

    // `moves` is in program order; remove from the back so indices stay
    // valid, then append to the targets in program order.
    let mut hoisted: Vec<(usize, Instruction, Span)> = Vec::new();
    for &(from, idx, to) in moves.iter().rev() {
        let block = &mut func.blocks[from];
        let inst = block.instructions.remove(idx);
        let span = if block.source_spans.is_empty() { Span::dummy() } else { block.source_spans.remove(idx) };
        hoisted.push((to, inst, span));
    }
    let n = hoisted.len();
    for (to, inst, span) in hoisted.into_iter().rev() {
        let block = &mut func.blocks[to];
        // Before a final compare that the branch tests, so the two stay
        // adjacent (cmp + b.cc).
        let mut at = block.instructions.len();
        if let (Some(Instruction::Cmp { dest, .. }), Terminator::CondBranch { cond: Operand::Value(c), .. }) =
            (block.instructions.last(), &block.terminator)
        {
            if dest == c {
                at -= 1;
            }
        }
        block.instructions.insert(at, inst);
        // source_spans is either empty or parallel to the instructions.
        if !block.source_spans.is_empty() {
            block.source_spans.insert(at, span);
        }
    }
    n
}

fn is_hoistable(inst: &Instruction) -> bool {
    match inst {
        Instruction::BinOp { op, .. } => !matches!(op, IrBinOp::SDiv | IrBinOp::UDiv | IrBinOp::SRem | IrBinOp::URem),
        Instruction::UnaryOp { .. } | Instruction::Cast { .. } | Instruction::Select { .. }
        | Instruction::GetElementPtr { .. } => true,
        _ => false,
    }
}

/// The deepest of `blocks` in the dominator tree, if all of them dominate
/// `bi` (walking up from `bi`).
fn deepest_on_dom_chain(cfg: &CfgAnalysis, bi: usize, blocks: impl Iterator<Item = usize>) -> Option<usize> {
    let wanted: FxHashSet<usize> = blocks.collect();
    let mut cur = bi;
    let mut found = 0;
    let mut deepest = None;
    loop {
        if wanted.contains(&cur) {
            deepest.get_or_insert(cur);
            found += 1;
        }
        if found == wanted.len() {
            return deepest;
        }
        let up = cfg.idom[cur];
        if up == cur || up == usize::MAX {
            return None;
        }
        cur = up;
    }
}

/// Whether every path from `target` reaches `bi`, without passing `target`
/// again, and `bi` does not lead back to `target`: then `bi` runs exactly
/// once for each run of `target`.
fn runs_once_after(cfg: &CfgAnalysis, func: &IrFunction, target: usize, bi: usize) -> bool {
    let mut seen = vec![false; cfg.num_blocks];
    let mut stack: Vec<usize> = cfg.succs.row(target).iter().map(|&s| s as usize).collect();
    while let Some(b) = stack.pop() {
        if b == bi || seen[b] {
            continue;
        }
        if b == target || cfg.succs.len(b) == 0
            || matches!(func.blocks[b].terminator, Terminator::Return(_) | Terminator::Unreachable) {
            return false;
        }
        seen[b] = true;
        stack.extend(cfg.succs.row(b).iter().map(|&s| s as usize));
    }
    // bi must not reach target.
    let mut seen = vec![false; cfg.num_blocks];
    let mut stack = vec![bi];
    while let Some(b) = stack.pop() {
        if seen[b] {
            continue;
        }
        if b == target {
            return false;
        }
        seen[b] = true;
        stack.extend(cfg.succs.row(b).iter().map(|&s| s as usize));
    }
    true
}
