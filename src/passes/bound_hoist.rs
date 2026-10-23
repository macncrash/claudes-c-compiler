//! Loop bound hoisting: move an exit test's invariant loads out of the loop.
//!
//! iv_widen and lftr need a loop's bound to be computed outside the loop,
//! but a bound read from memory every iteration stays in the header when
//! LICM cannot show nothing writes it, as for a local whose address was
//! passed to a call before the loop:
//!
//!   generate(p, &l);                          %n = load l.n    (preheader)
//!   for (i = 0; i < l.n; i++)            =>   loop: %c = cmp slt %i, %n
//!       make_move(&next, l.m[i]);
//!
//! loop_hoist moves such loads only after loop rotation, too late for those
//! passes. Here, while loops are still top-tested, the instructions in the
//! header that compute an operand of its exit test (casts, constant GEPs and
//! loads) move to the preheader when every load reads memory nothing in the
//! loop may write (mod_ref answers that for calls) and follows only
//! instructions sure to continue. The header runs on every entry to the
//! loop, so they then run on the same paths as before, once.

use crate::common::fx_hash::FxHashSet;
use crate::common::types::AddressSpace;
use crate::ir::analysis;
use crate::ir::reexports::{Instruction, IrFunction, Operand, Terminator, Value};
use super::loop_analysis;
use super::mod_ref::{AliasModel, ModRef};

/// Most instructions moved for one operand.
const MAX_CHAIN: usize = 4;

pub(crate) fn hoist_function(func: &mut IrFunction, mr: &ModRef) -> usize {
    if func.blocks.len() < 2 {
        return 0;
    }
    let cfg = analysis::CfgAnalysis::build(func);
    let loops = loop_analysis::find_natural_loops(cfg.num_blocks, &cfg.preds, &cfg.succs, &cfg.idom);
    let mut model: Option<AliasModel> = None;
    let mut moved = 0;
    for lp in loop_analysis::merge_loops_by_header(loops) {
        let Some(pre) = loop_analysis::find_preheader(lp.header, &lp.body, &cfg.preds) else { continue };
        let header = &func.blocks[lp.header];
        let Terminator::CondBranch { cond: Operand::Value(cond), .. } = header.terminator else { continue };
        let Some(Instruction::Cmp { lhs, rhs, .. }) = header.instructions.iter().find(|i| i.dest() == Some(cond))
            else { continue };
        let mut chain: Vec<usize> = Vec::new();
        for op in [*lhs, *rhs] {
            let Operand::Value(v) = op else { continue };
            if let Some(c) = invariant_chain(func, &lp.body, lp.header, v, mr, &mut model) {
                chain.extend(c);
            }
        }
        chain.sort_unstable();
        chain.dedup();
        // Take them out of the header, last first, and append them to the
        // preheader in order.
        let mut taken = Vec::new();
        for &k in chain.iter().rev() {
            let block = &mut func.blocks[lp.header];
            if block.source_spans.len() == block.instructions.len() {
                block.source_spans.remove(k);
            }
            taken.push(block.instructions.remove(k));
        }
        moved += taken.len();
        let block = &mut func.blocks[pre];
        let parallel = !block.source_spans.is_empty() && block.source_spans.len() == block.instructions.len();
        for inst in taken.into_iter().rev() {
            if parallel {
                let span = *block.source_spans.last().unwrap();
                block.source_spans.push(span);
            }
            block.instructions.push(inst);
        }
    }
    moved
}

fn def_of(func: &IrFunction, v: Value) -> Option<(usize, usize)> {
    func.blocks.iter().enumerate().find_map(|(b, block)| {
        block.instructions.iter().position(|i| i.dest() == Some(v)).map(|k| (b, k))
    })
}

/// The instructions in the header that compute `v` (their indices) when all
/// of them can move to the preheader, or None. Empty when `v` is defined
/// outside the loop.
fn invariant_chain(func: &IrFunction, body: &FxHashSet<usize>, header: usize, v: Value, mr: &ModRef,
                   model: &mut Option<AliasModel>) -> Option<Vec<usize>> {
    let insts = &func.blocks[header].instructions;
    let mut chain: Vec<usize> = Vec::new();
    let mut work = vec![v];
    while let Some(v) = work.pop() {
        let (b, k) = def_of(func, v)?;
        if !body.contains(&b) {
            continue;
        }
        if b != header || chain.contains(&k) || chain.len() >= MAX_CHAIN {
            return None;
        }
        match &insts[k] {
            Instruction::Cast { src, .. } => {
                if let Operand::Value(s) = src {
                    work.push(*s);
                }
            }
            Instruction::GetElementPtr { base, offset: Operand::Const(_), .. } => work.push(*base),
            Instruction::Load { ptr, seg_override: AddressSpace::Default, volatile: false, .. } => {
                if !insts[..k].iter().all(|i| mr.always_continues(i)) {
                    return None;
                }
                let model = model.get_or_insert_with(|| AliasModel::new(func, mr));
                let root = model.root(ptr.0);
                let written = body.iter().flat_map(|&b| &func.blocks[b].instructions).any(|i| {
                    let mut hit = false;
                    model.for_each_write(i, mr, |w| hit |= model.may_alias(w, root));
                    hit
                });
                if written {
                    return None;
                }
                work.push(*ptr);
            }
            _ => return None,
        }
        chain.push(k);
    }
    Some(chain)
}
