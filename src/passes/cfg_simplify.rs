//! CFG Simplification pass.
//!
//! Simplifies the control flow graph by:
//! 1. Folding `CondBranch` with a known-constant condition to `Branch`
//! 2. Folding `Switch` with a known-constant value to `Branch` (matching case or default)
//! 3. Converting `CondBranch` where both targets are the same to `Branch`
//! 4. Threading jump chains: if block A branches to empty block B which just
//!    branches to C, redirect A to branch directly to C (only when safe), and
//!    threading predecessors that feed a constant into a phi that their
//!    successor only branches on (`a || b || c` chains), and fusing two
//!    branches on bounds of one value (`c >= 'a' && c <= 'z'`) into one
//!    unsigned compare of the value minus the lower bound
//! 5. Removing dead (unreachable) blocks that have no predecessors
//! 6. Simplifying trivial phi nodes (single-entry or all-same-value) to Copy
//! 7. Merging single-predecessor blocks into their predecessor
//!
//! This pass runs to a fixpoint, since one simplification can enable others.
//! Phi nodes in successor blocks are updated when edges are redirected.

use crate::common::fx_hash::{FxHashMap, FxHashSet};
use crate::common::types::IrType;
use crate::ir::reexports::{
    BasicBlock,
    BlockId,
    Instruction,
    IrCmpOp,
    IrConst,
    IrFunction,
    Operand,
    Terminator,
    Value,
};

/// Maximum depth for resolving transitive jump chains (A→B→C→...),
/// to prevent pathological cases.
const MAX_CHAIN_DEPTH: u32 = 32;

/// Maximum depth for walking single-predecessor chains when resolving
/// a value to a constant across blocks.
const MAX_PRED_CHAIN_DEPTH: usize = 8;

/// Maximum recursion depth for global cross-block value resolution.
const MAX_GLOBAL_RESOLVE_DEPTH: usize = 16;

// ---------------------------------------------------------------------------
// Public entry points
// ---------------------------------------------------------------------------

/// Per-function entry point for dirty-tracking pipeline.
pub(crate) fn run_function(func: &mut IrFunction) -> usize {
    simplify_cfg(func)
}

/// Simplify the CFG of a single function.
/// Iterates until no more simplifications are possible (fixpoint).
///
/// Builds the `label_to_idx` map once per fixpoint iteration and shares it
/// across sub-passes that need block lookups, avoiding redundant HashMap
/// construction.
pub(crate) fn simplify_cfg(func: &mut IrFunction) -> usize {
    if func.blocks.len() <= 1 {
        return 0;
    }

    let mut total = 0;
    loop {
        let mut changed = 0;
        // Build the label-to-index map once per fixpoint iteration.
        // Sub-passes that modify block structure (remove_dead_blocks,
        // merge_single_pred_blocks) invalidate this map, but they run last.
        let label_to_idx = build_label_to_idx(func);
        changed += fold_constant_cond_branches(func, &label_to_idx);
        changed += fold_constant_switches(func, &label_to_idx);
        changed += simplify_redundant_cond_branches(func);
        changed += thread_jump_chains(func, &label_to_idx);
        changed += thread_phi_cond_branches(func, &label_to_idx);
        changed += thread_phi_cond_branches_merging(func, &label_to_idx);
        changed += fuse_range_tests(func, &label_to_idx);
        changed += remove_dead_blocks(func, &label_to_idx);
        changed += simplify_trivial_phis(func);
        changed += merge_single_pred_blocks(func);
        if changed == 0 {
            break;
        }
        total += changed;
    }
    total
}

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------

/// Build a map from BlockId -> index in func.blocks for O(1) lookup.
#[inline]
fn build_label_to_idx(func: &IrFunction) -> FxHashMap<BlockId, usize> {
    func.blocks.iter().enumerate().map(|(i, b)| (b.label, i)).collect()
}

/// Build predecessor count for each block.
///
/// Counts edges from terminators and from InlineAsm goto_labels (which are
/// implicit control flow edges). Also returns a single-predecessor map:
/// `single_pred[B] = A` iff A is B's only predecessor.
fn build_pred_info(func: &IrFunction) -> (FxHashMap<BlockId, u32>, FxHashMap<BlockId, BlockId>) {
    let mut pred_count: FxHashMap<BlockId, u32> = FxHashMap::default();
    let mut single_pred: FxHashMap<BlockId, BlockId> = FxHashMap::default();

    for block in func.blocks.iter() {
        let src = block.label;
        let mut add_edge = |target: BlockId| {
            let count = pred_count.entry(target).or_insert(0);
            *count += 1;
            if *count == 1 {
                single_pred.insert(target, src);
            } else {
                single_pred.remove(&target);
            }
        };
        for_each_terminator_target(&block.terminator, &mut add_edge);
        // InlineAsm goto_labels are implicit control flow edges.
        for inst in &block.instructions {
            if let Instruction::InlineAsm { goto_labels, .. } = inst {
                for (_, label) in goto_labels {
                    add_edge(*label);
                }
            }
        }
    }
    (pred_count, single_pred)
}

/// Remove phi entries that reference `source_label` from all phi nodes in `block`.
fn remove_phi_entries_from(block: &mut BasicBlock, source_label: BlockId) {
    for inst in &mut block.instructions {
        if let Instruction::Phi { incoming, .. } = inst {
            incoming.retain(|(_, label)| *label != source_label);
        }
    }
}

/// Remove phi entries from a block where the source label is in `dead_labels`.
fn remove_phi_entries_from_set(block: &mut BasicBlock, dead_labels: &FxHashSet<BlockId>) {
    for inst in &mut block.instructions {
        if let Instruction::Phi { incoming, .. } = inst {
            incoming.retain(|(_, label)| !dead_labels.contains(label));
        }
    }
}

/// Remap terminator targets using a label substitution map.
/// For each target in the terminator, if `remap[target]` exists, replace it.
fn remap_terminator_targets(term: &mut Terminator, remap: &FxHashMap<BlockId, BlockId>) {
    match term {
        Terminator::Branch(target) => {
            if let Some(&new) = remap.get(target) {
                *target = new;
            }
        }
        Terminator::CondBranch { true_label, false_label, .. } => {
            if let Some(&new) = remap.get(true_label) {
                *true_label = new;
            }
            if let Some(&new) = remap.get(false_label) {
                *false_label = new;
            }
        }
        Terminator::Switch { cases, default, .. } => {
            if let Some(&new) = remap.get(default) {
                *default = new;
            }
            for (_, label) in cases.iter_mut() {
                if let Some(&new) = remap.get(label) {
                    *label = new;
                }
            }
        }
        Terminator::IndirectBranch { possible_targets, .. } => {
            for target in possible_targets.iter_mut() {
                if let Some(&new) = remap.get(target) {
                    *target = new;
                }
            }
        }
        _ => {}
    }
}

/// Visit each branch target of a terminator, calling `f` for each target.
/// Switch targets are deduplicated against the default to avoid double-counting.
#[inline]
fn for_each_terminator_target(term: &Terminator, mut f: impl FnMut(BlockId)) {
    match term {
        Terminator::Branch(target) => f(*target),
        Terminator::CondBranch { true_label, false_label, .. } => {
            f(*true_label);
            f(*false_label);
        }
        Terminator::IndirectBranch { possible_targets, .. } => {
            for &target in possible_targets {
                f(target);
            }
        }
        Terminator::Switch { cases, default, .. } => {
            f(*default);
            for &(_, label) in cases {
                if label != *default {
                    f(label);
                }
            }
        }
        Terminator::Return(_) | Terminator::Unreachable => {}
    }
}

// ---------------------------------------------------------------------------
// Sub-pass: threading branches on phis of constants
// ---------------------------------------------------------------------------

/// `B: p = phi [1, P1], [x, P2]; condbr p, T, F` where p has no other use:
/// the edge from P1 always continues to T, so redirect P1 straight to T
/// (adding P1 to T's phis with B's incoming values). This is what `a || b ||
/// c` and `a && b && c` lower to: each operand's result is merged into a
/// boolean phi that the next test branches on again.
///
/// B may merge other values too (`*++p == *++q && ...` merges the pointers
/// with the flag) when each of their uses is either S's phi entry from B
/// (P's entry then takes the value B's phi has for P) or in the region of
/// B's other successor O, which B alone enters: bypassing B only removes
/// paths through B, so B still dominates those uses.
fn thread_phi_cond_branches(func: &mut IrFunction, label_to_idx: &FxHashMap<BlockId, usize>) -> usize {
    // Uses of each value outside block terminators that branch on it.
    let mut use_count: FxHashMap<u32, u32> = FxHashMap::default();
    for block in &func.blocks {
        for inst in &block.instructions {
            inst.for_each_used_value(|id| *use_count.entry(id).or_insert(0) += 1);
        }
        block.terminator.for_each_used_value(|id| *use_count.entry(id).or_insert(0) += 1);
    }

    // The other phis of candidate blocks, and where each is used: (block
    // index, the incoming label for a use in a phi).
    let mut extra_uses: FxHashMap<u32, Vec<(usize, Option<BlockId>)>> = FxHashMap::default();
    for block in &func.blocks {
        let Terminator::CondBranch { cond: Operand::Value(c), .. } = &block.terminator else { continue };
        if block.instructions.len() > 1 && block.instructions.iter().all(|i| matches!(i, Instruction::Phi { .. })) {
            for inst in &block.instructions {
                if let Some(d) = inst.dest().filter(|d| d != c) {
                    extra_uses.insert(d.0, Vec::new());
                }
            }
        }
    }
    let mut cfg: Option<(crate::ir::analysis::CfgAnalysis, FxHashMap<BlockId, u32>)> = None;
    if !extra_uses.is_empty() {
        for (ui, block) in func.blocks.iter().enumerate() {
            for inst in &block.instructions {
                if let Instruction::Phi { incoming, .. } = inst {
                    for (op, l) in incoming {
                        if let Operand::Value(v) = op {
                            if let Some(u) = extra_uses.get_mut(&v.0) {
                                u.push((ui, Some(*l)));
                            }
                        }
                    }
                } else {
                    inst.for_each_used_value(|v| if let Some(u) = extra_uses.get_mut(&v) { u.push((ui, None)); });
                }
            }
            block.terminator.for_each_used_value(|v| if let Some(u) = extra_uses.get_mut(&v) { u.push((ui, None)); });
        }
    }
    // Blocks whose region holds uses of a bypassed B's phis (they must keep
    // B as their only predecessor), and this round's targets.
    let mut anchors: FxHashSet<BlockId> = FxHashSet::default();
    let mut targets: FxHashSet<BlockId> = FxHashSet::default();

    // (pred index, B label, S label, B's phis that get a phi at S)
    let mut actions: Vec<(usize, BlockId, BlockId, Vec<u32>)> = Vec::new();
    let mut touched: FxHashSet<BlockId> = FxHashSet::default();
    let mut phi_inserted = false;
    for block in &func.blocks {
        let b = block.label;
        if block.instructions.is_empty() || !block.instructions.iter().all(|i| matches!(i, Instruction::Phi { .. })) {
            continue;
        }
        let Terminator::CondBranch { cond: Operand::Value(c), true_label, false_label } = &block.terminator else { continue };
        let Some(Instruction::Phi { dest: p, incoming, .. }) = block.instructions.iter().find(|i| i.dest() == Some(*c)) else { continue };
        if *true_label == b || *false_label == b || true_label == false_label {
            continue;
        }
        if use_count.get(&p.0).copied().unwrap_or(0) != 1 {
            continue;
        }
        let extras: Vec<u32> = block.instructions.iter().filter_map(|i| i.dest()).filter(|d| d != p).map(|d| d.0).collect();
        for (op, pred) in incoming {
            let Operand::Const(k) = op else { continue };
            let Some(v) = k.to_i64() else { continue };
            let target = if v != 0 { *true_label } else { *false_label };
            if incoming.iter().filter(|(_, l)| l == pred).count() != 1 || touched.contains(pred) {
                continue;
            }
            let Some(&pi) = label_to_idx.get(pred) else { continue };
            let pred_term = &func.blocks[pi].terminator;
            // Only plain edges; the predecessor must not already reach the target.
            let mut targets_b = 0;
            let mut targets_s = false;
            let simple = match pred_term {
                Terminator::Branch(_) | Terminator::CondBranch { .. } => true,
                _ => false,
            };
            for_each_terminator_target(pred_term, |t| {
                if t == b { targets_b += 1; }
                if t == target { targets_s = true; }
            });
            let has_asm_goto = func.blocks[pi].instructions.iter()
                .any(|i| matches!(i, Instruction::InlineAsm { goto_labels, .. } if !goto_labels.is_empty()));
            if !simple || targets_b != 1 || targets_s || has_asm_goto {
                continue;
            }
            // Every phi in the target needs an incoming value from B.
            let Some(&si) = label_to_idx.get(&target) else { continue };
            let phis_ok = func.blocks[si].instructions.iter().all(|i| match i {
                Instruction::Phi { incoming, .. } => incoming.iter().filter(|(_, l)| *l == b).count() == 1,
                _ => true,
            });
            if !phis_ok || anchors.contains(&target) {
                continue;
            }
            if !extras.is_empty() {
                let other = if target == *true_label { *false_label } else { *true_label };
                if touched.contains(&b) || targets.contains(&other) {
                    continue;
                }
                let (cfg, pred_count) = cfg.get_or_insert_with(|| {
                    (crate::ir::analysis::CfgAnalysis::build(func), build_pred_info(func).0)
                });
                let Some(&oi) = label_to_idx.get(&other) else { continue };
                let bi = label_to_idx[&b];
                let dominated_by = |x: usize, mut u: usize| loop {
                    if u == x {
                        return true;
                    }
                    let up = cfg.idom[u];
                    if up == u || up == usize::MAX {
                        return false;
                    }
                    u = up;
                };
                let block_of = |l: &BlockId| label_to_idx.get(l).copied();
                // Uses past O need O entered from B alone (a loop's merged
                // counter read only by the header's phi entry from B does not).
                let only_from_b = pred_count.get(&other) == Some(&1);
                let mut in_region = false;
                let mut needs_phi: Vec<u32> = Vec::new();
                let ok = extras.iter().all(|v| {
                    let one_entry = block.instructions.iter().any(|i| matches!(i, Instruction::Phi { dest, incoming, .. }
                        if dest.0 == *v && incoming.iter().filter(|(_, l)| l == pred).count() == 1));
                    one_entry && extra_uses[v].iter().all(|&(ui, l)| {
                        let at = match l {
                            Some(l) if l == b => return ui == si || ui == oi,
                            Some(l) => match block_of(&l) { Some(w) => w, None => return false },
                            None => ui,
                        };
                        if only_from_b && dominated_by(oi, at) {
                            in_region = true;
                            true
                        } else if dominated_by(si, at) {
                            if !needs_phi.contains(v) {
                                needs_phi.push(*v);
                            }
                            true
                        } else {
                            false
                        }
                    })
                });
                if !ok {
                    continue;
                }
                // Uses past S (`len = end - scan` after a match loop that
                // several tests leave) read a new phi at S. Every other
                // predecessor of S must see B's value, and S takes no other
                // new predecessor this round.
                if !needs_phi.is_empty() {
                    let preds_ok = cfg.preds.row(si).iter().all(|&p| {
                        let p = p as usize;
                        p == bi || dominated_by(si, p) || dominated_by(bi, p)
                    });
                    if !preds_ok || dominated_by(si, bi) || targets.contains(&target) || phi_inserted {
                        continue;
                    }
                    phi_inserted = true;
                    anchors.insert(target);
                }
                if in_region {
                    anchors.insert(other);
                }
                touched.insert(b);
                if !needs_phi.is_empty() {
                    touched.insert(*pred);
                    targets.insert(target);
                    actions.push((pi, b, target, needs_phi));
                    continue;
                }
            }
            touched.insert(*pred);
            targets.insert(target);
            actions.push((pi, b, target, Vec::new()));
        }
    }

    let mut next_id = func.next_value_id.max(func.max_value_id() + 1);
    for (pi, b, s, needs_phi) in &actions {
        let (pi, b, s) = (*pi, *b, *s);
        let pred_label = func.blocks[pi].label;
        let remap: FxHashMap<BlockId, BlockId> = [(b, s)].into_iter().collect();
        remap_terminator_targets(&mut func.blocks[pi].terminator, &remap);
        let bi = label_to_idx[&b];
        // The values B's phis have on the edge from the predecessor.
        let via_pred: FxHashMap<u32, Operand> = func.blocks[bi].instructions.iter().filter_map(|i| match i {
            Instruction::Phi { dest, incoming, .. } => incoming.iter().find(|(_, l)| *l == pred_label).map(|(op, _)| (dest.0, *op)),
            _ => None,
        }).collect();
        remove_phi_entries_from(&mut func.blocks[bi], pred_label);
        let si = label_to_idx[&s];
        for inst in &mut func.blocks[si].instructions {
            if let Instruction::Phi { incoming, .. } = inst {
                let from_b = incoming.iter().find(|(_, l)| *l == b).map(|(op, _)| *op);
                if let Some(op) = from_b {
                    let op = match &op {
                        Operand::Value(v) => via_pred.get(&v.0).cloned().unwrap_or(op),
                        Operand::Const(_) => op,
                    };
                    incoming.push((op, pred_label));
                }
            }
        }
        if needs_phi.is_empty() {
            continue;
        }
        // Uses of each such value where S dominates them read a phi at S:
        // B's value from B and from the predecessors B dominates, the phi
        // itself around a loop through S, and P's value from P.
        let (cfg, _) = cfg.as_ref().expect("built for extra phis");
        let dominated_by_s = |mut u: usize| loop {
            if u == si {
                return true;
            }
            let up = cfg.idom[u];
            if up == u || up == usize::MAX {
                return false;
            }
            u = up;
        };
        let s_preds: Vec<usize> = cfg.preds.row(si).iter().map(|&p| p as usize).collect();
        let mut map: Vec<Option<Operand>> = vec![None; next_id as usize];
        let mut new_phis = Vec::new();
        for &q in needs_phi {
            let ty = func.blocks[bi].instructions.iter().find_map(|i| match i {
                Instruction::Phi { dest, ty, .. } if dest.0 == q => Some(*ty),
                _ => None,
            }).expect("B's phi");
            let phi = Value(next_id);
            next_id += 1;
            map.push(None);
            map[q as usize] = Some(Operand::Value(phi));
            let mut incoming: Vec<(Operand, BlockId)> = s_preds.iter().map(|&p| {
                let v = if p != bi && dominated_by_s(p) { phi } else { Value(q) };
                (Operand::Value(v), func.blocks[p].label)
            }).collect();
            incoming.push((via_pred.get(&q).cloned().unwrap_or(Operand::Value(Value(q))), pred_label));
            new_phis.push(Instruction::Phi { dest: phi, ty, incoming });
        }
        for (ui, block) in func.blocks.iter_mut().enumerate() {
            let here = dominated_by_s(ui);
            for inst in &mut block.instructions {
                if let Instruction::Phi { incoming, .. } = inst {
                    for (op, l) in incoming.iter_mut() {
                        if let Operand::Value(v) = op {
                            if map.get(v.0 as usize).is_some_and(|m| m.is_some())
                                && label_to_idx.get(l).is_some_and(|&w| dominated_by_s(w)) {
                                *op = map[v.0 as usize].unwrap();
                            }
                        }
                    }
                } else if here {
                    crate::passes::copy_prop::replace_operands_in_instruction(inst, &map);
                }
            }
            if here {
                crate::passes::copy_prop::replace_operands_in_terminator(&mut block.terminator, &map);
            }
        }
        let block = &mut func.blocks[si];
        if !block.source_spans.is_empty() {
            let span = block.source_spans[0];
            block.source_spans.splice(0..0, std::iter::repeat_n(span, new_phis.len()));
        }
        block.instructions.splice(0..0, new_phis);
    }
    func.next_value_id = next_id;
    actions.len()
}

/// The same when B also merges values its targets read, as an inlined
/// `find(key, &v)` merges `v` (loaded on the found path) with the flag it
/// returns: `if (find(k, &v) && v == i)`. When every incoming flag is a
/// constant, every predecessor goes straight to its target and B is left
/// unreachable. A target reached only from B dominates the blocks that read
/// B's phis through it, so each such phi becomes a phi at the start of that
/// target over the predecessors now sent there. One B per call (the
/// dominator tree is built for it).
fn thread_phi_cond_branches_merging(func: &mut IrFunction, label_to_idx: &FxHashMap<BlockId, usize>) -> usize {
    let (pred_count, _) = build_pred_info(func);
    let mut use_count: FxHashMap<u32, u32> = FxHashMap::default();
    for block in &func.blocks {
        for inst in &block.instructions {
            inst.for_each_used_value(|id| *use_count.entry(id).or_insert(0) += 1);
        }
        block.terminator.for_each_used_value(|id| *use_count.entry(id).or_insert(0) += 1);
    }

    // (B, [(pred, target)])
    let mut candidates = Vec::new();
    for (bi, block) in func.blocks.iter().enumerate() {
        let b = block.label;
        if block.instructions.len() < 2 || !block.instructions.iter().all(|i| matches!(i, Instruction::Phi { .. })) {
            continue;
        }
        let Terminator::CondBranch { cond: Operand::Value(c), true_label: t, false_label: f } = block.terminator else { continue };
        if t == b || f == b || t == f || use_count.get(&c.0) != Some(&1) {
            continue;
        }
        let Some(Instruction::Phi { incoming, .. }) = block.instructions.iter().find(|i| i.dest() == Some(c)) else { continue };
        if pred_count.get(&b).copied() != Some(incoming.len() as u32) {
            continue;
        }
        let mut sends = Vec::with_capacity(incoming.len());
        for (op, pred) in incoming {
            let Some(v) = (match op { Operand::Const(k) => k.to_i64(), _ => None }) else { break };
            let target = if v != 0 { t } else { f };
            let Some(&pi) = label_to_idx.get(pred) else { break };
            let pb = &func.blocks[pi];
            let mut targets_b = 0;
            let mut targets_s = false;
            for_each_terminator_target(&pb.terminator, |x| {
                targets_b += (x == b) as u32;
                targets_s |= x == target;
            });
            let plain = matches!(pb.terminator, Terminator::Branch(_) | Terminator::CondBranch { .. });
            let asm_goto = pb.instructions.iter()
                .any(|i| matches!(i, Instruction::InlineAsm { goto_labels, .. } if !goto_labels.is_empty()));
            if !plain || targets_b != 1 || targets_s || asm_goto || pi == bi {
                break;
            }
            sends.push((pi, *pred, target));
        }
        if sends.len() == incoming.len() {
            candidates.push((bi, sends));
        }
    }
    if candidates.is_empty() {
        return 0;
    }
    let cfg = crate::ir::analysis::CfgAnalysis::build(func);
    let dominated_by = |x: usize, mut u: usize| loop {
        if u == x {
            return true;
        }
        let up = cfg.idom[u];
        if up == u || up == usize::MAX {
            return false;
        }
        u = up;
    };
    // Where B's other phis are read: each in the region of one target,
    // which must be reached only from B. Returns the (target, phi) pairs.
    let reads = |bi: usize| -> Option<FxHashSet<(usize, u32)>> {
        let b = func.blocks[bi].label;
        let Terminator::CondBranch { true_label: t, false_label: f, .. } = func.blocks[bi].terminator else { return None };
        let (ti, fi) = (label_to_idx[&t], label_to_idx[&f]);
        let is_b_phi = |v: u32| func.blocks[bi].instructions.iter().any(|i| i.dest().is_some_and(|d| d.0 == v));
        let region = |u: usize| if dominated_by(ti, u) { Some(ti) } else if dominated_by(fi, u) { Some(fi) } else { None };
        let mut needed: FxHashSet<(usize, u32)> = FxHashSet::default();
        let mut ok = true;
        for (ui, block) in func.blocks.iter().enumerate() {
            for inst in &block.instructions {
                if let Instruction::Phi { incoming, .. } = inst {
                    for (op, l) in incoming {
                        let Operand::Value(v) = op else { continue };
                        // Read at the end of the predecessor (B itself for a target's phi).
                        if !is_b_phi(v.0) || *l == b {
                            continue;
                        }
                        match label_to_idx.get(l).and_then(|&w| region(w)) {
                            Some(r) if ui != bi => { needed.insert((r, v.0)); }
                            _ => return None,
                        }
                    }
                } else {
                    inst.for_each_used_value(|v| {
                        if is_b_phi(v) {
                            match region(ui) {
                                Some(r) => { needed.insert((r, v)); }
                                None => ok = false,
                            }
                        }
                    });
                }
            }
            if ui != bi {
                block.terminator.for_each_used_value(|v| {
                    if is_b_phi(v) {
                        match region(ui) {
                            Some(r) => { needed.insert((r, v)); }
                            None => ok = false,
                        }
                    }
                });
            }
        }
        let single = needed.iter().all(|&(r, _)| pred_count.get(&func.blocks[r].label) == Some(&1));
        (ok && single).then_some(needed)
    };
    let Some((bi, sends, needed)) = candidates.into_iter().find_map(|(bi, sends)| reads(bi).map(|n| (bi, sends, n)))
    else { return 0 };

    let b = func.blocks[bi].label;
    let Terminator::CondBranch { true_label: t, false_label: f, .. } = func.blocks[bi].terminator else { return 0 };
    // B's phis: (dest, type, incoming).
    type Merged = (Value, crate::common::types::IrType, Vec<(Operand, BlockId)>);
    let phis: Vec<Merged> = func.blocks[bi].instructions.iter()
        .filter_map(|i| match i {
            Instruction::Phi { dest, ty, incoming } => Some((*dest, *ty, incoming.clone())),
            _ => None,
        })
        .collect();
    let is_b_phi = |v: u32| phis.iter().any(|(d, _, _)| d.0 == v);
    let (ti, fi) = (label_to_idx[&t], label_to_idx[&f]);

    // Redirect the predecessors.
    for &(pi, _, target) in &sends {
        let remap: FxHashMap<BlockId, BlockId> = [(b, target)].into_iter().collect();
        remap_terminator_targets(&mut func.blocks[pi].terminator, &remap);
    }
    let incoming_of = |v: Value, p: BlockId| -> Option<Operand> {
        phis.iter().find(|(d, _, _)| *d == v).and_then(|(_, _, inc)| inc.iter().find(|(_, l)| *l == p).map(|(op, _)| *op))
    };
    let mut next_id = func.next_value_id.max(func.max_value_id() + 1);
    for (target_idx, target) in [(ti, t), (fi, f)] {
        let preds: Vec<BlockId> = sends.iter().filter(|s| s.2 == target).map(|s| s.1).collect();
        // The target's own phis: B's entry becomes one per predecessor.
        for inst in &mut func.blocks[target_idx].instructions {
            if let Instruction::Phi { incoming, .. } = inst {
                if let Some(k) = incoming.iter().position(|(_, l)| *l == b) {
                    let (op, _) = incoming.remove(k);
                    for &p in &preds {
                        let mapped = match op {
                            Operand::Value(v) if is_b_phi(v.0) => incoming_of(v, p).unwrap_or(op),
                            _ => op,
                        };
                        incoming.push((mapped, p));
                    }
                }
            }
        }
        if preds.is_empty() {
            continue;
        }
        // B's phis read in the region: merged again at its start.
        let mut map: Vec<Option<Operand>> = Vec::new();
        let mut new_phis = Vec::new();
        for (d, ty, _) in &phis {
            if !needed.contains(&(target_idx, d.0)) {
                continue;
            }
            let Some(inc) = preds.iter().map(|&p| incoming_of(*d, p).map(|op| (op, p))).collect::<Option<Vec<_>>>() else { continue };
            let nv = Value(next_id);
            next_id += 1;
            if map.len() <= d.0 as usize {
                map.resize(d.0 as usize + 1, None);
            }
            map[d.0 as usize] = Some(Operand::Value(nv));
            new_phis.push(Instruction::Phi { dest: nv, ty: *ty, incoming: inc });
        }
        if map.is_empty() {
            continue;
        }
        for (ui, block) in func.blocks.iter_mut().enumerate() {
            if ui == bi {
                continue;
            }
            for inst in &mut block.instructions {
                match inst {
                    Instruction::Phi { incoming, .. } => {
                        for (op, l) in incoming.iter_mut() {
                            let in_region = label_to_idx.get(l).is_some_and(|&w| dominated_by(target_idx, w));
                            if in_region {
                                if let Operand::Value(v) = op {
                                    if let Some(Some(r)) = map.get(v.0 as usize) {
                                        *op = *r;
                                    }
                                }
                            }
                        }
                    }
                    _ if dominated_by(target_idx, ui) => {
                        super::copy_prop::replace_operands_in_instruction(inst, &map);
                    }
                    _ => {}
                }
            }
            if dominated_by(target_idx, ui) {
                super::copy_prop::replace_operands_in_terminator(&mut block.terminator, &map);
            }
        }
        let tb = &mut func.blocks[target_idx];
        if !tb.source_spans.is_empty() {
            let span = tb.source_spans[0];
            tb.source_spans.splice(0..0, std::iter::repeat_n(span, new_phis.len()));
        }
        tb.instructions.splice(0..0, new_phis);
    }
    func.next_value_id = next_id;
    1
}

// ---------------------------------------------------------------------------
// Sub-pass: range tests
// ---------------------------------------------------------------------------

/// `c >= 'a' && c <= 'z'` left as two branches (each operand of `&&`
/// branches on its own):
///
///   A: %p = cmp sge %x, 97; condbr %p, X, F      A: %t = sub %x, 97
///   X: %q = cmp sle %x, 122; condbr %q, T, F  =>    %c = cmp ule %t, 25
///                                                   condbr %c, T, F
///
/// One compare and one branch: x - lo, wrapping, is at most hi - lo exactly
/// when lo <= x <= hi. Either test may be negated (its block reaching F when
/// the compare holds). X must do nothing else and be reached only from A,
/// and F must take the same phi values from both.
fn fuse_range_tests(func: &mut IrFunction, label_to_idx: &FxHashMap<BlockId, usize>) -> usize {
    #[derive(Clone, Copy)]
    enum Bound {
        Lo(i128),
        Hi(i128),
    }
    // The bound `cmp` puts on its value when it holds (or fails, if negated),
    // and whether the comparison is signed.
    fn bound(op: IrCmpOp, ty: IrType, k: i64, x_left: bool, negate: bool) -> Option<(Bound, bool)> {
        use IrCmpOp::*;
        let bits = ty.size() as u32 * 8;
        let (signed, k) = match op {
            Slt | Sle | Sgt | Sge => (true, if bits == 64 { k as i128 } else { k as i32 as i128 }),
            Ult | Ule | Ugt | Uge => (false, if bits == 64 { k as u64 as i128 } else { k as u32 as i128 }),
            _ => return None,
        };
        // Normalize to `x op k`.
        let mut op = if x_left { op } else {
            match op { Slt => Sgt, Sle => Sge, Sgt => Slt, Sge => Sle, Ult => Ugt, Ule => Uge, Ugt => Ult, _ => Ule }
        };
        if negate {
            op = match op { Slt => Sge, Sle => Sgt, Sgt => Sle, Sge => Slt, Ult => Uge, Ule => Ugt, Ugt => Ule, _ => Ult };
        }
        let b = match op {
            Sge | Uge => Bound::Lo(k),
            Sgt | Ugt => Bound::Lo(k + 1),
            Sle | Ule => Bound::Hi(k),
            _ => Bound::Hi(k - 1),
        };
        Some((b, signed))
    }
    let mut use_count: FxHashMap<u32, u32> = FxHashMap::default();
    for block in &func.blocks {
        for inst in &block.instructions {
            inst.for_each_used_value(|id| *use_count.entry(id).or_insert(0) += 1);
        }
        block.terminator.for_each_used_value(|id| *use_count.entry(id).or_insert(0) += 1);
    }
    let (pred_count, _) = build_pred_info(func);
    // The compare a block ends with and branches on: (x, op, k, x on the left, ty).
    let test = |block: &BasicBlock| -> Option<(Value, IrCmpOp, i64, bool, IrType, Value)> {
        let Terminator::CondBranch { cond: Operand::Value(c), .. } = block.terminator else { return None };
        let Some(Instruction::Cmp { dest, op, lhs, rhs, ty }) = block.instructions.last() else { return None };
        if *dest != c || use_count.get(&c.0) != Some(&1) || !matches!(ty, IrType::I32 | IrType::U32 | IrType::I64 | IrType::U64) {
            return None;
        }
        match (lhs, rhs) {
            (Operand::Value(x), Operand::Const(k)) => Some((*x, *op, k.to_i64()?, true, *ty, c)),
            (Operand::Const(k), Operand::Value(x)) => Some((*x, *op, k.to_i64()?, false, *ty, c)),
            _ => None,
        }
    };

    let mut fused = 0;
    for ai in 0..func.blocks.len() {
        let a = &func.blocks[ai];
        let Terminator::CondBranch { true_label: at, false_label: af, .. } = a.terminator else { continue };
        let Some((x, op1, k1, left1, ty, c1)) = test(a) else { continue };
        // A continues to X on one side and gives up to F on the other.
        for (xl, f, neg1) in [(at, af, false), (af, at, true)] {
            let Some(&xi) = label_to_idx.get(&xl) else { continue };
            let xb = &func.blocks[xi];
            if xi == ai || xb.instructions.len() != 1 || pred_count.get(&xl) != Some(&1) {
                continue;
            }
            let Some((x2, op2, k2, left2, ty2, _)) = test(xb) else { continue };
            let Terminator::CondBranch { true_label: xt, false_label: xf, .. } = xb.terminator else { continue };
            let (t, neg2) = if xf == f { (xt, false) } else if xt == f { (xf, true) } else { continue };
            if x2 != x || ty2.size() != ty.size() || t == f || t == xl {
                continue;
            }
            let (Some((b1, s1)), Some((b2, s2))) = (bound(op1, ty, k1, left1, neg1), bound(op2, ty2, k2, left2, neg2)) else { continue };
            let (lo, hi) = match (b1, b2) {
                (Bound::Lo(l), Bound::Hi(h)) | (Bound::Hi(h), Bound::Lo(l)) => (l, h),
                _ => continue,
            };
            if s1 != s2 || lo > hi {
                continue;
            }
            // F's phis must not tell A's edge from X's.
            let Some(&fi) = label_to_idx.get(&f) else { continue };
            let (al, xlab) = (func.blocks[ai].label, xl);
            let same = func.blocks[fi].instructions.iter().all(|i| match i {
                Instruction::Phi { incoming, .. } => {
                    let from = |l: BlockId| incoming.iter().find(|(_, p)| *p == l).map(|(op, _)| *op);
                    match (from(al), from(xlab)) {
                        (Some(p), Some(q)) => operands_equal(&p, &q),
                        (p, q) => p.is_none() && q.is_none(),
                    }
                }
                _ => true,
            });
            if !same {
                continue;
            }
            let uty = ty.to_unsigned();
            let mut next_id = func.next_value_id.max(func.max_value_id() + 1);
            let tv = Value(next_id);
            next_id += 1;
            func.next_value_id = next_id;
            let ab = &mut func.blocks[ai];
            let k = ab.instructions.len() - 1;
            // x + 5 rather than x - (-5), which would build the constant.
            let (op, k_lo) = if lo < 0 {
                (crate::ir::reexports::IrBinOp::Add, (-lo) as i64)
            } else {
                (crate::ir::reexports::IrBinOp::Sub, lo as i64)
            };
            ab.instructions[k] = Instruction::BinOp {
                dest: tv, op, lhs: Operand::Value(x), rhs: Operand::Const(IrConst::from_i64(k_lo, uty)), ty: uty,
            };
            ab.instructions.push(Instruction::Cmp {
                dest: c1, op: IrCmpOp::Ule, lhs: Operand::Value(tv),
                rhs: Operand::Const(IrConst::from_i64((hi - lo) as i64, uty)), ty: uty,
            });
            if !ab.source_spans.is_empty() {
                let span = ab.source_spans[ab.source_spans.len() - 1];
                ab.source_spans.push(span);
            }
            ab.terminator = Terminator::CondBranch { cond: Operand::Value(c1), true_label: t, false_label: f };
            // T is now entered from A instead of X.
            if let Some(&ti) = label_to_idx.get(&t) {
                for inst in &mut func.blocks[ti].instructions {
                    if let Instruction::Phi { incoming, .. } = inst {
                        for (_, l) in incoming.iter_mut() {
                            if *l == xlab {
                                *l = al;
                            }
                        }
                    }
                }
            }
            fused += 1;
            // Block indices are unchanged, but use counts and predecessor
            // counts are stale: one fusion per call.
            return fused;
        }
    }
    fused
}

// ---------------------------------------------------------------------------
// Sub-pass: redundant conditional branches
// ---------------------------------------------------------------------------

/// Convert `CondBranch { cond, true_label: X, false_label: X }` to `Branch(X)`.
/// The condition is dead and will be cleaned up by DCE.
fn simplify_redundant_cond_branches(func: &mut IrFunction) -> usize {
    let mut count = 0;
    for block in &mut func.blocks {
        if let Terminator::CondBranch { true_label, false_label, .. } = &block.terminator {
            if true_label == false_label {
                let target = *true_label;
                block.terminator = Terminator::Branch(target);
                count += 1;
            }
        }
    }
    count
}

// ---------------------------------------------------------------------------
// Sub-pass: constant branch folding
// ---------------------------------------------------------------------------

/// Fold `CondBranch` with a known-constant condition into an unconditional `Branch`.
///
/// After constant folding + copy propagation, a CondBranch may have a constant
/// condition (e.g., `CondBranch { cond: Const(1), true_label, false_label }`).
/// This arises in switch(sizeof(T)) patterns where the dispatch comparisons
/// fold to constants. Converting these to unconditional branches enables dead
/// block removal to eliminate the unreachable switch cases.
///
/// When folding, we must clean up phi nodes in the not-taken target block:
/// the phi entries referencing the current block must be removed since the edge
/// no longer exists. Without this cleanup, stale phi entries can cause
/// miscompilation when the not-taken block is still reachable from other paths.
fn fold_constant_cond_branches(func: &mut IrFunction, label_to_idx: &FxHashMap<BlockId, usize>) -> usize {
    // Build predecessor info for cross-block value resolution.
    let (_pred_count, single_pred) = build_pred_info(func);

    // Build a global value definition map for cross-block resolution.
    let global_val_map = build_global_value_map(func);

    // First pass: collect the folding decisions.
    // Each entry: (block_index, taken_target, not_taken_target, block_label)
    let mut folds: Vec<(usize, BlockId, BlockId, BlockId)> = Vec::new();

    for (idx, block) in func.blocks.iter().enumerate() {
        if let Terminator::CondBranch { cond, true_label, false_label } = &block.terminator {
            let const_val = match cond {
                Operand::Const(c) => Some(c.is_nonzero()),
                Operand::Value(v) => {
                    resolve_cond_branch_value(func, block, *v, &single_pred, label_to_idx, &global_val_map)
                        .map(|c| c.is_nonzero())
                }
            };
            if let Some(is_true) = const_val {
                let taken = if is_true { *true_label } else { *false_label };
                let not_taken = if is_true { *false_label } else { *true_label };
                folds.push((idx, taken, not_taken, block.label));
            }
        }
    }

    if folds.is_empty() {
        return 0;
    }

    let count = folds.len();

    // Apply the folds and clean up phi nodes.
    for &(idx, taken, not_taken, block_label) in &folds {
        func.blocks[idx].terminator = Terminator::Branch(taken);
        // Remove phi entries in the not-taken target (if it differs from taken).
        if taken != not_taken {
            if let Some(&block_idx) = label_to_idx.get(&not_taken) {
                remove_phi_entries_from(&mut func.blocks[block_idx], block_label);
            }
        }
    }

    count
}

/// Try to resolve a CondBranch condition value to a constant.
///
/// Walks through Copy/Phi/Cmp/Select chains in the current block, then up the
/// single-predecessor chain, then globally across blocks.
fn resolve_cond_branch_value(
    func: &IrFunction,
    block: &BasicBlock,
    v: Value,
    single_pred: &FxHashMap<BlockId, BlockId>,
    label_to_idx: &FxHashMap<BlockId, usize>,
    global_val_map: &FxHashMap<Value, (usize, usize)>,
) -> Option<IrConst> {
    // Try local resolution first.
    if let Some(c) = resolve_value_to_const_in_block(block, v) {
        return Some(c);
    }

    // Walk the single-predecessor chain (blocks with exactly one predecessor
    // that unconditionally branches here).
    let mut current_label = block.label;
    for _ in 0..MAX_PRED_CHAIN_DEPTH {
        let pred_label = *single_pred.get(&current_label)?;
        let &pred_idx = label_to_idx.get(&pred_label)?;
        let pred_block = &func.blocks[pred_idx];
        if !matches!(&pred_block.terminator, Terminator::Branch(t) if *t == current_label) {
            break;
        }
        if let Some(c) = resolve_value_to_const_in_block(pred_block, v) {
            return Some(c);
        }
        current_label = pred_label;
    }

    // Global cross-block resolution (needed for kernel's
    // `alternative_has_cap_unlikely()` pattern).
    resolve_value_globally(func, v, global_val_map, 0)
}

// ---------------------------------------------------------------------------
// Sub-pass: constant switch folding
// ---------------------------------------------------------------------------

/// Fold `Switch` with a known-constant value into an unconditional `Branch`.
///
/// After inlining + constant folding + copy propagation, a Switch may have a
/// constant value. This is critical for the Linux kernel's `cpucap_is_possible()`
/// pattern: a switch on a capability number that should resolve at compile time.
///
/// When folding, we clean up phi nodes in all not-taken target blocks.
fn fold_constant_switches(func: &mut IrFunction, label_to_idx: &FxHashMap<BlockId, usize>) -> usize {
    // Collect folding decisions: (block_index, taken_target, not_taken_targets, block_label)
    let mut folds: Vec<(usize, BlockId, Vec<BlockId>, BlockId)> = Vec::new();

    for (idx, block) in func.blocks.iter().enumerate() {
        if let Terminator::Switch { val, cases, default, .. } = &block.terminator {
            let resolved_const = match val {
                Operand::Const(c) => Some(*c),
                Operand::Value(v) => resolve_value_to_const_in_block(block, *v),
            };
            if let Some(c) = resolved_const {
                if let Some(switch_int) = c.to_i64() {
                    let taken = cases.iter()
                        .find(|(cv, _)| *cv == switch_int)
                        .map(|(_, label)| *label)
                        .unwrap_or(*default);

                    // Collect unique not-taken targets.
                    let mut not_taken = Vec::new();
                    if *default != taken && !not_taken.contains(default) {
                        not_taken.push(*default);
                    }
                    for (_, label) in cases {
                        if *label != taken && !not_taken.contains(label) {
                            not_taken.push(*label);
                        }
                    }

                    folds.push((idx, taken, not_taken, block.label));
                }
            }
        }
    }

    if folds.is_empty() {
        return 0;
    }

    let count = folds.len();

    // Apply folds and clean up phi nodes.
    for (idx, taken, ref not_taken, block_label) in &folds {
        func.blocks[*idx].terminator = Terminator::Branch(*taken);
        for nt_target in not_taken {
            if let Some(&block_idx) = label_to_idx.get(nt_target) {
                remove_phi_entries_from(&mut func.blocks[block_idx], *block_label);
            }
        }
    }

    count
}

// ---------------------------------------------------------------------------
// Sub-pass: jump chain threading
// ---------------------------------------------------------------------------

/// Thread jump chains: if a block branches to an empty forwarding block
/// (no instructions, terminates with unconditional Branch), redirect to
/// skip the intermediate block.
///
/// Special care: when threading would cause a CondBranch's true and false
/// targets to become the same block (both edges merge), AND the target has
/// phi nodes that carry different values from the two paths, we must NOT
/// thread. Otherwise the merge block's phi loses the ability to distinguish
/// the two control flow paths, causing miscompilation.
fn thread_jump_chains(func: &mut IrFunction, label_to_idx: &FxHashMap<BlockId, usize>) -> usize {
    // Build forwarding map: empty blocks that just branch unconditionally.
    let forwarding: FxHashMap<BlockId, BlockId> = func.blocks.iter()
        .filter(|block| {
            block.instructions.is_empty()
                && matches!(&block.terminator, Terminator::Branch(_))
        })
        .map(|block| {
            if let Terminator::Branch(target) = &block.terminator {
                (block.label, *target)
            } else {
                unreachable!("block was filtered to have Branch terminator")
            }
        })
        .collect();

    if forwarding.is_empty() {
        return 0;
    }

    // Resolve transitive chains with cycle detection.
    // resolved[B] = (final_target, immediate_predecessor_of_final)
    let resolved = resolve_forwarding_chains(&forwarding);
    if resolved.is_empty() {
        return 0;
    }

    // Collect the redirections we need to make.
    let redirections = collect_thread_redirections(func, &resolved, label_to_idx);
    if redirections.is_empty() {
        return 0;
    }

    // Apply the redirections.
    apply_thread_redirections(func, &redirections, label_to_idx)
}

/// Resolve transitive forwarding chains.
/// Returns a map: start_block -> (final_target, immediate_pred_of_final).
fn resolve_forwarding_chains(forwarding: &FxHashMap<BlockId, BlockId>) -> FxHashMap<BlockId, (BlockId, BlockId)> {
    let mut resolved = FxHashMap::default();
    for &start in forwarding.keys() {
        let mut prev = start;
        let mut current = start;
        let mut depth = 0;
        while let Some(&next) = forwarding.get(&current) {
            if next == start || depth > MAX_CHAIN_DEPTH {
                break;
            }
            prev = current;
            current = next;
            depth += 1;
        }
        if current != start {
            resolved.insert(start, (current, prev));
        }
    }
    resolved
}

/// Collect edge redirections for jump chain threading.
/// Returns: Vec<(block_idx, Vec<(old_target, new_target, phi_lookup_block)>)>
fn collect_thread_redirections(
    func: &IrFunction,
    resolved: &FxHashMap<BlockId, (BlockId, BlockId)>,
    label_to_idx: &FxHashMap<BlockId, usize>,
) -> Vec<(usize, Vec<(BlockId, BlockId, BlockId)>)> {
    let mut redirections = Vec::new();

    for block_idx in 0..func.blocks.len() {
        let mut edge_changes = Vec::new();
        let block_label = func.blocks[block_idx].label;

        match &func.blocks[block_idx].terminator {
            Terminator::Branch(target) => {
                if let Some(&(resolved_target, phi_block)) = resolved.get(target) {
                    edge_changes.push((*target, resolved_target, phi_block));
                }
            }
            Terminator::CondBranch { true_label, false_label, .. } => {
                let true_resolved = resolved.get(true_label).copied();
                let false_resolved = resolved.get(false_label).copied();
                let true_final = true_resolved.map(|(t, _)| t).unwrap_or(*true_label);
                let false_final = false_resolved.map(|(t, _)| t).unwrap_or(*false_label);

                if true_final == false_final && true_label != false_label {
                    // Threading would merge both edges — check for phi conflict.
                    if would_create_phi_conflict(func, block_label, *true_label,
                                                  *false_label, true_final, resolved,
                                                  label_to_idx) {
                        continue; // Skip: phi conflict
                    }
                }
                // Safe to thread (different final targets, or no phi conflict).
                if let Some((rt, rt_phi)) = true_resolved {
                    edge_changes.push((*true_label, rt, rt_phi));
                }
                if let Some((rf, rf_phi)) = false_resolved {
                    if !edge_changes.iter().any(|(old, new, _)| *old == *false_label && *new == rf) {
                        edge_changes.push((*false_label, rf, rf_phi));
                    }
                }
            }
            // TODO: IndirectBranch targets could also be threaded through
            // empty blocks, but computed goto is rare enough to skip for now.
            _ => {}
        }

        if !edge_changes.is_empty() {
            redirections.push((block_idx, edge_changes));
        }
    }

    redirections
}

/// Apply collected thread redirections: update terminators and phi nodes.
fn apply_thread_redirections(
    func: &mut IrFunction,
    redirections: &[(usize, Vec<(BlockId, BlockId, BlockId)>)],
    label_to_idx: &FxHashMap<BlockId, usize>,
) -> usize {
    let mut count = 0;
    for (block_idx, edge_changes) in redirections {
        let block_label = func.blocks[*block_idx].label;

        // Update the terminator.
        match &mut func.blocks[*block_idx].terminator {
            Terminator::Branch(target) => {
                for (old, new, _) in edge_changes {
                    if target == old { *target = *new; }
                }
            }
            Terminator::CondBranch { true_label, false_label, .. } => {
                for (old, new, _) in edge_changes {
                    if true_label == old { *true_label = *new; }
                    if false_label == old { *false_label = *new; }
                }
            }
            _ => {}
        }
        count += 1;

        // Update phi nodes in new target blocks.
        for (_old, new_target, phi_lookup_block) in edge_changes {
            if let Some(&target_idx) = label_to_idx.get(new_target) {
                let block = &mut func.blocks[target_idx];
                for inst in &mut block.instructions {
                    if let Instruction::Phi { incoming, .. } = inst {
                        let value_from_chain = incoming.iter()
                            .find(|(_, label)| *label == *phi_lookup_block)
                            .map(|(val, _)| *val);
                        if let Some(val) = value_from_chain {
                            if !incoming.iter().any(|(_, label)| *label == block_label) {
                                incoming.push((val, block_label));
                            }
                        }
                    }
                }
            }
        }
    }

    count
}

/// Check if threading a CondBranch's two edges to the same final target would
/// create a phi conflict — i.e., the target has phi nodes that carry different
/// values from the two paths.
fn would_create_phi_conflict(
    func: &IrFunction,
    block_label: BlockId,
    true_label: BlockId,
    false_label: BlockId,
    target: BlockId,
    resolved: &FxHashMap<BlockId, (BlockId, BlockId)>,
    label_to_idx: &FxHashMap<BlockId, usize>,
) -> bool {
    let target_block = match label_to_idx.get(&target) {
        Some(&idx) => &func.blocks[idx],
        None => return false,
    };

    // Determine the phi-lookup label for each path.
    let true_phi_label = resolved.get(&true_label).map(|&(_, pb)| pb).unwrap_or(block_label);
    let false_phi_label = resolved.get(&false_label).map(|&(_, pb)| pb).unwrap_or(block_label);

    if true_phi_label == false_phi_label {
        return false;
    }

    for inst in &target_block.instructions {
        if let Instruction::Phi { incoming, .. } = inst {
            let true_value = incoming.iter().find(|(_, l)| *l == true_phi_label).map(|(v, _)| v);
            let false_value = incoming.iter().find(|(_, l)| *l == false_phi_label).map(|(v, _)| v);
            if let (Some(tv), Some(fv)) = (true_value, false_value) {
                if !operands_equal(tv, fv) {
                    return true;
                }
            }
        }
    }

    false
}

// ---------------------------------------------------------------------------
// Sub-pass: dead block elimination
// ---------------------------------------------------------------------------

/// Remove blocks that have no predecessors (except the entry block, blocks[0]).
/// Returns the number of blocks removed.
fn remove_dead_blocks(func: &mut IrFunction, label_to_idx: &FxHashMap<BlockId, usize>) -> usize {
    if func.blocks.len() <= 1 {
        return 0;
    }

    // Compute the set of blocks reachable from the entry block via BFS.
    let entry = func.blocks[0].label;
    let mut reachable = FxHashSet::default();
    reachable.insert(entry);
    let mut worklist = vec![entry];

    // Blocks referenced by static local initializers (&&label in global data).
    for &block_id in &func.global_init_label_blocks {
        if reachable.insert(block_id) {
            worklist.push(block_id);
        }
    }

    while let Some(block_id) = worklist.pop() {
        if let Some(&idx) = label_to_idx.get(&block_id) {
            for_each_terminator_target(&func.blocks[idx].terminator, |target| {
                if reachable.insert(target) {
                    worklist.push(target);
                }
            });
            for inst in &func.blocks[idx].instructions {
                if let Instruction::LabelAddr { label, .. } = inst {
                    if reachable.insert(*label) {
                        worklist.push(*label);
                    }
                }
                // Always mark asm goto target blocks as reachable.
                // The backend always emits the asm body (using $0 placeholders
                // for unsatisfiable immediate constraints), so goto target labels
                // must exist in the assembly output. Removing them would cause
                // linker errors when the asm template references %l[label].
                if let Instruction::InlineAsm { goto_labels, .. } = inst {
                    for (_, label) in goto_labels {
                        if reachable.insert(*label) {
                            worklist.push(*label);
                        }
                    }
                }
            }
        }
    }

    let dead_blocks: FxHashSet<BlockId> = func.blocks.iter()
        .map(|b| b.label)
        .filter(|label| !reachable.contains(label))
        .collect();

    if dead_blocks.is_empty() {
        return 0;
    }

    // Clean up phi nodes and InlineAsm goto_labels in reachable blocks.
    for block in &mut func.blocks {
        if !reachable.contains(&block.label) {
            continue;
        }
        remove_phi_entries_from_set(block, &dead_blocks);
        // Defensive: clean up InlineAsm goto_labels referencing dead blocks.
        for inst in &mut block.instructions {
            if let Instruction::InlineAsm { goto_labels, .. } = inst {
                goto_labels.retain(|(_, label)| !dead_blocks.contains(label));
            }
        }
    }

    let original_len = func.blocks.len();
    func.blocks.retain(|b| reachable.contains(&b.label));
    original_len - func.blocks.len()
}

// ---------------------------------------------------------------------------
// Sub-pass: trivial phi simplification
// ---------------------------------------------------------------------------

/// Simplify trivial phi nodes: phi nodes with exactly one incoming edge,
/// or where all incoming values are identical, are replaced with Copy.
///
/// This is critical for patterns like `if (1 || expr)` where the `||`
/// short-circuit generates a phi that merges two paths, but after constant
/// branch folding removes the dead path, the phi has only one incoming
/// edge remaining.
fn simplify_trivial_phis(func: &mut IrFunction) -> usize {
    let mut count = 0;
    for block in &mut func.blocks {
        for inst in &mut block.instructions {
            if let Instruction::Phi { dest, incoming, .. } = inst {
                let replacement = if incoming.len() == 1 {
                    Some(incoming[0].0)
                } else if incoming.len() > 1 {
                    let first = &incoming[0].0;
                    if incoming.iter().all(|(val, _)| operands_equal(val, first)) {
                        Some(*first)
                    } else {
                        None
                    }
                } else {
                    None
                };
                if let Some(src) = replacement {
                    *inst = Instruction::Copy { dest: *dest, src };
                    count += 1;
                }
            }
        }
    }
    count
}

// ---------------------------------------------------------------------------
// Sub-pass: single-predecessor block merging
// ---------------------------------------------------------------------------

/// Merge single-predecessor blocks into their predecessor.
///
/// When block A ends with `Branch(B)` and B has exactly one predecessor (A),
/// the two blocks can be fused: B's instructions are appended to A and A
/// inherits B's terminator.
///
/// This is critical for stack frame reduction after inlining: each inlined
/// function body creates a chain of blocks connected by unconditional branches.
/// Without fusion, SSA values crossing these artificial block boundaries are
/// classified as multi-block and each gets a permanent stack slot (8 bytes).
fn merge_single_pred_blocks(func: &mut IrFunction) -> usize {
    if func.blocks.len() <= 1 {
        return 0;
    }

    let (pred_count, _) = build_pred_info(func);
    let label_to_idx = build_label_to_idx(func);

    // Precompute sets of blocks that must keep their identity:
    // - blocks referenced by LabelAddr (computed goto targets)
    // - blocks referenced by InlineAsm goto_labels
    // - blocks referenced by global_init_label_blocks
    let (label_addr_targets, asm_goto_targets) = collect_unmergeable_targets(func);

    let fusions = find_fusion_candidates(
        func, &pred_count, &label_to_idx,
        &label_addr_targets, &asm_goto_targets,
    );

    if fusions.is_empty() {
        return 0;
    }

    let count = fusions.len();
    execute_fusions(func, &fusions);
    patch_absorbed_references(func, &fusions);
    count
}

/// Precompute sets of blocks that cannot be merged because they are referenced
/// by LabelAddr instructions or InlineAsm goto_labels across the function.
///
/// Computing these once avoids O(blocks * instructions) scans per fusion candidate.
fn collect_unmergeable_targets(func: &IrFunction) -> (FxHashSet<BlockId>, FxHashSet<BlockId>) {
    let mut label_addr_targets = FxHashSet::default();
    let mut asm_goto_targets = FxHashSet::default();

    for block in &func.blocks {
        for inst in &block.instructions {
            if let Instruction::LabelAddr { label, .. } = inst {
                label_addr_targets.insert(*label);
            }
            if let Instruction::InlineAsm { goto_labels, .. } = inst {
                for (_, label) in goto_labels {
                    asm_goto_targets.insert(*label);
                }
            }
        }
    }

    (label_addr_targets, asm_goto_targets)
}

/// Find (pred_idx, succ_idx) pairs where the predecessor can absorb the successor.
fn find_fusion_candidates(
    func: &IrFunction,
    pred_count: &FxHashMap<BlockId, u32>,
    label_to_idx: &FxHashMap<BlockId, usize>,
    label_addr_targets: &FxHashSet<BlockId>,
    asm_goto_targets: &FxHashSet<BlockId>,
) -> Vec<(usize, usize)> {
    let entry_label = func.blocks[0].label;
    let mut fusions = Vec::new();
    let mut absorbed: FxHashSet<usize> = FxHashSet::default();
    let mut absorbers: FxHashSet<usize> = FxHashSet::default();

    for (idx, block) in func.blocks.iter().enumerate() {
        let target = match &block.terminator {
            Terminator::Branch(t) => t,
            _ => continue,
        };

        if *target == entry_label { continue; }
        if pred_count.get(target).copied().unwrap_or(0) != 1 { continue; }

        let &succ_idx = match label_to_idx.get(target) {
            Some(si) => si,
            None => continue,
        };

        if idx == succ_idx { continue; }
        if absorbed.contains(&succ_idx) || absorbers.contains(&succ_idx) { continue; }
        if absorbed.contains(&idx) { continue; }

        // Successor contains InlineAsm with goto_labels — must stay separate.
        let has_asm_goto = func.blocks[succ_idx].instructions.iter().any(|inst| {
            matches!(inst, Instruction::InlineAsm { goto_labels, .. } if !goto_labels.is_empty())
        });
        if has_asm_goto { continue; }

        // Target is referenced by LabelAddr or asm goto — must keep identity.
        if label_addr_targets.contains(target) { continue; }
        if asm_goto_targets.contains(target) { continue; }

        // Target is referenced by global init (&&label in global data).
        if func.global_init_label_blocks.contains(target) { continue; }

        fusions.push((idx, succ_idx));
        absorbed.insert(succ_idx);
        absorbers.insert(idx);
    }

    fusions
}

/// Execute fusions: move successor instructions/terminator into predecessor.
fn execute_fusions(func: &mut IrFunction, fusions: &[(usize, usize)]) {
    for &(pred_idx, succ_idx) in fusions {
        let succ_instructions = std::mem::take(&mut func.blocks[succ_idx].instructions);
        let succ_terminator = std::mem::replace(
            &mut func.blocks[succ_idx].terminator,
            Terminator::Unreachable,
        );
        let succ_spans = std::mem::take(&mut func.blocks[succ_idx].source_spans);
        let pred_label = func.blocks[pred_idx].label;

        // Convert phi nodes to Copy (single-predecessor, so each Phi has one entry).
        let mut converted_instructions = Vec::with_capacity(succ_instructions.len());
        let mut converted_spans = Vec::with_capacity(succ_spans.len());

        for (i, inst) in succ_instructions.into_iter().enumerate() {
            if let Instruction::Phi { dest, incoming, .. } = &inst {
                let src = incoming.iter()
                    .find(|(_, label)| *label == pred_label)
                    .map(|(op, _)| *op)
                    .unwrap_or_else(|| {
                        // Fallback: single-pred blocks should always have a matching entry.
                        debug_assert!(!incoming.is_empty(), "Phi with no incoming in single-pred block");
                        if !incoming.is_empty() { incoming[0].0 } else { Operand::Const(IrConst::I64(0)) }
                    });
                converted_instructions.push(Instruction::Copy { dest: *dest, src });
            } else {
                converted_instructions.push(inst);
            }
            if i < succ_spans.len() {
                converted_spans.push(succ_spans[i]);
            }
        }

        // Maintain source_spans invariant: either empty or parallel to instructions.
        let pred_has_spans = !func.blocks[pred_idx].source_spans.is_empty();
        let succ_has_spans = !converted_spans.is_empty();

        if pred_has_spans && !succ_has_spans && !converted_instructions.is_empty() {
            converted_spans.resize(converted_instructions.len(), crate::common::source::Span::dummy());
        } else if !pred_has_spans && succ_has_spans {
            let pred_inst_len = func.blocks[pred_idx].instructions.len();
            func.blocks[pred_idx].source_spans.resize(
                pred_inst_len,
                crate::common::source::Span::dummy(),
            );
        }

        func.blocks[pred_idx].instructions.extend(converted_instructions);
        func.blocks[pred_idx].terminator = succ_terminator;
        if !converted_spans.is_empty() {
            func.blocks[pred_idx].source_spans.extend(converted_spans);
        }
    }
}

/// After fusing blocks, update phi nodes and terminators in all blocks to
/// replace references to absorbed block labels with their predecessor labels.
fn patch_absorbed_references(func: &mut IrFunction, fusions: &[(usize, usize)]) {
    let absorbed_to_pred: FxHashMap<BlockId, BlockId> = fusions.iter()
        .map(|&(pred_idx, succ_idx)| (func.blocks[succ_idx].label, func.blocks[pred_idx].label))
        .collect();

    for block in &mut func.blocks {
        for inst in &mut block.instructions {
            if let Instruction::Phi { incoming, .. } = inst {
                for (_, label) in incoming.iter_mut() {
                    if let Some(&new_label) = absorbed_to_pred.get(label) {
                        *label = new_label;
                    }
                }
            }
        }
        remap_terminator_targets(&mut block.terminator, &absorbed_to_pred);
    }
}

// ---------------------------------------------------------------------------
// Value resolution helpers
// ---------------------------------------------------------------------------

/// Build a map from Value -> (block_index, instruction_index) for the entire function.
fn build_global_value_map(func: &IrFunction) -> FxHashMap<Value, (usize, usize)> {
    let mut map = FxHashMap::default();
    for (bi, block) in func.blocks.iter().enumerate() {
        for (ii, inst) in block.instructions.iter().enumerate() {
            let dest = match inst {
                Instruction::Copy { dest, .. } |
                Instruction::Phi { dest, .. } |
                Instruction::Cmp { dest, .. } |
                Instruction::Cast { dest, .. } |
                Instruction::Select { dest, .. } |
                Instruction::BinOp { dest, .. } |
                Instruction::UnaryOp { dest, .. } => Some(*dest),
                _ => None,
            };
            if let Some(d) = dest {
                map.insert(d, (bi, ii));
            }
        }
    }
    map
}

/// Resolve a Value to a constant by following its definition chain across blocks.
///
/// This handles chains like: Phi → Copy → Cmp(Ne, Cast(Phi), 0) where the inner
/// Phi has collapsed to a constant but the outer chain spans multiple blocks.
fn resolve_value_globally(func: &IrFunction, v: Value, val_map: &FxHashMap<Value, (usize, usize)>, depth: usize) -> Option<IrConst> {
    if depth > MAX_GLOBAL_RESOLVE_DEPTH {
        return None;
    }
    let &(bi, ii) = val_map.get(&v)?;
    let inst = &func.blocks[bi].instructions[ii];
    match inst {
        Instruction::Copy { src: Operand::Const(c), .. } => Some(*c),
        Instruction::Copy { src: Operand::Value(sv), .. } => {
            resolve_value_globally(func, *sv, val_map, depth + 1)
        }
        Instruction::Phi { incoming, .. } => {
            // All incoming must be the same constant.
            let mut common_val: Option<i64> = None;
            let mut first_const: Option<IrConst> = None;
            for (op, _) in incoming {
                let c = match op {
                    Operand::Const(c) => Some(*c),
                    Operand::Value(pv) => resolve_value_globally(func, *pv, val_map, depth + 1),
                };
                match c {
                    Some(c) => {
                        let ci = c.to_i64()?;
                        if let Some(prev) = common_val {
                            if prev != ci { return None; }
                        } else {
                            common_val = Some(ci);
                            first_const = Some(c);
                        }
                    }
                    None => return None,
                }
            }
            first_const
        }
        Instruction::Cmp { op, lhs, rhs, ty, .. } => {
            let l = resolve_operand_globally(func, lhs, val_map, depth + 1)?;
            let r = resolve_operand_globally(func, rhs, val_map, depth + 1)?;
            let result = op.eval_i64(ty.truncate_i64(l), ty.truncate_i64(r));
            Some(IrConst::I32(if result { 1 } else { 0 }))
        }
        Instruction::Cast { src: Operand::Const(c), .. } => Some(*c),
        Instruction::Cast { src: Operand::Value(sv), .. } => {
            resolve_value_globally(func, *sv, val_map, depth + 1)
        }
        Instruction::Select { cond, true_val, false_val, .. } => {
            let cond_val = resolve_operand_globally(func, cond, val_map, depth + 1)?;
            let chosen = if cond_val != 0 { true_val } else { false_val };
            match chosen {
                Operand::Const(c) => Some(*c),
                Operand::Value(cv) => resolve_value_globally(func, *cv, val_map, depth + 1),
            }
        }
        _ => None,
    }
}

/// Resolve an operand to an i64 constant using global cross-block resolution.
fn resolve_operand_globally(func: &IrFunction, op: &Operand, val_map: &FxHashMap<Value, (usize, usize)>, depth: usize) -> Option<i64> {
    match op {
        Operand::Const(c) => c.to_i64(),
        Operand::Value(v) => resolve_value_globally(func, *v, val_map, depth)?.to_i64(),
    }
}

/// Look through Copy, Phi, Cmp, and Select instructions in a block to resolve
/// a Value to a constant. This allows constant branch/switch folding to see
/// through instructions created by previous simplifications within the same
/// cfg_simplify fixpoint loop, without waiting for a separate copy_prop pass.
fn resolve_value_to_const_in_block(block: &BasicBlock, v: Value) -> Option<IrConst> {
    for inst in &block.instructions {
        match inst {
            Instruction::Copy { dest, src: Operand::Const(c) } if *dest == v => {
                return Some(*c);
            }
            Instruction::Phi { dest, incoming, .. } if *dest == v => {
                // Check if all incoming values are the same constant.
                let mut common_val: Option<i64> = None;
                let mut first_const: Option<IrConst> = None;
                for (op, _) in incoming {
                    match op {
                        Operand::Const(c) => {
                            let ci = c.to_i64()?;
                            if let Some(prev) = common_val {
                                if prev != ci { return None; }
                            } else {
                                common_val = Some(ci);
                                first_const = Some(*c);
                            }
                        }
                        _ => return None,
                    }
                }
                return first_const;
            }
            Instruction::Cmp { dest, op, lhs, rhs, ty } if *dest == v => {
                let l = resolve_operand_to_i64_in_block(block, lhs)?;
                let r = resolve_operand_to_i64_in_block(block, rhs)?;
                let result = op.eval_i64(ty.truncate_i64(l), ty.truncate_i64(r));
                return Some(IrConst::I32(if result { 1 } else { 0 }));
            }
            Instruction::Select { dest, cond, true_val, false_val, .. } if *dest == v => {
                let cond_const = resolve_operand_to_i64_in_block(block, cond)?;
                let chosen = if cond_const != 0 { true_val } else { false_val };
                return match chosen {
                    Operand::Const(c) => Some(*c),
                    Operand::Value(cv) => resolve_value_to_const_in_block(block, *cv),
                };
            }
            _ => {}
        }
    }
    None
}

/// Resolve an operand to an i64 constant within a single block.
fn resolve_operand_to_i64_in_block(block: &BasicBlock, op: &Operand) -> Option<i64> {
    match op {
        Operand::Const(c) => c.to_i64(),
        Operand::Value(v) => resolve_value_to_const_in_block(block, *v)?.to_i64(),
    }
}

/// Compare two operands for structural equality.
fn operands_equal(a: &Operand, b: &Operand) -> bool {
    match (a, b) {
        (Operand::Value(v1), Operand::Value(v2)) => v1.0 == v2.0,
        (Operand::Const(c1), Operand::Const(c2)) => consts_equal_for_phi(c1, c2),
        _ => false,
    }
}

/// Compare two IR constants for phi simplification.
/// Integer constants of different widths but same numeric value are considered
/// equal (e.g., I32(0) == I64(0)).
fn consts_equal_for_phi(a: &IrConst, b: &IrConst) -> bool {
    if a.to_hash_key() == b.to_hash_key() {
        return true;
    }
    match (a.to_i64(), b.to_i64()) {
        (Some(va), Some(vb)) => va == vb,
        _ => false,
    }
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use super::*;

    /// Create a basic block for testing. Reduces boilerplate since
    /// `source_spans` is always empty in tests.
    fn make_block(label: BlockId, instructions: Vec<Instruction>, terminator: Terminator) -> BasicBlock {
        BasicBlock { label, instructions, terminator, source_spans: Vec::new() }
    }

    #[test]
    fn test_redundant_cond_branch() {
        let mut func = IrFunction::new("test".to_string(), IrType::Void, vec![], false);
        func.blocks.push(make_block(
            BlockId(0),
            vec![Instruction::Copy { dest: Value(0), src: Operand::Const(IrConst::I32(1)) }],
            Terminator::CondBranch {
                cond: Operand::Value(Value(0)),
                true_label: BlockId(1),
                false_label: BlockId(1),
            },
        ));
        func.blocks.push(make_block(BlockId(1), vec![], Terminator::Return(None)));

        let count = simplify_cfg(&mut func);
        assert!(count > 0);
        // After redundant cond branch -> Branch(1), then merge_single_pred_blocks
        // merges Block 1 into Block 0, so Block 0 ends with Return(None).
        assert!(matches!(func.blocks[0].terminator, Terminator::Return(None)));
        assert_eq!(func.blocks.len(), 1);
    }

    /// `return a || b ? 10 : 20` shape:
    ///   b0: v0 = param; condbr v0, b1, b2
    ///   b1: br b3                     (a is true: or-result 1)
    ///   b2: v1 = param; br b3         (or-result is b)
    ///   b3: v2 = phi [1, b1], [v1, b2]; condbr v2, b4, b5
    ///   b4: ret 10    b5: ret 20
    fn make_or_chain() -> IrFunction {
        let mut func = IrFunction::new("test".to_string(), IrType::I32, vec![], false);
        func.blocks.push(make_block(BlockId(0),
            vec![Instruction::ParamRef { dest: Value(0), param_idx: 0, ty: IrType::I32 }],
            Terminator::CondBranch { cond: Operand::Value(Value(0)), true_label: BlockId(1), false_label: BlockId(2) }));
        func.blocks.push(make_block(BlockId(1), vec![], Terminator::Branch(BlockId(3))));
        func.blocks.push(make_block(BlockId(2),
            vec![Instruction::ParamRef { dest: Value(1), param_idx: 1, ty: IrType::I32 }],
            Terminator::Branch(BlockId(3))));
        func.blocks.push(make_block(BlockId(3),
            vec![Instruction::Phi { dest: Value(2), ty: IrType::I32, incoming: vec![
                (Operand::Const(IrConst::I32(1)), BlockId(1)), (Operand::Value(Value(1)), BlockId(2))] }],
            Terminator::CondBranch { cond: Operand::Value(Value(2)), true_label: BlockId(4), false_label: BlockId(5) }));
        func.blocks.push(make_block(BlockId(4), vec![], Terminator::Return(Some(Operand::Const(IrConst::I32(10))))));
        func.blocks.push(make_block(BlockId(5), vec![], Terminator::Return(Some(Operand::Const(IrConst::I32(20))))));
        func
    }

    fn returns_reached_when(func: &IrFunction, first_true: bool) -> Option<i64> {
        // Follow the path for v0 == first_true (and v1 false), as far as it is decided.
        let idx = |l: BlockId| func.blocks.iter().position(|b| b.label == l).unwrap();
        let mut b = &func.blocks[0];
        for _ in 0..8 {
            match &b.terminator {
                Terminator::Return(Some(Operand::Const(c))) => return c.to_i64(),
                Terminator::Branch(t) => b = &func.blocks[idx(*t)],
                Terminator::CondBranch { cond: Operand::Value(Value(0)), true_label, false_label } => {
                    b = &func.blocks[idx(if first_true { *true_label } else { *false_label })];
                }
                _ => return None,
            }
        }
        None
    }

    #[test]
    fn test_phi_constant_threading() {
        let mut func = make_or_chain();
        simplify_cfg(&mut func);
        // When the first operand is true, control now reaches `ret 10` without
        // re-testing the merged boolean.
        assert_eq!(returns_reached_when(&func, true), Some(10));
        // The merge block keeps only the non-constant incoming value.
        for b in &func.blocks {
            for inst in &b.instructions {
                if let Instruction::Phi { incoming, .. } = inst {
                    assert!(incoming.iter().all(|(op, _)| matches!(op, Operand::Value(_))), "{:?}", incoming);
                }
            }
        }
    }

    #[test]
    fn test_phi_constant_threading_with_merged_pointer() {
        // bb3 merges a flag and a pointer; the edge from bb1 brings flag 0
        // and goes on to bb5, which returns the pointer. Threaded, bb1 jumps
        // to bb5, where a new phi gives the return bb1's pointer.
        let add = |dest: u32, k: i64| Instruction::BinOp {
            dest: Value(dest), op: crate::ir::reexports::IrBinOp::Add, lhs: Operand::Value(Value(5)), rhs: Operand::Const(IrConst::I64(k)), ty: IrType::I64,
        };
        let mut func = IrFunction::new("test".to_string(), IrType::I64, vec![], false);
        func.blocks.push(make_block(BlockId(0), vec![
            Instruction::ParamRef { dest: Value(0), param_idx: 0, ty: IrType::I32 },
            Instruction::ParamRef { dest: Value(1), param_idx: 1, ty: IrType::I32 },
            Instruction::ParamRef { dest: Value(5), param_idx: 2, ty: IrType::I64 },
        ], Terminator::CondBranch { cond: Operand::Value(Value(0)), true_label: BlockId(1), false_label: BlockId(2) }));
        func.blocks.push(make_block(BlockId(1), vec![add(3, 1)], Terminator::Branch(BlockId(3))));
        func.blocks.push(make_block(BlockId(2), vec![add(4, 2)], Terminator::Branch(BlockId(3))));
        func.blocks.push(make_block(BlockId(3), vec![
            Instruction::Phi { dest: Value(2), ty: IrType::I32, incoming: vec![
                (Operand::Const(IrConst::I32(0)), BlockId(1)), (Operand::Value(Value(1)), BlockId(2))] },
            Instruction::Phi { dest: Value(6), ty: IrType::I64, incoming: vec![
                (Operand::Value(Value(3)), BlockId(1)), (Operand::Value(Value(4)), BlockId(2))] },
        ], Terminator::CondBranch { cond: Operand::Value(Value(2)), true_label: BlockId(4), false_label: BlockId(5) }));
        func.blocks.push(make_block(BlockId(4), vec![], Terminator::Return(Some(Operand::Const(IrConst::I64(10))))));
        func.blocks.push(make_block(BlockId(5), vec![], Terminator::Return(Some(Operand::Value(Value(6))))));
        func.next_value_id = 7;
        simplify_cfg(&mut func);

        let b1 = func.blocks.iter().find(|b| b.instructions.iter().any(|i| i.dest() == Some(Value(3)))).unwrap();
        let Terminator::Branch(to) = b1.terminator else { panic!("{:?}", b1.terminator) };
        let ret = func.blocks.iter().find(|b| b.label == to).unwrap();
        let Terminator::Return(Some(Operand::Value(r))) = ret.terminator else { panic!("{:?}", ret.terminator) };
        let phi = ret.instructions.iter().find(|i| i.dest() == Some(r)).expect("a phi at the target");
        let Instruction::Phi { incoming, .. } = phi else { panic!("{:?}", phi) };
        assert!(incoming.iter().any(|(op, l)| matches!(op, Operand::Value(Value(3))) && *l == b1.label), "{:?}", incoming);
    }

    #[test]
    fn test_phi_constant_threading_skips_used_phi() {
        // The merged boolean is also returned elsewhere: no threading.
        let mut func = make_or_chain();
        func.blocks[4].terminator = Terminator::Return(Some(Operand::Value(Value(2))));
        simplify_cfg(&mut func);
        let phi_has_const = func.blocks.iter().flat_map(|b| &b.instructions).any(|i| matches!(i,
            Instruction::Phi { incoming, .. } if incoming.iter().any(|(op, _)| matches!(op, Operand::Const(_)))));
        assert!(phi_has_const);
    }

    #[test]
    fn test_jump_chain_threading() {
        let mut func = IrFunction::new("test".to_string(), IrType::Void, vec![], false);
        func.blocks.push(make_block(BlockId(0), vec![], Terminator::Branch(BlockId(1))));
        func.blocks.push(make_block(BlockId(1), vec![], Terminator::Branch(BlockId(2))));
        func.blocks.push(make_block(BlockId(2), vec![], Terminator::Return(None)));

        let count = simplify_cfg(&mut func);
        assert!(count > 0);
        // After threading Block 0 -> Block 2 (skipping Block 1), Block 1 is dead,
        // then merge_single_pred_blocks merges Block 2 into Block 0.
        assert!(matches!(func.blocks[0].terminator, Terminator::Return(None)));
        assert_eq!(func.blocks.len(), 1);
    }

    #[test]
    fn test_dead_block_elimination() {
        let mut func = IrFunction::new("test".to_string(), IrType::Void, vec![], false);
        func.blocks.push(make_block(BlockId(0), vec![], Terminator::Return(None)));
        func.blocks.push(make_block(
            BlockId(1),
            vec![Instruction::Copy { dest: Value(0), src: Operand::Const(IrConst::I32(42)) }],
            Terminator::Return(None),
        ));

        let count = simplify_cfg(&mut func);
        assert!(count > 0);
        assert_eq!(func.blocks.len(), 1);
        assert_eq!(func.blocks[0].label, BlockId(0));
    }

    #[test]
    fn test_combined_simplifications() {
        // CondBranch(1,1) -> Branch(1) -> thread to 2 -> dead block removal -> merge.
        // After all simplifications, Block 0 absorbs everything reachable.
        let mut func = IrFunction::new("test".to_string(), IrType::Void, vec![], false);
        func.blocks.push(make_block(
            BlockId(0),
            vec![Instruction::Copy { dest: Value(0), src: Operand::Const(IrConst::I32(1)) }],
            Terminator::CondBranch {
                cond: Operand::Value(Value(0)),
                true_label: BlockId(1),
                false_label: BlockId(1),
            },
        ));
        func.blocks.push(make_block(BlockId(1), vec![], Terminator::Branch(BlockId(2))));
        func.blocks.push(make_block(BlockId(2), vec![], Terminator::Return(None)));
        func.blocks.push(make_block(BlockId(3), vec![], Terminator::Return(None)));

        let count = simplify_cfg(&mut func);
        assert!(count > 0);
        // After all passes: Block 3 is dead, intermediate blocks are merged.
        assert!(matches!(func.blocks[0].terminator, Terminator::Return(None)));
        assert_eq!(func.blocks.len(), 1);
    }

    #[test]
    fn test_phi_update_on_thread() {
        // Block 0 -> Block 1 (empty) -> Block 2 (phi referencing Block 1).
        // Threading skips Block 1, then trivial phi simplifies to Copy,
        // then Block 2 merges into Block 0.
        let mut func = IrFunction::new("test".to_string(), IrType::I32, vec![], false);
        func.blocks.push(make_block(
            BlockId(0),
            vec![Instruction::Copy { dest: Value(0), src: Operand::Const(IrConst::I32(42)) }],
            Terminator::Branch(BlockId(1)),
        ));
        func.blocks.push(make_block(BlockId(1), vec![], Terminator::Branch(BlockId(2))));
        func.blocks.push(make_block(
            BlockId(2),
            vec![Instruction::Phi {
                dest: Value(1),
                ty: IrType::I32,
                incoming: vec![(Operand::Value(Value(0)), BlockId(1))],
            }],
            Terminator::Return(Some(Operand::Value(Value(1)))),
        ));

        let count = simplify_cfg(&mut func);
        assert!(count > 0);
        // After threading + dead block elimination + trivial phi -> Copy + merge:
        // Block 0 absorbs everything and ends with Return.
        assert!(matches!(func.blocks[0].terminator, Terminator::Return(Some(Operand::Value(Value(1))))));
        // The Copy instruction should be in Block 0 now (from the merged phi-to-copy).
        let has_copy = func.blocks[0].instructions.iter().any(|inst| {
            matches!(inst, Instruction::Copy { dest: Value(1), .. })
        });
        assert!(has_copy, "Block 0 should contain the Copy from the merged phi");
    }

    #[test]
    fn test_no_thread_through_block_with_instructions() {
        // Block 1 has instructions, so it should NOT be threaded.
        // However, merge_single_pred_blocks will merge Block 1 into Block 0
        // (single pred), and then Block 2 into the merged block.
        let mut func = IrFunction::new("test".to_string(), IrType::I32, vec![], false);
        func.blocks.push(make_block(BlockId(0), vec![], Terminator::Branch(BlockId(1))));
        func.blocks.push(make_block(
            BlockId(1),
            vec![Instruction::Copy { dest: Value(0), src: Operand::Const(IrConst::I32(42)) }],
            Terminator::Branch(BlockId(2)),
        ));
        func.blocks.push(make_block(
            BlockId(2),
            vec![],
            Terminator::Return(Some(Operand::Value(Value(0)))),
        ));

        let _ = simplify_cfg(&mut func);
        // No threading, but merge_single_pred_blocks merges all three blocks.
        assert!(matches!(func.blocks[0].terminator, Terminator::Return(Some(Operand::Value(Value(0))))));
        assert_eq!(func.blocks.len(), 1);
    }

    #[test]
    fn test_cond_branch_threading() {
        // Block 0 cond-branches to Block 1 and Block 2, both forward to Block 3.
        // Threading makes both targets Block 3, redundant cond branch -> Branch(3),
        // dead blocks removed, then Block 3 merged into Block 0.
        let mut func = IrFunction::new("test".to_string(), IrType::Void, vec![], false);
        func.blocks.push(make_block(
            BlockId(0),
            vec![Instruction::Copy { dest: Value(0), src: Operand::Const(IrConst::I32(1)) }],
            Terminator::CondBranch {
                cond: Operand::Value(Value(0)),
                true_label: BlockId(1),
                false_label: BlockId(2),
            },
        ));
        func.blocks.push(make_block(BlockId(1), vec![], Terminator::Branch(BlockId(3))));
        func.blocks.push(make_block(BlockId(2), vec![], Terminator::Branch(BlockId(3))));
        func.blocks.push(make_block(BlockId(3), vec![], Terminator::Return(None)));

        let count = simplify_cfg(&mut func);
        assert!(count > 0);
        // All intermediate blocks absorbed: Block 0 ends with Return(None).
        assert!(matches!(func.blocks[0].terminator, Terminator::Return(None)));
        assert_eq!(func.blocks.len(), 1);
    }

    #[test]
    fn test_no_thread_when_phi_conflict() {
        let mut func = IrFunction::new("test".to_string(), IrType::I64, vec![], false);
        func.blocks.push(make_block(
            BlockId(0),
            vec![Instruction::Cmp {
                dest: Value(5),
                op: IrCmpOp::Eq,
                lhs: Operand::Value(Value(2)),
                rhs: Operand::Const(IrConst::I64(0)),
                ty: IrType::I64,
            }],
            Terminator::CondBranch {
                cond: Operand::Value(Value(5)),
                true_label: BlockId(1),
                false_label: BlockId(3),
            },
        ));
        func.blocks.push(make_block(BlockId(1), vec![], Terminator::Branch(BlockId(3))));
        func.blocks.push(make_block(
            BlockId(3),
            vec![Instruction::Phi {
                dest: Value(8),
                ty: IrType::I64,
                incoming: vec![
                    (Operand::Value(Value(2)), BlockId(0)),
                    (Operand::Const(IrConst::I64(1)), BlockId(1)),
                ],
            }],
            Terminator::Return(Some(Operand::Value(Value(8)))),
        ));

        simplify_cfg(&mut func);

        match &func.blocks[0].terminator {
            Terminator::CondBranch { true_label, false_label, .. } => {
                assert!(
                    *true_label == BlockId(1) || *false_label != *true_label,
                    "Should not thread both branches to same target when phi has different values"
                );
            }
            Terminator::Branch(_) => {
                panic!("CondBranch was incorrectly simplified to unconditional Branch, losing phi distinction");
            }
            _ => panic!("Unexpected terminator"),
        }

        let merge_block = func.blocks.iter().find(|b| b.label == BlockId(3)).unwrap();
        if let Instruction::Phi { incoming, .. } = &merge_block.instructions[0] {
            assert!(incoming.len() >= 2, "Phi must retain at least 2 incoming edges");
            let has_const_1 = incoming.iter().any(|(val, _)| {
                matches!(val, Operand::Const(IrConst::I64(1)))
            });
            assert!(has_const_1, "Phi must still have the Const(1) incoming value");
        } else {
            panic!("Expected Phi instruction in merge block");
        }
    }

    #[test]
    fn test_trivial_phi_simplification() {
        // Simulates `__builtin_constant_p(v) && expr`:
        // Block 0 has constant-false cond branch, so Block 1 becomes dead.
        // The phi in Block 2 loses one incoming edge -> trivial phi -> Copy.
        // Then blocks merge into Block 0.
        let mut func = IrFunction::new("test".to_string(), IrType::I32, vec![], false);
        func.blocks.push(make_block(
            BlockId(0),
            vec![],
            Terminator::CondBranch {
                cond: Operand::Const(IrConst::I64(0)),
                true_label: BlockId(1),
                false_label: BlockId(2),
            },
        ));
        func.blocks.push(make_block(
            BlockId(1),
            vec![Instruction::Cmp {
                dest: Value(1),
                op: IrCmpOp::Ne,
                lhs: Operand::Value(Value(0)),
                rhs: Operand::Const(IrConst::I64(0)),
                ty: IrType::I64,
            }],
            Terminator::Branch(BlockId(2)),
        ));
        func.blocks.push(make_block(
            BlockId(2),
            vec![Instruction::Phi {
                dest: Value(2),
                ty: IrType::I64,
                incoming: vec![
                    (Operand::Const(IrConst::I64(0)), BlockId(0)),
                    (Operand::Value(Value(1)), BlockId(1)),
                ],
            }],
            Terminator::CondBranch {
                cond: Operand::Value(Value(2)),
                true_label: BlockId(3),
                false_label: BlockId(4),
            },
        ));
        func.blocks.push(make_block(
            BlockId(3),
            vec![],
            Terminator::Return(Some(Operand::Const(IrConst::I32(1)))),
        ));
        func.blocks.push(make_block(
            BlockId(4),
            vec![],
            Terminator::Return(Some(Operand::Const(IrConst::I32(0)))),
        ));

        let _ = simplify_cfg(&mut func);

        // Block 1 should be dead (unreachable).
        let has_block_1 = func.blocks.iter().any(|b| b.label == BlockId(1));
        assert!(!has_block_1, "Dead block 1 (RHS of &&) should be eliminated");

        // The phi should have been simplified to a Copy with Const(0), or the
        // constant edge threaded straight to `return 0` (no phi left at all).
        // A Copy may be in Block 0 (if Block 2 was merged) or Block 2.
        let has_copy_const_0 = func.blocks.iter().any(|b| {
            b.instructions.iter().any(|inst| {
                matches!(inst, Instruction::Copy { dest: Value(2), src: Operand::Const(IrConst::I64(0)) })
            })
        });
        let threaded_to_return_0 = func.blocks.len() == 1
            && matches!(func.blocks[0].terminator, Terminator::Return(Some(Operand::Const(IrConst::I32(0)))));
        assert!(has_copy_const_0 || threaded_to_return_0, "Should have Copy(Value(2), Const(0)) or return 0: {:?}",
            func.blocks.iter().map(|b| &b.terminator).collect::<Vec<_>>());
    }

    #[test]
    fn test_fold_constant_switch() {
        let mut func = IrFunction::new("test".to_string(), IrType::I32, vec![], false);
        func.blocks.push(make_block(
            BlockId(0),
            vec![],
            Terminator::Switch {
                val: Operand::Const(IrConst::I64(37)),
                cases: vec![(10, BlockId(1)), (20, BlockId(2)), (30, BlockId(3))],
                default: BlockId(4),
                ty: IrType::I64,
            },
        ));
        func.blocks.push(make_block(BlockId(1), vec![], Terminator::Return(Some(Operand::Const(IrConst::I32(100))))));
        func.blocks.push(make_block(BlockId(2), vec![], Terminator::Return(Some(Operand::Const(IrConst::I32(200))))));
        func.blocks.push(make_block(BlockId(3), vec![], Terminator::Return(Some(Operand::Const(IrConst::I32(300))))));
        func.blocks.push(make_block(BlockId(4), vec![], Terminator::Return(Some(Operand::Const(IrConst::I32(-1))))));

        let count = simplify_cfg(&mut func);
        assert!(count > 0, "Should have made simplifications");
        // After switch fold -> Branch(4), dead blocks removed, Block 4 merged into Block 0.
        assert!(matches!(func.blocks[0].terminator, Terminator::Return(Some(Operand::Const(IrConst::I32(-1))))),
            "Switch on constant 37 should result in Return(-1) after merge");
        assert_eq!(func.blocks.len(), 1);
    }

    #[test]
    fn test_fold_constant_switch_matching_case() {
        let mut func = IrFunction::new("test".to_string(), IrType::I32, vec![], false);
        func.blocks.push(make_block(
            BlockId(0),
            vec![],
            Terminator::Switch {
                val: Operand::Const(IrConst::I64(20)),
                cases: vec![(10, BlockId(1)), (20, BlockId(2)), (30, BlockId(3))],
                default: BlockId(4),
                ty: IrType::I64,
            },
        ));
        func.blocks.push(make_block(BlockId(1), vec![], Terminator::Return(Some(Operand::Const(IrConst::I32(100))))));
        func.blocks.push(make_block(BlockId(2), vec![], Terminator::Return(Some(Operand::Const(IrConst::I32(200))))));
        func.blocks.push(make_block(BlockId(3), vec![], Terminator::Return(Some(Operand::Const(IrConst::I32(300))))));
        func.blocks.push(make_block(BlockId(4), vec![], Terminator::Return(Some(Operand::Const(IrConst::I32(-1))))));

        let count = simplify_cfg(&mut func);
        assert!(count > 0, "Should have made simplifications");
        // After switch fold -> Branch(2), dead blocks removed, Block 2 merged into Block 0.
        assert!(matches!(func.blocks[0].terminator, Terminator::Return(Some(Operand::Const(IrConst::I32(200))))),
            "Switch on constant 20 should result in Return(200) after merge");
        assert_eq!(func.blocks.len(), 1);
    }

    #[test]
    fn test_fold_constant_switch_phi_cleanup() {
        // Block 0: Copy(Value(1), Const(1)), CondBranch(Value(1), true:1, false:3)
        // Block 1: Switch(Const(10), case 10->Block 2, default->Block 3)
        // Block 2: Return(42)
        // Block 3: Phi from Block 0 and Block 1, Return(phi)
        //
        // After folding:
        // - Block 0's CondBranch resolves (cond=1) -> Branch(1)
        // - Block 1's Switch resolves (val=10) -> Branch(2), removes phi entry from Block 3
        // - Block 3 becomes dead (only Block 0 went to it, but that's folded to Block 1)
        // - Result: Block 0 -> Block 1 -> Block 2, all merge.
        let mut func = IrFunction::new("test".to_string(), IrType::I32, vec![], false);
        func.blocks.push(make_block(
            BlockId(0),
            vec![Instruction::Copy { dest: Value(1), src: Operand::Const(IrConst::I32(1)) }],
            Terminator::CondBranch {
                cond: Operand::Value(Value(1)),
                true_label: BlockId(1),
                false_label: BlockId(3),
            },
        ));
        func.blocks.push(make_block(
            BlockId(1),
            vec![],
            Terminator::Switch {
                val: Operand::Const(IrConst::I64(10)),
                cases: vec![(10, BlockId(2))],
                default: BlockId(3),
                ty: IrType::I64,
            },
        ));
        func.blocks.push(make_block(BlockId(2), vec![], Terminator::Return(Some(Operand::Const(IrConst::I32(42))))));
        func.blocks.push(make_block(
            BlockId(3),
            vec![Instruction::Phi {
                dest: Value(0),
                ty: IrType::I32,
                incoming: vec![
                    (Operand::Const(IrConst::I32(1)), BlockId(0)),
                    (Operand::Const(IrConst::I32(2)), BlockId(1)),
                ],
            }],
            Terminator::Return(Some(Operand::Value(Value(0)))),
        ));

        let count = simplify_cfg(&mut func);
        assert!(count > 0, "Should have made simplifications");

        // After all folding and merging, Block 0 should end with Return(42).
        assert!(matches!(func.blocks[0].terminator, Terminator::Return(Some(Operand::Const(IrConst::I32(42))))),
            "After full simplification, should return 42");
    }

    /// `if (find(k, &v) && v == 3)` after inlining: the hit and miss paths
    /// merge the found flag with `v`, and the merge branches on the flag.
    #[test]
    fn test_flag_threaded_with_merged_value() {
        let mut func = IrFunction::new("test".to_string(), IrType::I32, vec![], false);
        let (b, v) = (BlockId, |n| Operand::Value(Value(n)));
        func.blocks = vec![
            make_block(b(0), vec![], Terminator::CondBranch { cond: v(1), true_label: b(1), false_label: b(2) }),
            // hit: v = 5
            make_block(b(1), vec![], Terminator::Branch(b(3))),
            // miss
            make_block(b(2), vec![], Terminator::Branch(b(3))),
            make_block(b(3), vec![
                Instruction::Phi { dest: Value(20), ty: IrType::I32, incoming: vec![(Operand::Const(IrConst::I32(1)), b(1)), (Operand::Const(IrConst::I32(0)), b(2))] },
                Instruction::Phi { dest: Value(21), ty: IrType::I32, incoming: vec![(Operand::Const(IrConst::I32(5)), b(1)), (Operand::Const(IrConst::I32(0)), b(2))] },
            ], Terminator::CondBranch { cond: v(20), true_label: b(4), false_label: b(6) }),
            make_block(b(4), vec![
                Instruction::Cmp { dest: Value(30), op: IrCmpOp::Eq, lhs: v(21), rhs: Operand::Const(IrConst::I32(3)), ty: IrType::I32 },
            ], Terminator::CondBranch { cond: v(30), true_label: b(5), false_label: b(6) }),
            make_block(b(5), vec![], Terminator::Return(Some(v(21)))),
            make_block(b(6), vec![], Terminator::Return(Some(Operand::Const(IrConst::I32(0))))),
        ];
        func.next_value_id = 31;
        let label_to_idx = build_label_to_idx(&func);
        assert_eq!(thread_phi_cond_branches_merging(&mut func, &label_to_idx), 1);
        // Each path goes straight to its target.
        assert!(matches!(func.blocks[1].terminator, Terminator::Branch(BlockId(4))));
        assert!(matches!(func.blocks[2].terminator, Terminator::Branch(BlockId(6))));
        // The found path's `v` is merged again where it is read: in the
        // target, from the one predecessor sent there.
        let Instruction::Phi { dest: nv, ref incoming, .. } = func.blocks[4].instructions[0]
        else { panic!("{:?}", func.blocks[4].instructions) };
        assert!(matches!(incoming[..], [(Operand::Const(IrConst::I32(5)), BlockId(1))]));
        assert!(matches!(func.blocks[4].instructions[1], Instruction::Cmp { lhs: Operand::Value(l), .. } if l == nv));
        assert!(matches!(func.blocks[5].terminator, Terminator::Return(Some(Operand::Value(r))) if r == nv));
        // The miss path's target, also reached from block 4, is not touched.
        assert!(func.blocks[6].instructions.is_empty());
    }

    /// `if (x >= 97 && x <= 122) return 1; return phi` with F's phi taking
    /// `f_from_x` from the second test's block.
    fn range_test(f_from_x: i32) -> IrFunction {
        let mut func = IrFunction::new("test".to_string(), IrType::I32, vec![], false);
        let (b, v) = (BlockId, |n| Operand::Value(Value(n)));
        let c = |n| Operand::Const(IrConst::I32(n));
        func.blocks = vec![
            make_block(b(0), vec![
                Instruction::Cmp { dest: Value(10), op: IrCmpOp::Sge, lhs: v(1), rhs: c(97), ty: IrType::I32 },
            ], Terminator::CondBranch { cond: v(10), true_label: b(1), false_label: b(3) }),
            make_block(b(1), vec![
                Instruction::Cmp { dest: Value(11), op: IrCmpOp::Sle, lhs: v(1), rhs: c(122), ty: IrType::I32 },
            ], Terminator::CondBranch { cond: v(11), true_label: b(2), false_label: b(3) }),
            make_block(b(2), vec![], Terminator::Return(Some(c(1)))),
            make_block(b(3), vec![
                Instruction::Phi { dest: Value(12), ty: IrType::I32, incoming: vec![(c(0), b(0)), (c(f_from_x), b(1))] },
            ], Terminator::Return(Some(v(12)))),
        ];
        func.next_value_id = 13;
        func
    }

    #[test]
    fn test_range_tests_fused() {
        let mut func = range_test(0);
        let label_to_idx = build_label_to_idx(&func);
        assert_eq!(fuse_range_tests(&mut func, &label_to_idx), 1);
        assert!(matches!(func.blocks[0].instructions[..], [
            Instruction::BinOp { op: crate::ir::reexports::IrBinOp::Sub, ty: IrType::U32, .. },
            Instruction::Cmp { dest: Value(10), op: IrCmpOp::Ule, ty: IrType::U32, .. },
        ]));
        let Instruction::Cmp { rhs: Operand::Const(k), .. } = func.blocks[0].instructions[1] else { unreachable!() };
        assert_eq!(k.to_i64(), Some(25));
        assert!(matches!(func.blocks[0].terminator,
            Terminator::CondBranch { true_label: BlockId(2), false_label: BlockId(3), .. }));
        // The second test's block is left unreachable.
        assert!(simplify_cfg(&mut func) > 0);
        assert!(!func.blocks.iter().any(|b| b.label == BlockId(1)));
    }

    #[test]
    fn test_range_tests_kept_when_failing_edges_differ() {
        let mut func = range_test(5);
        let label_to_idx = build_label_to_idx(&func);
        assert_eq!(fuse_range_tests(&mut func, &label_to_idx), 0);
    }
}
