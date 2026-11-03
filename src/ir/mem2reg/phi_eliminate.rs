//! Phi elimination: lower SSA phi nodes to copies in predecessor blocks.
//!
//! This pass runs after all SSA optimizations and before backend codegen.
//! It converts each Phi instruction into Copy instructions placed at the end
//! of each predecessor block (before the terminator).
//!
//! Smart temporary allocation:
//! When a block has multiple phis, we analyze the copy graph per-predecessor
//! to determine which copies actually need temporaries (only those involved
//! in cycles, e.g., swap patterns) and which can be direct copies. This
//! dramatically reduces the number of temporaries and copy instructions,
//! especially for large switch statements where most phis pass through
//! values unchanged.
//!
//! For non-conflicting phis (the common case), we emit direct copies:
//!   pred_block:
//!     %phi1_dest = copy src1
//!     %phi2_dest = copy src2
//!     <terminator>
//!
//! For conflicting phis (cycles), we use shared temporaries and a two-phase
//! copy sequence to avoid the lost-copy problem:
//!   pred_block:
//!     %tmp1 = copy src1  // save source before it's overwritten
//!     <terminator>
//!   target_block:
//!     %phi1_dest = copy %tmp1  // restore from temporary
//!     ... rest of block ...
//!
//! Critical edge splitting:
//! When a predecessor block has multiple successors (e.g., a CondBranch) and
//! the target block has phis, placing copies at the end of the predecessor
//! would execute them on ALL outgoing paths, not just the edge to the phi's
//! block. This corrupts values used on other paths. To fix this, we split
//! the critical edge by inserting a new trampoline block that contains only
//! the phi copies and branches unconditionally to the target.

use crate::common::fx_hash::{FxHashMap, FxHashSet};
use crate::ir::reexports::{
    BasicBlock,
    BlockId,
    Instruction,
    IrFunction,
    IrModule,
    Operand,
    Terminator,
    Value,
};

/// Eliminate all phi nodes in the module by lowering them to copies.
pub fn eliminate_phis(module: &mut IrModule) {
    // Compute the global max block ID across ALL functions to avoid label collisions
    // when creating trampoline blocks. Labels are module-wide (.LBB0, .LBB1, ...).
    let mut next_block_id = 0u32;
    for func in &module.functions {
        for block in &func.blocks {
            if block.label.0 >= next_block_id {
                next_block_id = block.label.0 + 1;
            }
        }
    }

    for func in &mut module.functions {
        if func.is_declaration || func.blocks.is_empty() {
            continue;
        }
        eliminate_phis_in_function(func, &mut next_block_id);
    }
}

/// Returns the number of distinct successor block IDs for a block.
/// Accounts for both terminator targets and InlineAsm goto_labels.
fn successor_count(block: &BasicBlock) -> usize {
    let mut seen: Vec<BlockId> = Vec::new();
    match &block.terminator {
        Terminator::Return(_) | Terminator::Unreachable => {}
        Terminator::Branch(label) => { seen.push(*label); }
        Terminator::CondBranch { true_label, false_label, .. } => {
            seen.push(*true_label);
            if true_label != false_label { seen.push(*false_label); }
        }
        Terminator::IndirectBranch { possible_targets, .. } => {
            seen.extend_from_slice(possible_targets);
        }
        Terminator::Switch { cases, default, .. } => {
            seen.push(*default);
            for &(_, label) in cases {
                if !seen.contains(&label) { seen.push(label); }
            }
        }
    }
    // InlineAsm goto_labels are implicit control flow edges.
    for inst in &block.instructions {
        if let Instruction::InlineAsm { goto_labels, .. } = inst {
            for (_, label) in goto_labels {
                if !seen.contains(label) { seen.push(*label); }
            }
        }
    }
    seen.len()
}

/// Replace one occurrence of `old_target` with `new_target` in a block's
/// terminator or InlineAsm goto_labels.
fn retarget_block_edge_once(block: &mut BasicBlock, old_target: BlockId, new_target: BlockId) {
    match &mut block.terminator {
        Terminator::Branch(t) => {
            if *t == old_target {
                *t = new_target;
                return;
            }
        }
        Terminator::CondBranch { true_label, false_label, .. } => {
            // Only retarget one edge to avoid changing both sides of a diamond
            if *true_label == old_target {
                *true_label = new_target;
                return;
            } else if *false_label == old_target {
                *false_label = new_target;
                return;
            }
        }
        Terminator::IndirectBranch { possible_targets, .. } => {
            for t in possible_targets.iter_mut() {
                if *t == old_target {
                    *t = new_target;
                    return;
                }
            }
        }
        Terminator::Switch { cases, default, .. } => {
            if *default == old_target {
                *default = new_target;
                return;
            } else {
                for (_, t) in cases.iter_mut() {
                    if *t == old_target {
                        *t = new_target;
                        return;
                    }
                }
            }
        }
        _ => {}
    }
    // Check InlineAsm goto_labels for implicit control flow edges.
    for inst in &mut block.instructions {
        if let Instruction::InlineAsm { goto_labels, .. } = inst {
            for (_, label) in goto_labels.iter_mut() {
                if *label == old_target {
                    *label = new_target;
                    return;
                }
            }
        }
    }
}

struct TrampolineBlock {
    label: BlockId,
    copies: Vec<Instruction>,
    branch_target: BlockId,
    pred_idx: usize,
    old_target: BlockId,
}

/// Get or create a trampoline block for a (pred, target) critical edge.
fn get_or_create_trampoline(
    trampoline_map: &mut FxHashMap<(usize, BlockId), usize>,
    trampolines: &mut Vec<TrampolineBlock>,
    pred_idx: usize,
    target_block_id: BlockId,
    next_block_id: &mut u32,
) -> usize {
    *trampoline_map
        .entry((pred_idx, target_block_id))
        .or_insert_with(|| {
            let idx = trampolines.len();
            let label = BlockId(*next_block_id);
            *next_block_id += 1;
            trampolines.push(TrampolineBlock {
                label,
                copies: Vec::new(),
                branch_target: target_block_id,
                pred_idx,
                old_target: target_block_id,
            });
            idx
        })
}

/// Determine which phi copies on a given predecessor edge need temporaries.
///
/// A copy `dest_i = src_i` needs a temporary if `src_i` is the destination
/// of another phi copy (i.e., another phi writes to the value we're reading).
/// This detects interference in the copy graph (e.g., swap patterns: x=y, y=x).
///
/// Returns a set of phi indices that need temporaries.
fn find_conflicting_phis(copies: &[(u32, Option<u32>)]) -> FxHashSet<usize> {
    // Build set of all phi destinations for this edge.
    let dest_set: FxHashSet<u32> = copies.iter().map(|(d, _)| *d).collect();

    // A phi copy needs a temporary if its source is overwritten by another phi
    // destination on this same edge. Specifically, phi i needs a temp if
    // src_i is in dest_set AND src_i != dest_i (self-copies don't conflict).
    let mut needs_temp: FxHashSet<usize> = FxHashSet::default();

    for (i, &(dest_i, src_i_opt)) in copies.iter().enumerate() {
        if let Some(src_i) = src_i_opt {
            if src_i != dest_i && dest_set.contains(&src_i) {
                needs_temp.insert(i);
            }
        }
    }

    // Conservative safety net: also mark phis whose destination is read by a
    // conflicting phi. This ensures the two-phase ordering (temp saves in
    // Pass 1, direct copies in Pass 2) remains correct even for chain
    // patterns like `a = b, b = c, c = a` where multiple values are involved
    // in cycles. This may over-approximate slightly (e.g., marking a phi
    // whose destination is read but already saved by another temp), but
    // correctness is paramount.
    if !needs_temp.is_empty() {
        let conflicting_sources: FxHashSet<u32> = needs_temp.iter()
            .filter_map(|&i| copies[i].1)
            .collect();
        for (j, &(dest_j, _)) in copies.iter().enumerate() {
            if conflicting_sources.contains(&dest_j) {
                needs_temp.insert(j);
            }
        }
    }

    needs_temp
}

/// Phi information extracted from a block.
struct PhiInfo {
    dest: Value,
    incoming: Vec<(Operand, BlockId)>,
}

/// Context shared across phi elimination for a single function.
struct PhiElimCtx<'a> {
    label_to_idx: FxHashMap<BlockId, usize>,
    multi_succ: Vec<bool>,
    is_indirect_branch: Vec<bool>,
    pred_copies: FxHashMap<usize, Vec<Instruction>>,
    target_copies: Vec<Vec<Instruction>>,
    trampolines: Vec<TrampolineBlock>,
    trampoline_map: FxHashMap<(usize, BlockId), usize>,
    next_block_id: &'a mut u32,
    next_value: u32,
}

fn eliminate_phis_in_function(func: &mut IrFunction, next_block_id: &mut u32) {
    let mut ctx = PhiElimCtx {
        label_to_idx: func.blocks.iter().enumerate().map(|(i, b)| (b.label, i)).collect(),
        multi_succ: func.blocks.iter().map(|b| successor_count(b) > 1).collect(),
        is_indirect_branch: func.blocks.iter()
            .map(|b| matches!(&b.terminator, Terminator::IndirectBranch { .. })).collect(),
        pred_copies: FxHashMap::default(),
        target_copies: vec![Vec::new(); func.blocks.len()],
        trampolines: Vec::new(),
        trampoline_map: FxHashMap::default(),
        next_block_id,
        next_value: if func.next_value_id > 0 { func.next_value_id } else { func.max_value_id() + 1 },
    };

    let block_phis = collect_block_phis(func);

    for (block_idx, phis) in block_phis.iter().enumerate() {
        if phis.is_empty() {
            continue;
        }
        let target_block_id = func.blocks[block_idx].label;
        if phis.len() == 1 {
            emit_single_phi_copies(&phis[0], target_block_id, &mut ctx);
        } else {
            emit_multi_phi_copies(phis, block_idx, target_block_id, &mut ctx);
        }
    }

    apply_phi_transformations(func, &mut ctx);
    func.next_value_id = ctx.next_value;
}

/// Collect PhiInfo from all blocks.
fn collect_block_phis(func: &IrFunction) -> Vec<Vec<PhiInfo>> {
    func.blocks.iter().map(|block| {
        block.instructions.iter().filter_map(|inst| {
            if let Instruction::Phi { dest, incoming, .. } = inst {
                Some(PhiInfo { dest: *dest, incoming: incoming.clone() })
            } else {
                None
            }
        }).collect()
    }).collect()
}

/// Emit copies for a block with a single phi (no temporaries needed).
fn emit_single_phi_copies(phi: &PhiInfo, target_block_id: BlockId, ctx: &mut PhiElimCtx) {
    for (src, pred_label) in &phi.incoming {
        let pred_idx = match ctx.label_to_idx.get(pred_label) {
            Some(&idx) => idx,
            None => continue,
        };
        // Skip self-copies
        if let Operand::Value(v) = src {
            if v.0 == phi.dest.0 {
                continue;
            }
        }
        let copy_inst = Instruction::Copy { dest: phi.dest, src: *src };
        place_copy(ctx, pred_idx, target_block_id, copy_inst);
    }
}

/// Emit copies for a block with multiple phis, using smart temporary allocation.
/// Shared temporaries are only allocated for phis involved in copy cycles.
fn emit_multi_phi_copies(
    phis: &[PhiInfo], block_idx: usize, target_block_id: BlockId,
    ctx: &mut PhiElimCtx,
) {
    // Collect unique predecessor labels.
    let mut pred_label_set: FxHashSet<BlockId> = FxHashSet::default();
    let mut pred_labels: Vec<BlockId> = Vec::new();
    for phi in phis {
        for (_, pred_label) in &phi.incoming {
            if pred_label_set.insert(*pred_label) {
                pred_labels.push(*pred_label);
            }
        }
    }

    // Precompute per-phi source lookup tables.
    let phi_src_maps: Vec<FxHashMap<BlockId, &Operand>> = phis.iter()
        .map(|phi| phi.incoming.iter().map(|(src, pl)| (*pl, src)).collect())
        .collect();

    // Find which phis are globally conflicting (need temporaries on any edge).
    let globally_needs_temp = find_globally_conflicting_phis(
        phis, &pred_labels, &phi_src_maps, &ctx.label_to_idx,
    );

    // Allocate shared temporaries.
    let mut phi_temps: Vec<Option<Value>> = vec![None; phis.len()];
    for &i in &globally_needs_temp {
        phi_temps[i] = Some(Value(ctx.next_value));
        ctx.next_value += 1;
    }

    // Emit target block copies for conflicting phis (temp -> dest).
    for (i, phi) in phis.iter().enumerate() {
        if let Some(tmp) = phi_temps[i] {
            ctx.target_copies[block_idx].push(Instruction::Copy {
                dest: phi.dest,
                src: Operand::Value(tmp),
            });
        }
    }

    // Emit copies for each predecessor edge.
    for pred_label in &pred_labels {
        let pred_idx = match ctx.label_to_idx.get(pred_label) {
            Some(&idx) => idx,
            None => continue,
        };
        let edge_copies = build_edge_copies(phis, &phi_temps, &phi_src_maps, pred_label);
        place_copies(ctx, pred_idx, target_block_id, edge_copies);
    }
}

/// Determine which phi indices are globally conflicting across all predecessor edges.
fn find_globally_conflicting_phis(
    phis: &[PhiInfo],
    pred_labels: &[BlockId],
    phi_src_maps: &[FxHashMap<BlockId, &Operand>],
    label_to_idx: &FxHashMap<BlockId, usize>,
) -> FxHashSet<usize> {
    let mut globally_needs_temp: FxHashSet<usize> = FxHashSet::default();
    for pred_label in pred_labels {
        if !label_to_idx.contains_key(pred_label) {
            continue;
        }
        let copies_info: Vec<(u32, Option<u32>)> = phis.iter().enumerate().map(|(i, phi)| {
            let src_val_id = phi_src_maps[i].get(pred_label).and_then(|s| {
                if let Operand::Value(v) = *s { Some(v.0) } else { None }
            });
            (phi.dest.0, src_val_id)
        }).collect();
        for &i in &find_conflicting_phis(&copies_info) {
            globally_needs_temp.insert(i);
        }
    }
    globally_needs_temp
}

/// Build the ordered copy instructions for a single predecessor edge:
/// Pass 1 (temp saves for conflicting phis), then Pass 2 (direct copies).
fn build_edge_copies(
    phis: &[PhiInfo],
    phi_temps: &[Option<Value>],
    phi_src_maps: &[FxHashMap<BlockId, &Operand>],
    pred_label: &BlockId,
) -> Vec<Instruction> {
    let mut copies = Vec::new();

    // Pass 1: Emit temporary saves for conflicting phis (must come first).
    for (i, _phi) in phis.iter().enumerate() {
        if let Some(tmp) = phi_temps[i] {
            if let Some(src) = phi_src_maps[i].get(pred_label) {
                copies.push(Instruction::Copy { dest: tmp, src: *(*src) });
            }
        }
    }

    // Pass 2: Emit direct copies for non-conflicting phis.
    for (i, phi) in phis.iter().enumerate() {
        if phi_temps[i].is_none() {
            if let Some(src) = phi_src_maps[i].get(pred_label) {
                if let Operand::Value(v) = *src {
                    if v.0 == phi.dest.0 { continue; } // skip self-copy
                }
                copies.push(Instruction::Copy { dest: phi.dest, src: *(*src) });
            }
        }
    }

    copies
}

/// Place a single copy instruction, using trampolines for critical edges.
fn place_copy(ctx: &mut PhiElimCtx, pred_idx: usize, target_block_id: BlockId, copy_inst: Instruction) {
    if ctx.multi_succ[pred_idx] && !ctx.is_indirect_branch[pred_idx] {
        let tramp_idx = get_or_create_trampoline(
            &mut ctx.trampoline_map, &mut ctx.trampolines,
            pred_idx, target_block_id, ctx.next_block_id,
        );
        ctx.trampolines[tramp_idx].copies.push(copy_inst);
    } else {
        ctx.pred_copies.entry(pred_idx).or_default().push(copy_inst);
    }
}

/// Place multiple copy instructions, using trampolines for critical edges.
fn place_copies(ctx: &mut PhiElimCtx, pred_idx: usize, target_block_id: BlockId, copies: Vec<Instruction>) {
    if copies.is_empty() { return; }
    if ctx.multi_succ[pred_idx] && !ctx.is_indirect_branch[pred_idx] {
        let tramp_idx = get_or_create_trampoline(
            &mut ctx.trampoline_map, &mut ctx.trampolines,
            pred_idx, target_block_id, ctx.next_block_id,
        );
        ctx.trampolines[tramp_idx].copies.extend(copies);
    } else {
        ctx.pred_copies.entry(pred_idx).or_default().extend(copies);
    }
}

/// Apply all phi elimination transformations to the function:
/// remove phis, insert copies, retarget terminators, add trampolines.
fn apply_phi_transformations(func: &mut IrFunction, ctx: &mut PhiElimCtx) {
    for (block_idx, block) in func.blocks.iter_mut().enumerate() {
        // Remove phi instructions (and their spans)
        if !block.source_spans.is_empty() {
            let mut span_idx = 0;
            block.source_spans.retain(|_| {
                let keep = !matches!(block.instructions.get(span_idx), Some(Instruction::Phi { .. }));
                span_idx += 1;
                keep
            });
        }
        block.instructions.retain(|inst| !matches!(inst, Instruction::Phi { .. }));

        // Prepend target copies (these go at the start, replacing the phis)
        if !ctx.target_copies[block_idx].is_empty() {
            let num_copies = ctx.target_copies[block_idx].len();
            let mut new_insts = ctx.target_copies[block_idx].clone();
            new_insts.append(&mut block.instructions);
            block.instructions = new_insts;
            if !block.source_spans.is_empty() {
                let mut new_spans = vec![crate::common::source::Span::dummy(); num_copies];
                new_spans.append(&mut block.source_spans);
                block.source_spans = new_spans;
            }
        }

        // Insert predecessor copies before terminator
        if let Some(copies) = ctx.pred_copies.remove(&block_idx) {
            let num_copies = copies.len();
            block.instructions.extend(copies);
            if !block.source_spans.is_empty() {
                block.source_spans.extend(std::iter::repeat_n(crate::common::source::Span::dummy(), num_copies));
            }
        }
    }

    // Retarget predecessors that need trampolines
    for trampoline in &ctx.trampolines {
        retarget_block_edge_once(
            &mut func.blocks[trampoline.pred_idx],
            trampoline.old_target,
            trampoline.label,
        );
    }

    // A trampoline at the end of the function costs its path two jumps, out
    // and back. Right before its target it falls through instead, costing
    // that path one jump at most, but the block laid out before the target
    // then jumps to it if it used to fall through. So it goes there when
    // that costs nothing or is worth it: on a loop's back edge (the block
    // before the header is entered once per loop), when that block does not
    // fall through into the target, or when it makes a call, which costs
    // far more than a jump. Otherwise it stays at the end, where a
    // trampoline whose copies coalesce away is jumped over for free.
    let index: FxHashMap<BlockId, usize> = func.blocks.iter().enumerate().map(|(i, b)| (b.label, i)).collect();
    let mut before: FxHashMap<BlockId, Vec<BasicBlock>> = FxHashMap::default();
    for trampoline in std::mem::take(&mut ctx.trampolines) {
        let num_copies = trampoline.copies.len();
        let block = BasicBlock {
            label: trampoline.label,
            instructions: trampoline.copies,
            source_spans: vec![crate::common::source::Span::dummy(); num_copies],
            terminator: Terminator::Branch(trampoline.branch_target),
        };
        let target = trampoline.branch_target;
        let move_before = index.get(&target).is_some_and(|&t| {
            let Some(prev) = t.checked_sub(1).map(|p| &func.blocks[p]) else { return false };
            let falls_into = match &prev.terminator {
                Terminator::Branch(l) => *l == target,
                Terminator::CondBranch { true_label, false_label, .. } => *true_label == target || *false_label == target,
                _ => false,
            };
            let calls = prev.instructions.iter().any(|i| matches!(i, Instruction::Call { .. } | Instruction::CallIndirect { .. }));
            trampoline.pred_idx >= t || !falls_into || calls
        });
        if move_before {
            before.entry(target).or_default().push(block);
        } else {
            func.blocks.push(block);
        }
    }
    if before.is_empty() {
        return;
    }
    let mut blocks = Vec::with_capacity(func.blocks.len() + before.len());
    for block in std::mem::take(&mut func.blocks) {
        blocks.extend(before.remove(&block.label).unwrap_or_default());
        blocks.push(block);
    }
    func.blocks = blocks;
}

/// Restores of phis from their temporaries move into the predecessors, where
/// a restore of the value its temporary was just saved from goes, with the
/// save.
///
/// The phis of a block that conflict (one's incoming value is another's
/// destination) are saved into temporaries on every edge and restored at the
/// start of the block. The passes after phi elimination (loop rotation and
/// promotion) leave many such saves of the phi itself: sqlite3VdbeExec's
/// dispatch loop carries values that most of its 232 latches leave alone,
/// and each of those loaded them into temporaries for the loop's head to
/// store back.
///
/// A block's copies `p = copy t`, each t used there only, that can go first
/// (nothing before them touches p or writes t) move to the end of every
/// predecessor when all of them branch to it alone (then
/// the copies run on the same paths, in the same order, the t's holding the
/// same values). In each, a `t = copy p` with neither written after it is
/// then dropped together with the restore.
pub(crate) fn sink_phi_restores(module: &mut IrModule) {
    for func in module.functions.iter_mut().filter(|f| !f.is_declaration) {
        sink_phi_restores_in_function(func);
    }
}

fn sink_phi_restores_in_function(func: &mut IrFunction) {
    let n = func.blocks.len();
    if n < 2 {
        return;
    }
    let mut uses: FxHashMap<u32, u32> = FxHashMap::default();
    for block in &func.blocks {
        for inst in &block.instructions {
            inst.for_each_used_value(|v| *uses.entry(v).or_default() += 1);
        }
        block.terminator.for_each_used_value(|v| *uses.entry(v).or_default() += 1);
    }
    let index: FxHashMap<BlockId, usize> = func.blocks.iter().enumerate().map(|(i, b)| (b.label, i)).collect();
    // Predecessors by a plain branch; None when some other terminator
    // reaches the block.
    let mut preds: Vec<Option<Vec<usize>>> = vec![Some(Vec::new()); n];
    for (i, block) in func.blocks.iter().enumerate() {
        match &block.terminator {
            Terminator::Branch(l) => {
                if let Some(Some(p)) = index.get(l).map(|&t| &mut preds[t]) {
                    p.push(i);
                }
            }
            t => {
                let mut targets: Vec<BlockId> = Vec::new();
                match t {
                    Terminator::CondBranch { true_label, false_label, .. } => targets.extend([*true_label, *false_label]),
                    Terminator::Switch { cases, default, .. } => {
                        targets.push(*default);
                        targets.extend(cases.iter().map(|(_, l)| *l));
                    }
                    Terminator::IndirectBranch { possible_targets, .. } => targets.extend(possible_targets.iter().copied()),
                    Terminator::Return(_) | Terminator::Unreachable | Terminator::Branch(_) => {}
                }
                for l in targets {
                    if let Some(&t) = index.get(&l) {
                        preds[t] = None;
                    }
                }
            }
        }
    }
    for (b, ps) in preds.iter().enumerate().skip(1) {
        let Some(ps) = ps.as_ref().filter(|p| !p.is_empty()) else { continue };
        // The restores that can go first: no line before them touches p,
        // or writes t (read only there).
        let block = &func.blocks[b];
        let mut touched: FxHashSet<u32> = FxHashSet::default();
        let mut moved = vec![false; block.instructions.len()];
        for (i, inst) in block.instructions.iter().enumerate() {
            if let Instruction::Copy { dest: p, src: Operand::Value(t) } = inst {
                if uses.get(&t.0) == Some(&1) && !touched.contains(&p.0) && !touched.contains(&t.0) {
                    moved[i] = true;
                    continue;
                }
            }
            inst.for_each_used_value(|v| { touched.insert(v); });
            if let Some(d) = inst.dest() {
                touched.insert(d.0);
            }
        }
        let k = moved.iter().filter(|&&m| m).count();
        if k == 0 {
            continue;
        }
        let block = &mut func.blocks[b];
        let restores: Vec<Instruction> = block.instructions.iter().zip(&moved).filter(|(_, &m)| m).map(|(i, _)| i.clone()).collect();
        // Spans out of step with the instructions (an earlier pass's) say
        // nothing about which line is which: dropped.
        if block.source_spans.len() != moved.len() {
            block.source_spans.clear();
        }
        let mut i = 0;
        block.instructions.retain(|_| { i += 1; !moved[i - 1] });
        if !block.source_spans.is_empty() {
            let mut i = 0;
            block.source_spans.retain(|_| { i += 1; !moved[i - 1] });
        }
        for &p in ps {
            let block = &mut func.blocks[p];
            let start = block.instructions.len();
            block.instructions.extend(restores.iter().cloned());
            if !block.source_spans.is_empty() {
                block.source_spans.extend(std::iter::repeat_n(crate::common::source::Span::dummy(), k));
            }
            drop_round_trip_copies(block, start);
        }
    }
}

/// Drop each restore `p = copy t` (at `start` or later) with the save
/// `t = copy p` it undoes: neither p nor t written in between.
fn drop_round_trip_copies(block: &mut BasicBlock, start: usize) {
    let mut gone = vec![false; block.instructions.len()];
    for r in start..block.instructions.len() {
        let Instruction::Copy { dest: p, src: Operand::Value(t) } = block.instructions[r] else { continue };
        for s in (0..r).rev() {
            if gone[s] {
                continue;
            }
            let inst = &block.instructions[s];
            let def = inst.dest().map(|d| d.0);
            if let Instruction::Copy { dest, src: Operand::Value(v) } = inst {
                if dest.0 == t.0 && v.0 == p.0 {
                    gone[s] = true;
                    gone[r] = true;
                    break;
                }
            }
            // Calls and asm may write values only through their dest.
            if def == Some(p.0) || def == Some(t.0) {
                break;
            }
        }
    }
    if !gone.contains(&true) {
        return;
    }
    let mut i = 0;
    block.instructions.retain(|_| { i += 1; !gone[i - 1] });
    if !block.source_spans.is_empty() {
        let mut i = 0;
        block.source_spans.retain(|_| { i += 1; !gone[i - 1] });
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_no_conflicts_independent_phis() {
        // a = 1, b = 2 — no overlap between dests and sources
        let copies = vec![(10, Some(1)), (20, Some(2))];
        let result = find_conflicting_phis(&copies);
        assert!(result.is_empty(), "Independent phis should have no conflicts");
    }

    #[test]
    fn test_swap_pattern() {
        // a = b, b = a — classic swap cycle
        let copies = vec![(10, Some(20)), (20, Some(10))];
        let result = find_conflicting_phis(&copies);
        assert!(result.contains(&0), "First phi in swap should be conflicting");
        assert!(result.contains(&1), "Second phi in swap should be conflicting");
    }

    #[test]
    fn test_three_way_cycle() {
        // a = b, b = c, c = a — three-way rotation
        let copies = vec![(10, Some(20)), (20, Some(30)), (30, Some(10))];
        let result = find_conflicting_phis(&copies);
        assert_eq!(result.len(), 3, "All three phis in a 3-way cycle should be conflicting");
    }

    #[test]
    fn test_mixed_conflicting_and_non_conflicting() {
        // a = b, b = a (conflict), c = 99 (independent)
        let copies = vec![(10, Some(20)), (20, Some(10)), (30, Some(99))];
        let result = find_conflicting_phis(&copies);
        assert!(result.contains(&0));
        assert!(result.contains(&1));
        assert!(!result.contains(&2), "Independent phi should not be marked conflicting");
    }

    #[test]
    fn test_self_copy_not_conflicting() {
        // a = a — self-copy, not a conflict
        let copies = vec![(10, Some(10)), (20, Some(30))];
        let result = find_conflicting_phis(&copies);
        assert!(result.is_empty(), "Self-copy should not be conflicting");
    }

    #[test]
    fn test_constant_source_not_conflicting() {
        // a = <const>, b = <const> — None sources (constants)
        let copies = vec![(10, None), (20, None)];
        let result = find_conflicting_phis(&copies);
        assert!(result.is_empty(), "Constant sources should have no conflicts");
    }

    #[test]
    fn test_chain_pattern_conservative() {
        // a = b, b = c — b is dest of phi 1 and source of phi 0
        // phi 0 reads b (which is the dest of phi 1), so phi 0 is directly conflicting.
        // The safety net then marks phi 1 because its dest (b=20) is a source
        // of a conflicting phi.
        let copies = vec![(10, Some(20)), (20, Some(30))];
        let result = find_conflicting_phis(&copies);
        assert!(result.contains(&0), "Chain phi reading overwritten dest should be conflicting");
        assert!(result.contains(&1), "Chain phi whose dest is read by conflicting phi should also be marked");
    }

    /// A dispatch loop's head restoring two phis from temporaries: entered
    /// from 0, and from latches 2 (leaving v1 alone) and 3 (changing both).
    /// `cond` makes latch 3 a conditional branch.
    fn dispatch(cond: bool) -> IrFunction {
        use crate::common::types::IrType;
        use crate::ir::reexports::{IrBinOp, IrConst};
        let v = |n| Operand::Value(Value(n));
        let c = |n| Operand::Const(IrConst::I64(n));
        let block = |l: u32, instructions: Vec<Instruction>, terminator| BasicBlock { label: BlockId(l), instructions, terminator, source_spans: Vec::new() };
        let mut func = IrFunction::new("test".to_string(), IrType::I64, vec![], false);
        let latch3 = if cond {
            Terminator::CondBranch { cond: v(5), true_label: BlockId(1), false_label: BlockId(4) }
        } else {
            Terminator::Branch(BlockId(1))
        };
        func.blocks = vec![
            block(0, vec![
                Instruction::Copy { dest: Value(20), src: c(0) },
                Instruction::Copy { dest: Value(21), src: c(1) },
            ], Terminator::Branch(BlockId(1))),
            block(1, vec![
                Instruction::BinOp { dest: Value(5), op: IrBinOp::Add, lhs: v(22), rhs: c(1), ty: IrType::I64 },
                Instruction::Copy { dest: Value(1), src: v(20) },
                Instruction::Copy { dest: Value(2), src: v(21) },
            ], Terminator::CondBranch { cond: v(5), true_label: BlockId(2), false_label: BlockId(3) }),
            block(2, vec![
                Instruction::Copy { dest: Value(20), src: v(1) },
                Instruction::Copy { dest: Value(21), src: v(4) },
            ], Terminator::Branch(BlockId(1))),
            block(3, vec![
                Instruction::Copy { dest: Value(20), src: v(2) },
                Instruction::Copy { dest: Value(21), src: v(1) },
            ], latch3),
            block(4, vec![], Terminator::Return(Some(v(1)))),
        ];
        func.next_value_id = 23;
        func
    }

    #[test]
    fn phi_restores_move_into_latches_and_round_trips_go() {
        let mut func = dispatch(false);
        sink_phi_restores_in_function(&mut func);
        let copies = |b: usize| -> Vec<(u32, u32)> {
            func.blocks[b].instructions.iter().filter_map(|i| match i {
                Instruction::Copy { dest, src: Operand::Value(s) } => Some((dest.0, s.0)),
                Instruction::Copy { dest, .. } => Some((dest.0, u32::MAX)),
                _ => None,
            }).collect()
        };
        // The head keeps only its own work.
        assert_eq!(func.blocks[1].instructions.len(), 1);
        assert_eq!(copies(0), vec![(20, u32::MAX), (21, u32::MAX), (1, 20), (2, 21)]);
        // Latch 2 saved v1 only to restore it.
        assert_eq!(copies(2), vec![(21, 4), (2, 21)]);
        // Latch 3 swaps them, through the temporaries.
        assert_eq!(copies(3), vec![(20, 2), (21, 1), (1, 20), (2, 21)]);
    }

    #[test]
    fn phi_restores_move_from_a_head_whose_spans_are_out_of_step() {
        // An earlier pass left the head one span more than instructions.
        let mut func = dispatch(false);
        func.blocks[1].source_spans = vec![crate::common::source::Span::dummy(); 4];
        sink_phi_restores_in_function(&mut func);
        assert_eq!(func.blocks[1].instructions.len(), 1);
        assert!(func.blocks[1].source_spans.is_empty());
    }

    #[test]
    fn phi_restores_stay_with_a_conditional_predecessor() {
        let mut func = dispatch(true);
        sink_phi_restores_in_function(&mut func);
        assert_eq!(func.blocks[1].instructions.len(), 3);
        assert_eq!(func.blocks[2].instructions.len(), 2);
    }
}
