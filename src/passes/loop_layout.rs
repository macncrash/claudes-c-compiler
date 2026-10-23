//! Loop layout: put the end of a loop before its header.
//!
//! A bottom-tested loop laid out header first ends with its latch's
//! conditional branch back to the header, taken every iteration. Often the
//! blocks at the end of the loop are reached only by jumps anyway (a
//! `continue`, or an `x ? a : b` arm whose other side jumps past it). The run
//! of blocks ending at the latch whose first block nothing falls into can
//! then move in front of the header, so the latch falls into the header and
//! exits by a jump instead:
//!
//!   header:  ...                              (preheader: b header)
//!            cbnz w0, latch               tail:    mov x10, #1
//!            ...; b add                   add:     add x21, x21, x10
//!   tail:    mov x10, #1             =>   latch:   ...; b.ge exit
//!   add:     add x21, x21, x10            header:  ...
//!   latch:   ...; b.lt header                      cbnz w0, latch
//!   exit:                                          ...; b add
//!
//! Every path through the loop then takes one jump less per iteration; the
//! loop's entry and exit take one more each, once per loop. Runs last,
//! after phi elimination and loop rotation. The moved blocks must all be in
//! the loop; others of its blocks (a trampoline at the end of the function)
//! may lie elsewhere.

use crate::ir::analysis;
use crate::ir::reexports::{BasicBlock, BlockId, IrFunction, IrModule, Terminator};
use super::loop_analysis;

/// Most loops rearranged in one function (each re-analyzes the CFG).
const MAX_LOOPS: usize = 64;

pub(crate) fn layout_loops(module: &mut IrModule) {
    for func in module.functions.iter_mut().filter(|f| !f.is_declaration) {
        for _ in 0..MAX_LOOPS {
            if !layout_one(func) {
                break;
            }
        }
    }
}

/// Whether `block` may fall through into the block labeled `next`.
fn falls_into(block: &BasicBlock, next: BlockId) -> bool {
    match &block.terminator {
        Terminator::Branch(l) => *l == next,
        Terminator::CondBranch { true_label, false_label, .. } => *true_label == next || *false_label == next,
        _ => false,
    }
}

/// Rearrange the first loop that benefits; false when none does.
fn layout_one(func: &mut IrFunction) -> bool {
    if func.blocks.len() < 3 {
        return false;
    }
    let cfg = analysis::CfgAnalysis::build(func);
    let loops = loop_analysis::find_natural_loops(cfg.num_blocks, &cfg.preds, &cfg.succs, &cfg.idom);
    for lp in loop_analysis::merge_loops_by_header(loops) {
        let h = lp.header;
        if h == 0 {
            continue;
        }
        // The latch laid out last after the header (a loop already
        // rearranged has its latch before the header).
        let header_label = func.blocks[h].label;
        let Some(last) = lp.body.iter().copied().filter(|&b| b > h && match &func.blocks[b].terminator {
            Terminator::Branch(l) => *l == header_label,
            Terminator::CondBranch { true_label, false_label, .. } => *true_label == header_label || *false_label == header_label,
            _ => false,
        }).max() else { continue };
        // The run of blocks in the loop ending at the latch, each falling
        // into the next, whose first nothing falls into.
        let mut k = last;
        while k > h && lp.body.contains(&(k - 1)) && falls_into(&func.blocks[k - 1], func.blocks[k].label) {
            k -= 1;
        }
        if k == h || falls_into(&func.blocks[k - 1], func.blocks[k].label) {
            continue;
        }
        let tail: Vec<BasicBlock> = func.blocks.drain(k..=last).collect();
        func.blocks.splice(h..h, tail);
        return true;
    }
    false
}
