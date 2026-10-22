//! Loop-invariant load hoisting after loop rotation.
//!
//! LICM runs while loops are still top-tested, where a load in the body
//! cannot move to the preheader: the loop may run zero times, and the
//! pointer may then be invalid (`for (i = 0; i < n; i++) s += p->f;` with
//! `p == NULL` when `n == 0`). Once loop_rotate has made a loop
//! bottom-tested, the guard (the old header) enters it only to run at least
//! one iteration. A load that every iteration executes, before anything
//! that might not return, can then run once in a new block on the guard's
//! edge into the loop, provided nothing in the loop may write the memory it
//! reads; mod_ref answers that for calls as well as stores. Loads from a
//! local at a constant in-bounds offset are safe anywhere in the loop, and
//! pure instructions computed from hoisted loads follow them out.
//!
//! Runs after phi elimination: a value is invariant when every definition
//! of it is outside the loop, and only values defined once are moved.

use crate::common::fx_hash::{FxHashMap, FxHashSet};
use crate::common::source::Span;
use crate::common::types::AddressSpace;
use crate::ir::analysis::CfgAnalysis;
use crate::ir::reexports::{BasicBlock, BlockId, Instruction, IrFunction, IrModule, Operand, Terminator};
use super::licm::is_hoistable;
use super::loop_analysis::{find_natural_loops, merge_loops_by_header, NaturalLoop};
use super::mod_ref::{AliasModel, ModRef, Root};

pub(crate) fn hoist_rotated_loops(module: &mut IrModule) -> usize {
    let mr = ModRef::compute(module);
    let mut next_label = module.functions.iter().flat_map(|f| &f.blocks).map(|b| b.label.0 + 1).max().unwrap_or(0);
    let mut total = 0;
    for func in module.functions.iter_mut().filter(|f| !f.is_declaration) {
        total += hoist_function(func, &mr, &mut next_label);
    }
    total
}

fn hoist_function(func: &mut IrFunction, mr: &ModRef, next_label: &mut u32) -> usize {
    if func.blocks.len() < 2 {
        return 0;
    }
    // Moving instructions changes no value's provenance, so one model serves.
    let model = AliasModel::new(func, mr);
    let mut done: FxHashSet<BlockId> = FxHashSet::default();
    let mut total = 0;
    loop {
        let cfg = CfgAnalysis::build(func);
        let mut loops = merge_loops_by_header(find_natural_loops(cfg.num_blocks, &cfg.preds, &cfg.succs, &cfg.idom));
        // Inner loops first, so what leaves them can then leave the outer loop.
        loops.sort_by_key(|l| (l.body.len(), l.header));
        let mut progressed = false;
        for l in &loops {
            if !done.insert(func.blocks[l.header].label) {
                continue;
            }
            let n = hoist_loop(func, &cfg, l, &model, mr, next_label);
            if n > 0 {
                total += n;
                progressed = true;
                break;
            }
        }
        if !progressed {
            return total;
        }
    }
}

fn hoist_loop(
    func: &mut IrFunction,
    cfg: &CfgAnalysis,
    l: &NaturalLoop,
    model: &AliasModel,
    mr: &ModRef,
    next_label: &mut u32,
) -> usize {
    let header = l.header;
    let outside: Vec<usize> = cfg.preds.row(header).iter().map(|&p| p as usize).filter(|p| !l.body.contains(p)).collect();
    let [guard] = outside[..] else { return 0 };
    if !matches!(func.blocks[guard].terminator, Terminator::Branch(_) | Terminator::CondBranch { .. }) {
        return 0;
    }
    let mut blocks: Vec<usize> = l.body.iter().copied().collect();
    blocks.sort_unstable();
    if blocks.iter().any(|&b| func.blocks[b].instructions.iter().any(|i| matches!(i, Instruction::InlineAsm { .. }))) {
        return 0;
    }

    let mut n = func.next_value_id as usize;
    for inst in func.blocks.iter().flat_map(|b| &b.instructions) {
        if let Some(d) = inst.dest() {
            n = n.max(d.0 as usize + 1);
        }
    }
    // How many definitions each value has, and whether one is in the loop.
    let mut defs = vec![0u32; n];
    let mut in_loop = vec![false; n];
    let mut alloca_size: FxHashMap<u32, usize> = FxHashMap::default();
    let mut const_gep: FxHashMap<u32, (u32, i64)> = FxHashMap::default();
    for (b, block) in func.blocks.iter().enumerate() {
        for inst in &block.instructions {
            if let Some(d) = inst.dest() {
                defs[d.0 as usize] += 1;
                in_loop[d.0 as usize] |= l.body.contains(&b);
            }
            match inst {
                Instruction::Alloca { dest, size, .. } => {
                    alloca_size.insert(dest.0, *size);
                }
                Instruction::GetElementPtr { dest, base, offset: Operand::Const(c), .. } => {
                    if let Some(off) = c.to_i64() {
                        const_gep.insert(dest.0, (base.0, off));
                    }
                }
                _ => {}
            }
        }
    }
    // Whether `size` bytes at `ptr` are inside a local, so loading them
    // cannot fault wherever the load runs.
    let in_bounds_local = |ptr: u32, size: usize| {
        let (mut v, mut off) = (ptr, 0i64);
        for _ in 0..16 {
            if let Some(&s) = alloca_size.get(&v) {
                return off >= 0 && off as usize + size <= s;
            }
            match const_gep.get(&v) {
                Some(&(base, o)) if defs[v as usize] == 1 => {
                    v = base;
                    off = off.saturating_add(o);
                }
                _ => return false,
            }
        }
        false
    };

    // Everything the loop may write.
    let mut writes: Vec<Root> = Vec::new();
    for &b in &blocks {
        for inst in &func.blocks[b].instructions {
            model.for_each_write(inst, mr, |r| {
                if !writes.contains(&r) {
                    writes.push(r);
                }
            });
        }
    }

    // Blocks whose instructions run on every entry to the loop before
    // leaving it: those dominating every exit, when everything that can
    // run before them in an iteration returns.
    let dominates = |a: usize, mut b: usize| loop {
        if a == b {
            return true;
        }
        let up = cfg.idom[b];
        if up == b || up == usize::MAX {
            return false;
        }
        b = up;
    };
    let exits: Vec<usize> = blocks.iter().copied().filter(|&b| {
        let s = cfg.succs.row(b);
        s.is_empty() || s.iter().any(|&x| !l.body.contains(&(x as usize)))
    }).collect();
    let always_runs = |b: usize| {
        if !exits.iter().all(|&x| dominates(b, x)) {
            return false;
        }
        // Blocks that can run before b in an iteration starting at the header.
        let mut before: FxHashSet<usize> = FxHashSet::default();
        let mut work = if b == header { vec![] } else { vec![b] };
        while let Some(x) = work.pop() {
            for &p in cfg.preds.row(x) {
                let p = p as usize;
                if l.body.contains(&p) && p != b && before.insert(p) && p != header {
                    work.push(p);
                }
            }
        }
        before.iter().all(|&x| func.blocks[x].instructions.iter().all(|i| mr.always_continues(i)))
    };
    let mut runs_first: FxHashMap<usize, bool> = FxHashMap::default();

    let mut hoisted: FxHashSet<u32> = FxHashSet::default();
    let mut moves: Vec<(usize, usize)> = Vec::new();
    loop {
        let mut found = false;
        for &b in &blocks {
            for (k, inst) in func.blocks[b].instructions.iter().enumerate() {
                let Some(d) = inst.dest() else { continue };
                if hoisted.contains(&d.0) || defs[d.0 as usize] != 1 {
                    continue;
                }
                let (mut invariant, mut uses_hoisted) = (true, false);
                inst.for_each_used_value(|v| {
                    let h = hoisted.contains(&v);
                    invariant &= !in_loop[v as usize] || h;
                    uses_hoisted |= h;
                });
                if !invariant {
                    continue;
                }
                let ok = match inst {
                    Instruction::Load { ptr, ty, seg_override: AddressSpace::Default, .. } => {
                        let root = model.root(ptr.0);
                        !writes.iter().any(|&w| model.may_alias(w, root))
                            && (in_bounds_local(ptr.0, ty.size())
                                || (*runs_first.entry(b).or_insert_with(|| always_runs(b))
                                    && func.blocks[b].instructions[..k].iter().all(|i| mr.always_continues(i))))
                    }
                    // Only what the hoisted loads make invariant: LICM has
                    // already moved the rest it could.
                    _ => uses_hoisted && is_hoistable(inst),
                };
                if ok {
                    hoisted.insert(d.0);
                    moves.push((b, k));
                    found = true;
                }
            }
        }
        if !found {
            break;
        }
    }
    if moves.is_empty() {
        return 0;
    }

    // Take the instructions out, keeping their discovery order (operands
    // before their users).
    let mut taken: FxHashMap<(usize, usize), (Instruction, Option<Span>)> = FxHashMap::default();
    let mut by_block = moves.clone();
    by_block.sort_unstable_by(|a, b| b.cmp(a));
    for &(b, k) in &by_block {
        let block = &mut func.blocks[b];
        let span = (block.source_spans.len() == block.instructions.len()).then(|| block.source_spans.remove(k));
        taken.insert((b, k), (block.instructions.remove(k), span));
    }
    let (insts, spans): (Vec<Instruction>, Vec<Option<Span>>) = moves.iter().map(|m| taken.remove(m).unwrap()).unzip();
    let count = insts.len();

    let header_label = func.blocks[header].label;
    if matches!(func.blocks[guard].terminator, Terminator::Branch(_)) {
        let g = &mut func.blocks[guard];
        let parallel = g.source_spans.len() == g.instructions.len();
        g.instructions.extend(insts);
        if parallel {
            g.source_spans.extend(spans.into_iter().map(|s| s.unwrap_or_else(Span::dummy)));
        }
        return count;
    }
    let label = BlockId(*next_label);
    *next_label += 1;
    if let Terminator::CondBranch { true_label, false_label, .. } = &mut func.blocks[guard].terminator {
        for t in [true_label, false_label] {
            if *t == header_label {
                *t = label;
            }
        }
    }
    let source_spans = if spans.iter().all(Option::is_some) { spans.into_iter().flatten().collect() } else { Vec::new() };
    func.blocks.insert(header, BasicBlock { label, instructions: insts, terminator: Terminator::Branch(header_label), source_spans });
    count
}
