//! Step a loop's pointer right after its load, for post-index addressing.
//!
//! A pointer variable stepped at the end of each iteration and read once,
//! by a load at the top, costs the step's own add:
//!
//!   latch:   %q = gep %p, 4                     header:  %x = load %p
//!            %p = copy %q             =>                 %q = gep %p, 4
//!            ... cmp %p, %end                            %p = copy %q
//!   header:  %x = load %p                       latch:   ... cmp %p, %end
//!
//! With the step right after the load, the backend's peephole folds the two
//! into one post-indexed load (`ldr w1, [x20], #4`). Valid when the loop
//! reads %p nowhere else but after the step in the latch, and nothing that
//! can run after the loop has started reads it: every iteration then still
//! loads from %p and steps it once before any other read. Runs after phi
//! elimination and loop layout, on loops with one latch, which may be the
//! header itself.

use crate::common::fx_hash::FxHashSet;
use crate::common::types::IrType;
use crate::ir::analysis;
use crate::ir::reexports::{Instruction, IrBinOp, IrFunction, IrModule, Operand, Value};
use super::loop_analysis;

pub(crate) fn step_to_loads(module: &mut IrModule) {
    for func in module.functions.iter_mut().filter(|f| !f.is_declaration) {
        step_function(func);
    }
}

/// One loop, with the blocks reachable once it has started.
struct Loop<'a> {
    body: &'a FxHashSet<usize>,
    after: FxHashSet<usize>,
    header: usize,
    latch: usize,
}

/// A step `%q = gep %p, c` at latch index `g`, its `%p = copy %q` at `c`,
/// and the header's `load %p` at `k`.
struct Step {
    g: usize,
    c: usize,
    k: usize,
    p: Value,
    q: Value,
}

fn step_function(func: &mut IrFunction) -> usize {
    if func.blocks.len() < 2 {
        return 0;
    }
    let cfg = analysis::CfgAnalysis::build(func);
    let loops = loop_analysis::find_natural_loops(cfg.num_blocks, &cfg.preds, &cfg.succs, &cfg.idom);
    let mut moved = 0;
    for lp in loop_analysis::merge_loops_by_header(loops) {
        let latches: Vec<usize> = cfg.preds.row(lp.header).iter().map(|&p| p as usize)
            .filter(|p| lp.body.contains(p)).collect();
        let [latch] = latches[..] else { continue };
        let mut after: FxHashSet<usize> = FxHashSet::default();
        let mut work = vec![lp.header];
        while let Some(b) = work.pop() {
            if after.insert(b) {
                work.extend(cfg.succs.row(b).iter().map(|&s| s as usize));
            }
        }
        let l = Loop { body: &lp.body, after, header: lp.header, latch };
        if let Some(step) = find_step(func, &l) {
            move_step(func, &l, &step);
            moved += 1;
        }
    }
    moved
}

fn find_step(func: &IrFunction, l: &Loop) -> Option<Step> {
    // The latch's steps: %q = gep %p, c (or add or sub), then %p = copy %q.
    let insts = &func.blocks[l.latch].instructions;
    insts.iter().enumerate().find_map(|(g, inst)| {
        let (p, q) = match inst {
            Instruction::GetElementPtr { dest, base, offset: Operand::Const(_), .. } => (*base, *dest),
            Instruction::BinOp { dest, op: IrBinOp::Add | IrBinOp::Sub, lhs: Operand::Value(base),
                                 rhs: Operand::Const(_), ty: IrType::I64 | IrType::Ptr } => (*base, *dest),
            _ => return None,
        };
        let c = insts.iter().position(|i| matches!(i,
            Instruction::Copy { dest, src: Operand::Value(s) } if *dest == p && *s == q))?;
        // The header's load of %p; in a one-block loop, before the copy.
        let k = func.blocks[l.header].instructions.iter().position(|i| matches!(i,
            Instruction::Load { ptr, .. } if *ptr == p))?;
        let step = Step { g, c, k, p, q };
        (g < c && (l.header != l.latch || k < c) && is_movable(func, l, &step)).then_some(step)
    })
}

/// Every other read of %p or %q is a read of %p after the copy in the latch
/// or a read before the loop, and nothing else in the loop defines either.
fn is_movable(func: &IrFunction, l: &Loop, s: &Step) -> bool {
    for (b, block) in func.blocks.iter().enumerate() {
        let reachable = l.after.contains(&b);
        for (i, inst) in block.instructions.iter().enumerate() {
            if l.body.contains(&b) && inst.dest().is_some_and(|d| (d == s.p && !(b == l.latch && i == s.c))
                                                             || (d == s.q && !(b == l.latch && i == s.g))) {
                return false;
            }
            let used = inst.used_values();
            let (reads_p, reads_q) = (used.contains(&s.p.0), used.contains(&s.q.0));
            if !reads_p && !reads_q {
                continue;
            }
            let ok = (b == l.header && i == s.k)
                || (b == l.latch && (i == s.g || i == s.c || (i > s.c && !reads_q)))
                || (!reachable && !reads_q);
            if !ok {
                return false;
            }
        }
        let used = block.terminator.used_values();
        if (used.contains(&s.p.0) && reachable && b != l.latch) || used.contains(&s.q.0) {
            return false;
        }
    }
    true
}

/// Move the step to right after the load.
fn move_step(func: &mut IrFunction, l: &Loop, s: &Step) {
    let lb = &mut func.blocks[l.latch];
    let spans = lb.source_spans.len() == lb.instructions.len();
    let copy = lb.instructions.remove(s.c);
    let gep = lb.instructions.remove(s.g);
    if spans {
        lb.source_spans.remove(s.c);
        lb.source_spans.remove(s.g);
    }
    let k = if l.header == l.latch && s.g < s.k { s.k - 1 } else { s.k };
    let hb = &mut func.blocks[l.header];
    if hb.source_spans.len() == hb.instructions.len() && !hb.source_spans.is_empty() {
        let span = hb.source_spans[k];
        hb.source_spans.insert(k + 1, span);
        hb.source_spans.insert(k + 1, span);
    }
    hb.instructions.insert(k + 1, copy);
    hb.instructions.insert(k + 1, gep);
}
