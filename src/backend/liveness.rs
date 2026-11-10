//! Liveness analysis for IR values.
//!
//! Computes live intervals for each IR value in an IrFunction. A live interval
//! represents the range [def_point, last_use_point] where a value is live and
//! needs to be preserved (either in a register or a stack slot).
//!
//! The analysis supports loops via backward dataflow iteration:
//! 1. First, assign sequential program points to all instructions and terminators.
//! 2. Run backward dataflow to compute live-in/live-out sets for each block.
//!    This correctly handles values that are live across loop back-edges.
//! 3. Build intervals by taking the union of def/use points and live-through blocks.
//!
//! ## Performance
//!
//! The dataflow uses compact bitsets instead of hash sets for gen/kill/live_in/live_out.
//! Value IDs are remapped to a dense [0..N) range so bitsets are minimal size.
//! This eliminates per-iteration heap allocation and replaces hash-table operations
//! with fast word-level bitwise ops (union = OR, difference = AND-NOT, equality = ==).

use crate::common::fx_hash::{FxHashMap, FxHashSet};
use crate::common::types::IrType;
use crate::ir::reexports::{
    Instruction,
    IrBinOp,
    IrConst,
    IrFunction,
    Operand,
    Terminator,
    Value,
};

/// A live interval for an IR value: [start, end] in program point numbering.
/// start = the point where the value is defined
/// end = the last point where the value is used
#[derive(Debug, Clone, Copy)]
pub struct LiveInterval {
    pub start: u32,
    pub end: u32,
    pub value_id: u32,
}

/// Result of liveness analysis: maps value IDs to their live intervals.
pub struct LivenessResult {
    pub intervals: Vec<LiveInterval>,
    /// Program points that are Call or CallIndirect instructions.
    /// Used by the register allocator to identify values that cross call boundaries.
    pub call_points: Vec<u32>,
    /// The subset of `call_points` that are calls passing only scalar
    /// arguments in registers (see `is_reg_args_only_call`), each with the
    /// value the call defines, if any.
    pub reg_args_call_points: Vec<(u32, Option<u32>)>,
    /// Program points of instructions whose AArch64 codegen may use x0-x8
    /// as scratch registers (128-bit integer and long double operations,
    /// atomics, intrinsics, inline asm, varargs, dynamic allocas): the
    /// call-free registers are busy there, as at calls.
    pub scratch_points: Vec<u32>,
    /// Estimated execution frequency of each block (block_index -> freq),
    /// the entry's being 1 (compute_block_freq). The register allocator
    /// weights uses by it.
    pub block_freq: Vec<f64>,
    /// Whether block_freq comes from a profile (real counts per call, which
    /// an interpreter loop takes far past the static estimates' range).
    pub profiled: bool,
    /// For values whose liveness has holes, the sorted, disjoint pieces of
    /// their interval in which they are actually live (one per run of blocks
    /// they are live in). A value missing here is live over its whole
    /// interval. The register allocator lets other values use a register in
    /// the holes, and a call in a hole does not count as one the value spans.
    pub segments: FxHashMap<u32, Vec<(u32, u32)>>,
}

impl LivenessResult {
    /// The pieces of `iv` in which its value is live (see `segments`).
    pub fn segments_of(&self, iv: &LiveInterval) -> Vec<(u32, u32)> {
        match self.segments.get(&iv.value_id) {
            Some(segs) => segs.clone(),
            None => vec![(iv.start, iv.end)],
        }
    }
}

// ── Compact bitset for dataflow ──────────────────────────────────────────────

/// A compact bitset stored as a contiguous slice of u64 words.
/// Supports O(1) insert/contains and O(n/64) union/difference/equality.
#[derive(Clone)]
struct BitSet {
    words: Vec<u64>,
}

impl BitSet {
    /// Create a new empty bitset that can hold indices [0..num_bits).
    fn new(num_bits: usize) -> Self {
        let num_words = num_bits.div_ceil(64);
        Self { words: vec![0u64; num_words] }
    }

    #[inline(always)]
    fn insert(&mut self, idx: usize) {
        let word = idx / 64;
        let bit = idx % 64;
        self.words[word] |= 1u64 << bit;
    }

    #[inline(always)]
    fn contains(&self, idx: usize) -> bool {
        let word = idx / 64;
        let bit = idx % 64;
        (self.words[word] >> bit) & 1 != 0
    }

    /// self = self | other. Returns true if self changed.
    fn union_with(&mut self, other: &BitSet) -> bool {
        let mut changed = false;
        for (w, o) in self.words.iter_mut().zip(other.words.iter()) {
            let old = *w;
            *w |= *o;
            changed |= *w != old;
        }
        changed
    }

    /// Computes: self = gen ∪ (out - kill) in one pass. Returns true if self changed.
    fn assign_gen_union_out_minus_kill(&mut self, gen: &BitSet, out: &BitSet, kill: &BitSet) -> bool {
        let mut changed = false;
        for i in 0..self.words.len() {
            let new_val = gen.words[i] | (out.words[i] & !kill.words[i]);
            if new_val != self.words[i] {
                self.words[i] = new_val;
                changed = true;
            }
        }
        changed
    }

    /// Iterate over all set bits, calling f(bit_index) for each.
    fn for_each_set_bit(&self, mut f: impl FnMut(usize)) {
        for (word_idx, &word) in self.words.iter().enumerate() {
            if word == 0 { continue; }
            let base = word_idx * 64;
            let mut w = word;
            while w != 0 {
                let tz = w.trailing_zeros() as usize;
                f(base + tz);
                w &= w - 1; // clear lowest set bit
            }
        }
    }

    /// Clear all bits.
    fn clear(&mut self) {
        for w in &mut self.words {
            *w = 0;
        }
    }
}

/// Intermediate state built during Phase 1 (program point assignment and gen/kill).
struct ProgramPointState {
    block_start_points: Vec<u32>,
    block_end_points: Vec<u32>,
    def_points: Vec<u32>,
    last_use_points: Vec<u32>,
    block_gen: Vec<BitSet>,
    block_kill: Vec<BitSet>,
    block_id_to_idx: FxHashMap<u32, usize>,
    setjmp_block_indices: Vec<usize>,
    call_points: Vec<u32>,
    reg_args_call_points: Vec<(u32, Option<u32>)>,
    scratch_points: Vec<u32>,
    num_points: u32,
}

/// Compute live intervals for all non-alloca values in a function.
///
/// Uses backward dataflow analysis to correctly handle loops:
/// - live_in[B] = gen[B] ∪ (live_out[B] - kill[B])
/// - live_out[B] = ∪ live_in[S] for all successors S of B
///
/// Values that are live-in to a block have their interval extended to cover
/// from the block's start point through the entire block. This correctly
/// extends intervals through loop back-edges.
pub fn compute_live_intervals(func: &IrFunction) -> LivenessResult {
    let num_blocks = func.blocks.len();
    if num_blocks == 0 {
        return LivenessResult {
            intervals: Vec::new(), call_points: Vec::new(), reg_args_call_points: Vec::new(), scratch_points: Vec::new(),
            block_freq: Vec::new(), profiled: false, segments: FxHashMap::default(),
        };
    }

    let alloca_set = collect_alloca_set(func);
    let (value_ids, id_to_dense) = build_dense_value_map(func, &alloca_set);

    let num_values = value_ids.len();
    if num_values == 0 {
        return LivenessResult {
            intervals: Vec::new(), call_points: Vec::new(), reg_args_call_points: Vec::new(), scratch_points: Vec::new(),
            block_freq: Vec::new(), profiled: false, segments: FxHashMap::default(),
        };
    }

    // Phase 1: Assign program points and build gen/kill sets.
    let mut ps = assign_program_points(func, num_blocks, num_values, &alloca_set, &id_to_dense);

    // Phase 1b: Extend liveness of GEP base values for GEP folding.
    let gep_base_uses = extend_gep_base_liveness(func, &alloca_set, &id_to_dense,
        &mut ps.last_use_points, &mut ps.block_gen);

    // Phase 1c: Extend liveness for F128 source pointers.
    let f128_ptrs = extend_f128_source_liveness(func, &alloca_set, &id_to_dense,
        &mut ps.last_use_points, &mut ps.block_gen);

    // Phase 2: Build successor lists for the CFG.
    let successors = build_successor_lists(func, num_blocks, &ps.block_id_to_idx);

    // Phase 2b: Estimate each block's execution frequency, or take it from
    // a profile (IrFunction::profile): its count per call. Blocks made
    // after the profile point keep the estimate.
    let mut block_freq = compute_block_freq(&successors, num_blocks);
    let mut profiled = false;
    if let Some(profile) = &func.profile {
        if let Some(&calls) = func.blocks.first().and_then(|b| profile.get(&b.label.0)).filter(|&&c| c > 0) {
            for (b, block) in func.blocks.iter().enumerate() {
                if let Some(&c) = profile.get(&block.label.0) {
                    block_freq[b] = c as f64 / calls as f64;
                }
            }
            profiled = true;
        }
    }

    // Phase 3: Backward dataflow to compute live-in/live-out per block.
    let (live_in, live_out) = run_backward_dataflow(
        num_blocks, num_values, &successors, &ps.block_gen, &ps.block_kill,
    );

    // Phase 4: Extend intervals for values that are live-in or live-out of blocks.
    extend_intervals_from_liveness(
        num_blocks, &live_in, &live_out,
        &ps.block_start_points, &ps.block_end_points,
        &mut ps.def_points, &mut ps.last_use_points,
    );

    // Phase 4b: Handle setjmp/longjmp.
    extend_intervals_for_setjmp(
        &ps.setjmp_block_indices, ps.num_points, &live_in, &live_out,
        &mut ps.last_use_points,
    );

    // Phase 5: Build and sort intervals.
    let intervals = build_intervals(&value_ids, &ps.def_points, &ps.last_use_points);

    // Phase 6: Live segments. setjmp functions keep whole intervals, since a
    // longjmp can return to any point after the setjmp.
    let segments = if ps.setjmp_block_indices.is_empty() {
        build_live_segments(func, &value_ids, &ps, &live_in, &live_out, &alloca_set, &id_to_dense, &gep_base_uses, &f128_ptrs)
    } else {
        FxHashMap::default()
    };

    LivenessResult {
        intervals,
        call_points: ps.call_points,
        reg_args_call_points: ps.reg_args_call_points,
        scratch_points: ps.scratch_points,
        block_freq,
        profiled,
        segments,
    }
}

/// Phase 6: Split each value's interval into the blocks it is live in.
///
/// In a block where the value is live-in, its piece starts at the block start,
/// otherwise at its first def or use there. Where it is live-out, the piece
/// ends at the block end, otherwise at the earlier of the block end and the
/// end of its interval (which covers the use points that the GEP and F128
/// extensions add without a visible operand). Adjacent pieces are merged, and
/// only values left with more than one piece are returned.
fn build_live_segments(
    func: &IrFunction,
    value_ids: &[u32],
    ps: &ProgramPointState,
    live_in: &[BitSet],
    live_out: &[BitSet],
    alloca_set: &FxHashSet<u32>,
    id_to_dense: &FxHashMap<u32, usize>,
    gep_base_uses: &[(usize, usize, u32)],
    f128_ptrs: &FxHashSet<usize>,
) -> FxHashMap<u32, Vec<(u32, u32)>> {
    let num_values = value_ids.len();
    // Last read of each value in the current block (u32::MAX: none yet).
    let mut last_read: Vec<u32> = vec![u32::MAX; num_values];
    let mut read_touched: Vec<usize> = Vec::new();
    let mut segs: Vec<Vec<(u32, u32)>> = vec![Vec::new(); num_values];
    let mut first_seen: Vec<u32> = vec![u32::MAX; num_values];
    let mut touched: Vec<usize> = Vec::new();
    // A value live into a block that is also defined there (a lowered phi:
    // the loop's latch copy) is dead from its last use before that
    // definition until the definition. Per block: (first def point, last
    // use point before it).
    let mut redef: Vec<(u32, u32)> = vec![(u32::MAX, u32::MAX); num_values];
    let mut redef_touched: Vec<usize> = Vec::new();

    for (bi, block) in func.blocks.iter().enumerate() {
        let block_start = ps.block_start_points[bi];
        let block_end = ps.block_end_points[bi];
        let note = |vid: u32, point: u32, first_seen: &mut [u32], touched: &mut Vec<usize>| {
            if alloca_set.contains(&vid) {
                return;
            }
            if let Some(&dense) = id_to_dense.get(&vid) {
                if first_seen[dense] == u32::MAX {
                    first_seen[dense] = point;
                    touched.push(dense);
                }
            }
        };
        for (i, inst) in block.instructions.iter().enumerate() {
            let point = block_start + i as u32;
            {
                let mut read = |vid: u32| {
                    if let Some(&d) = id_to_dense.get(&vid) {
                        if last_read[d] == u32::MAX {
                            read_touched.push(d);
                        }
                        last_read[d] = point;
                    }
                };
                for_each_operand_in_instruction(inst, |op| {
                    if let Operand::Value(v) = op {
                        read(v.0);
                    }
                });
                for_each_value_use_in_instruction(inst, |v| read(v.0));
                if let Instruction::InlineAsm { outputs, .. } = inst {
                    for (_, out_val, _) in outputs {
                        read(out_val.0);
                    }
                }
            }
            let mut used_here = |vid: u32, redef: &mut [(u32, u32)]| {
                if let Some(&dense) = id_to_dense.get(&vid) {
                    if redef[dense].0 == u32::MAX {
                        if redef[dense].1 == u32::MAX {
                            redef_touched.push(dense);
                        }
                        redef[dense].1 = point;
                    }
                }
            };
            for_each_operand_in_instruction(inst, |op| {
                if let Operand::Value(v) = op {
                    note(v.0, point, &mut first_seen, &mut touched);
                    used_here(v.0, &mut redef);
                }
            });
            for_each_value_use_in_instruction(inst, |v| {
                note(v.0, point, &mut first_seen, &mut touched);
                used_here(v.0, &mut redef);
            });
            if let Instruction::InlineAsm { outputs, .. } = inst {
                for (_, out_val, _) in outputs {
                    note(out_val.0, point, &mut first_seen, &mut touched);
                    used_here(out_val.0, &mut redef);
                }
            }
            if let Some(dest) = inst.dest() {
                note(dest.0, point, &mut first_seen, &mut touched);
                if let Some(&dense) = id_to_dense.get(&dest.0) {
                    if redef[dense].0 == u32::MAX {
                        if redef[dense].1 == u32::MAX {
                            redef_touched.push(dense);
                        }
                        redef[dense].0 = point;
                    }
                }
            }
        }
        for_each_operand_in_terminator(&block.terminator, |op| {
            if let Operand::Value(v) = op {
                note(v.0, block_end, &mut first_seen, &mut touched);
                if let Some(&d) = id_to_dense.get(&v.0) {
                    if last_read[d] == u32::MAX {
                        read_touched.push(d);
                    }
                    last_read[d] = block_end;
                }
            }
        });
        // Reads the GEP extension adds (a folded GEP's base at its loads and
        // stores).
        for &(b, d, point) in gep_base_uses {
            if b == bi && (last_read[d] == u32::MAX || point > last_read[d]) {
                if last_read[d] == u32::MAX {
                    read_touched.push(d);
                }
                last_read[d] = point;
            }
        }

        let mut add = |dense: usize, lo: u32, cap: u32, apart: bool| {
            let iv_start = ps.def_points[dense];
            if iv_start == u32::MAX {
                return;
            }
            let last = ps.last_use_points[dense];
            let iv_end = if last == u32::MAX { iv_start } else { last.max(iv_start) };
            let lo = lo.max(iv_start);
            // Not live out: the piece ends at the value's last read in the
            // block (an F128 load's pointer, extended to the loaded value's
            // last use without a read here, keeps the interval's end).
            let hi = if live_out[bi].contains(dense) {
                block_end
            } else if last_read[dense] != u32::MAX && !f128_ptrs.contains(&dense) {
                last_read[dense].max(lo)
            } else {
                block_end.min(iv_end)
            }.min(cap);
            if hi < lo {
                return;
            }
            let list = &mut segs[dense];
            match list.last_mut() {
                // (A piece starting at a redefinition stays apart from the one
                // ending just before it, so a value defined in between, at the
                // use and read by the redefinition, may share the register.)
                Some(prev) if prev.1 >= lo || (!apart && prev.1 + 1 == lo) => prev.1 = prev.1.max(hi),
                _ => list.push((lo, hi)),
            }
        };
        live_in[bi].for_each_set_bit(|dense| match redef[dense] {
            // Live in, last read at `used`, then redefined at `def`: two
            // pieces. (Live in with no read before the redefinition can only
            // be over-approximated liveness; keep the whole block.)
            (def, used) if def != u32::MAX && used != u32::MAX && used < def => {
                add(dense, block_start, used, false);
                add(dense, def, u32::MAX, true);
            }
            _ => add(dense, block_start, u32::MAX, false),
        });
        for dense in redef_touched.drain(..) {
            redef[dense] = (u32::MAX, u32::MAX);
        }
        for &dense in &touched {
            // Not live in: the piece starts at a definition (each edge
            // block of a lowered phi copying into it), apart from the piece
            // of the block laid out before.
            if !live_in[bi].contains(dense) {
                add(dense, first_seen[dense], u32::MAX, true);
            }
            first_seen[dense] = u32::MAX;
        }
        touched.clear();
        for d in read_touched.drain(..) {
            last_read[d] = u32::MAX;
        }
    }

    let mut result = FxHashMap::default();
    for (dense, list) in segs.into_iter().enumerate() {
        if list.len() > 1 {
            result.insert(value_ids[dense], list);
        }
    }
    result
}

/// Collect alloca values (not register-allocatable).
fn collect_alloca_set(func: &IrFunction) -> FxHashSet<u32> {
    let mut alloca_set: FxHashSet<u32> = FxHashSet::default();
    for block in &func.blocks {
        for inst in &block.instructions {
            if let Instruction::Alloca { dest, .. } = inst {
                alloca_set.insert(dest.0);
            }
        }
    }
    alloca_set
}

/// Collect all non-alloca value IDs and build a dense remapping:
/// sparse value_id -> dense index in [0..num_values).
fn build_dense_value_map(func: &IrFunction, alloca_set: &FxHashSet<u32>) -> (Vec<u32>, FxHashMap<u32, usize>) {
    let mut value_ids: Vec<u32> = Vec::new();
    let mut seen: FxHashSet<u32> = FxHashSet::default();

    let maybe_add = |id: u32, alloca_set: &FxHashSet<u32>, seen: &mut FxHashSet<u32>, value_ids: &mut Vec<u32>| {
        if !alloca_set.contains(&id) && seen.insert(id) {
            value_ids.push(id);
        }
    };

    for block in &func.blocks {
        for inst in &block.instructions {
            if let Some(dest) = inst.dest() {
                maybe_add(dest.0, alloca_set, &mut seen, &mut value_ids);
            }
            for_each_operand_in_instruction(inst, |op| {
                if let Operand::Value(v) = op {
                    maybe_add(v.0, alloca_set, &mut seen, &mut value_ids);
                }
            });
            for_each_value_use_in_instruction(inst, |v| {
                maybe_add(v.0, alloca_set, &mut seen, &mut value_ids);
            });
        }
        for_each_operand_in_terminator(&block.terminator, |op| {
            if let Operand::Value(v) = op {
                maybe_add(v.0, alloca_set, &mut seen, &mut value_ids);
            }
        });
    }

    let mut id_to_dense: FxHashMap<u32, usize> = FxHashMap::default();
    id_to_dense.reserve(value_ids.len());
    for (dense_idx, &vid) in value_ids.iter().enumerate() {
        id_to_dense.insert(vid, dense_idx);
    }

    (value_ids, id_to_dense)
}

/// Phase 1: Assign sequential program points to all instructions/terminators
/// and build per-block gen/kill bitsets, def/use point arrays, call points,
/// and setjmp block indices.
fn assign_program_points(
    func: &IrFunction,
    num_blocks: usize,
    num_values: usize,
    alloca_set: &FxHashSet<u32>,
    id_to_dense: &FxHashMap<u32, usize>,
) -> ProgramPointState {
    let mut point: u32 = 0;
    let mut block_start_points: Vec<u32> = Vec::with_capacity(num_blocks);
    let mut block_end_points: Vec<u32> = Vec::with_capacity(num_blocks);
    let mut def_points: Vec<u32> = vec![u32::MAX; num_values];
    let mut last_use_points: Vec<u32> = vec![u32::MAX; num_values];
    let mut block_gen: Vec<BitSet> = Vec::with_capacity(num_blocks);
    let mut block_kill: Vec<BitSet> = Vec::with_capacity(num_blocks);
    let mut block_id_to_idx: FxHashMap<u32, usize> = FxHashMap::default();
    let mut setjmp_block_indices: Vec<usize> = Vec::new();
    let mut call_points: Vec<u32> = Vec::new();
    let mut reg_args_call_points: Vec<(u32, Option<u32>)> = Vec::new();
    let mut scratch_points: Vec<u32> = Vec::new();

    for (block_idx, block) in func.blocks.iter().enumerate() {
        block_id_to_idx.insert(block.label.0, block_idx);
        let block_start = point;
        block_start_points.push(block_start);
        let mut gen = BitSet::new(num_values);
        let mut kill = BitSet::new(num_values);

        for inst in &block.instructions {
            if is_returns_twice_call(inst) {
                setjmp_block_indices.push(block_idx);
            }

            // Track call instruction program points for register allocation.
            // InlineAsm instructions with register operands are treated as call
            // points because they may clobber caller-saved registers (r8-r11 on
            // x86). This ensures values whose live ranges span inline asm get
            // callee-saved registers (which survive inline asm), while values NOT
            // spanning inline asm can safely use caller-saved registers.
            //
            // Empty inline asm barriers (e.g., `asm volatile("" ::: "memory")`)
            // are NOT call points since they don't use any GP registers. These
            // are common in the kernel for memory barriers, preempt_disable/enable,
            // etc. Treating them as call points would unnecessarily force values
            // into callee-saved registers across simple barriers.
            match inst {
                Instruction::Call { info, .. } | Instruction::CallIndirect { info, .. } => {
                    call_points.push(point);
                    if is_reg_args_only_call(info) && !is_returns_twice_call(inst) {
                        reg_args_call_points.push((point, info.dest.map(|d| d.0)));
                    }
                }
                Instruction::InlineAsm { outputs, inputs, .. } => {
                    // Only treat as call point if the asm has register operands
                    // (outputs or inputs that bind to GP registers).
                    if !outputs.is_empty() || !inputs.is_empty() {
                        call_points.push(point);
                    }
                }
                // Memcpy uses rep movsb which clobbers rdi, rsi, rcx.
                // VaArg/VaCopy/VaArgStruct clobber rdi/rsi for struct copy.
                // VaStart is included conservatively for safety.
                // Treat these as call points so caller-saved registers (including
                // rdi/rsi) are not allocated across them.
                Instruction::Memcpy { .. }
                | Instruction::VaArg { .. }
                | Instruction::VaStart { .. }
                | Instruction::VaCopy { .. }
                | Instruction::VaArgStruct { .. } => {
                    call_points.push(point);
                }
                // i128 div/rem emit implicit calls to __divti3/__udivti3/__modti3/__umodti3.
                // These are BinOp instructions at the IR level but generate `call` at the
                // assembly level, clobbering all caller-saved registers. We must treat them
                // as call points so the register allocator doesn't assign caller-saved
                // registers to values whose live ranges span these operations.
                Instruction::BinOp { op, ty, .. }
                    if matches!(ty, IrType::I128 | IrType::U128)
                        && matches!(op, IrBinOp::SDiv | IrBinOp::UDiv | IrBinOp::SRem | IrBinOp::URem) =>
                {
                    call_points.push(point);
                }
                // i128 <-> float casts emit implicit calls to compiler-rt helpers
                // (__floattidf/__fixdfti/etc.). Same reasoning as i128 div/rem above.
                Instruction::Cast { from_ty, to_ty, .. }
                    if (matches!(from_ty, IrType::I128 | IrType::U128) && to_ty.is_float())
                        || (from_ty.is_float() && matches!(to_ty, IrType::I128 | IrType::U128)) =>
                {
                    call_points.push(point);
                }
                _ => {}
            }

            if uses_scratch_regs(inst) {
                scratch_points.push(point);
            }

            record_instruction_uses_dense(inst, point, alloca_set, id_to_dense, &mut last_use_points);

            // Record InlineAsm output definitions BEFORE gen collection so
            // that promoted (non-alloca) outputs are in the kill set and won't
            // be treated as upward-exposed uses.
            //
            // Only kill output values that are first defined here (promoted asm
            // outputs). Output values already defined earlier (e.g., pointer
            // values passed through for indirect stores like `"=a"(*ptr)`) are
            // merely *used* by the InlineAsm — the asm reads the pointer from
            // the slot and stores through it, but does not overwrite the pointer
            // itself. Killing such values truncates their live interval, letting
            // the slot packer reuse their slot too early, which corrupts the
            // pointer on the next loop iteration.
            if let Instruction::InlineAsm { outputs, .. } = inst {
                for (_, out_val, _) in outputs {
                    if !alloca_set.contains(&out_val.0) {
                        if let Some(&dense) = id_to_dense.get(&out_val.0) {
                            if def_points[dense] == u32::MAX {
                                def_points[dense] = point;
                                kill.insert(dense);
                            }
                        }
                    }
                }
            }

            collect_instruction_gen_dense(inst, alloca_set, id_to_dense, &kill, &mut gen);

            if let Some(dest) = inst.dest() {
                if !alloca_set.contains(&dest.0) {
                    if let Some(&dense) = id_to_dense.get(&dest.0) {
                        if def_points[dense] == u32::MAX {
                            def_points[dense] = point;
                        }
                        kill.insert(dense);
                    }
                }
            }

            point += 1;
        }

        record_terminator_uses_dense(&block.terminator, point, alloca_set, id_to_dense, &mut last_use_points);
        collect_terminator_gen_dense(&block.terminator, alloca_set, id_to_dense, &kill, &mut gen);
        let block_end = point;
        block_end_points.push(block_end);
        point += 1;

        block_gen.push(gen);
        block_kill.push(kill);
    }

    ProgramPointState {
        block_start_points,
        block_end_points,
        def_points,
        last_use_points,
        block_gen,
        block_kill,
        block_id_to_idx,
        setjmp_block_indices,
        call_points,
        reg_args_call_points,
        scratch_points,
        num_points: point,
    }
}

/// Whether an instruction's AArch64 codegen may use argument registers
/// (x0-x8) as scratch: see LivenessResult::scratch_points.
pub(crate) fn uses_scratch_regs(inst: &Instruction) -> bool {
    let wide = |ty: &IrType| matches!(ty, IrType::I128 | IrType::U128 | IrType::F128);
    match inst {
        Instruction::BinOp { ty, .. } | Instruction::UnaryOp { ty, .. } | Instruction::Cmp { ty, .. }
        | Instruction::Load { ty, .. } | Instruction::Store { ty, .. } => wide(ty),
        Instruction::Cast { from_ty, to_ty, .. } => wide(from_ty) || wide(to_ty),
        Instruction::InlineAsm { .. } | Instruction::Intrinsic { .. } | Instruction::AtomicLoad { .. }
        | Instruction::AtomicStore { .. } | Instruction::AtomicRmw { .. } | Instruction::AtomicCmpxchg { .. }
        | Instruction::Fence { .. } | Instruction::VaArg { .. } | Instruction::VaStart { .. } | Instruction::VaEnd { .. }
        | Instruction::VaCopy { .. } | Instruction::VaArgStruct { .. } | Instruction::DynAlloca { .. } => true,
        _ => false,
    }
}

/// Phase 2: Build successor lists from block terminators and asm goto labels.
fn build_successor_lists(
    func: &IrFunction,
    num_blocks: usize,
    block_id_to_idx: &FxHashMap<u32, usize>,
) -> Vec<Vec<usize>> {
    let mut successors: Vec<Vec<usize>> = vec![Vec::new(); num_blocks];
    for (idx, block) in func.blocks.iter().enumerate() {
        for target_id in terminator_targets(&block.terminator) {
            if let Some(&target_idx) = block_id_to_idx.get(&target_id) {
                successors[idx].push(target_idx);
            }
        }
        // InlineAsm goto_labels are implicit control flow edges.
        for inst in &block.instructions {
            if let Instruction::InlineAsm { goto_labels, .. } = inst {
                for (_, label) in goto_labels {
                    if let Some(&target_idx) = block_id_to_idx.get(&label.0) {
                        if !successors[idx].contains(&target_idx) {
                            successors[idx].push(target_idx);
                        }
                    }
                }
            }
        }
    }
    successors
}

/// Phase 3: Backward dataflow to compute live-in/live-out per block.
/// live_in[B] = gen[B] ∪ (live_out[B] - kill[B])
/// live_out[B] = ∪ live_in[S] for all successors S of B
fn run_backward_dataflow(
    num_blocks: usize,
    num_values: usize,
    successors: &[Vec<usize>],
    block_gen: &[BitSet],
    block_kill: &[BitSet],
) -> (Vec<BitSet>, Vec<BitSet>) {
    let mut live_in: Vec<BitSet> = (0..num_blocks).map(|_| BitSet::new(num_values)).collect();
    let mut live_out: Vec<BitSet> = (0..num_blocks).map(|_| BitSet::new(num_values)).collect();
    let mut tmp_out = BitSet::new(num_values);

    // Iterate until fixpoint (backward order converges faster).
    // MAX_ITERATIONS is a safety bound for pathological irreducible control flow.
    let mut changed = true;
    let mut iteration = 0;
    const MAX_ITERATIONS: u32 = 50;
    while changed && iteration < MAX_ITERATIONS {
        changed = false;
        iteration += 1;

        for idx in (0..num_blocks).rev() {
            tmp_out.clear();
            for &succ in &successors[idx] {
                tmp_out.union_with(&live_in[succ]);
            }

            if tmp_out.words != live_out[idx].words {
                live_out[idx].words.copy_from_slice(&tmp_out.words);
                changed = true;
            }

            let in_changed = live_in[idx].assign_gen_union_out_minus_kill(
                &block_gen[idx], &live_out[idx], &block_kill[idx]
            );
            changed |= in_changed;
        }
    }

    (live_in, live_out)
}

/// Phase 4: Extend intervals for values that are live-in or live-out of blocks.
/// A value live-in to a block has its interval cover the entire block.
fn extend_intervals_from_liveness(
    num_blocks: usize,
    live_in: &[BitSet],
    live_out: &[BitSet],
    block_start_points: &[u32],
    block_end_points: &[u32],
    def_points: &mut [u32],
    last_use_points: &mut [u32],
) {
    for idx in 0..num_blocks {
        let start = block_start_points[idx];
        let end = block_end_points[idx];

        // Live in but not out: the value dies at its last use in the block,
        // which the use scan (and the GEP and F128 extensions) already
        // recorded, so only the block start is added here.
        live_in[idx].for_each_set_bit(|dense_idx| {
            let def_entry = &mut def_points[dense_idx];
            if *def_entry == u32::MAX || start < *def_entry {
                *def_entry = start;
            }
            let entry = &mut last_use_points[dense_idx];
            if *entry == u32::MAX || start > *entry {
                *entry = start;
            }
        });

        // Live out: either live in too (handled above) or defined in the
        // block, where its interval starts at the definition.
        live_out[idx].for_each_set_bit(|dense_idx| {
            let def_entry = &mut def_points[dense_idx];
            if *def_entry == u32::MAX {
                *def_entry = start;
            }
            let entry = &mut last_use_points[dense_idx];
            if *entry == u32::MAX {
                *entry = end;
            }
            if end > *entry {
                *entry = end;
            }
        });
    }
}

/// Phase 4b: Handle setjmp/longjmp — extend intervals for values live at
/// setjmp call points to the end of the function, preventing slot reuse.
fn extend_intervals_for_setjmp(
    setjmp_block_indices: &[usize],
    num_points: u32,
    live_in: &[BitSet],
    live_out: &[BitSet],
    last_use_points: &mut [u32],
) {
    if setjmp_block_indices.is_empty() {
        return;
    }
    let func_end = num_points.saturating_sub(1);
    for &sjb in setjmp_block_indices {
        live_in[sjb].for_each_set_bit(|dense_idx| {
            let entry = &mut last_use_points[dense_idx];
            if *entry == u32::MAX || func_end > *entry {
                *entry = func_end;
            }
        });
        live_out[sjb].for_each_set_bit(|dense_idx| {
            let entry = &mut last_use_points[dense_idx];
            if *entry == u32::MAX || func_end > *entry {
                *entry = func_end;
            }
        });
    }
}

/// Phase 5: Build sorted live intervals from def/use point arrays.
fn build_intervals(value_ids: &[u32], def_points: &[u32], last_use_points: &[u32]) -> Vec<LiveInterval> {
    let mut intervals: Vec<LiveInterval> = Vec::new();
    for (dense_idx, &vid) in value_ids.iter().enumerate() {
        let start = def_points[dense_idx];
        if start == u32::MAX { continue; }
        let end = last_use_points[dense_idx];
        let end = if end == u32::MAX { start } else { end.max(start) };
        intervals.push(LiveInterval { start, end, value_id: vid });
    }
    intervals.sort_unstable_by_key(|iv| iv.start);
    intervals
}

/// Extend liveness of GEP base values so that their registers remain valid
/// at Load/Store use points where the GEP offset can be folded into the
/// addressing mode.
///
/// For each GEP `%gep = gep %base, const_offset` whose result is only used
/// as a Load/Store ptr operand:
/// - Find all Load/Store instructions that use %gep as their ptr
/// - Record %base as "used" at those instruction program points
/// - Update the gen bitset for the block containing the Load/Store
///
/// This ensures the register allocator keeps %base alive through the folded
/// Load/Store, enabling safe `offset(%base_reg)` addressing at codegen time.
fn extend_gep_base_liveness(
    func: &IrFunction,
    alloca_set: &FxHashSet<u32>,
    id_to_dense: &FxHashMap<u32, usize>,
    last_use_points: &mut [u32],
    block_gen: &mut [BitSet],
) -> Vec<(usize, usize, u32)> {
    // The reads added: (block, value, point).
    let mut added: Vec<(usize, usize, u32)> = Vec::new();
    // Phase A: Identify foldable GEPs with non-alloca bases.
    // Same criteria as build_gep_fold_map in generation.rs.
    let mut gep_info: FxHashMap<u32, (u32, i64)> = FxHashMap::default(); // gep_dest_id -> (base_id, offset)

    for block in &func.blocks {
        for inst in &block.instructions {
            if let Instruction::GetElementPtr { dest, base, offset: Operand::Const(c), .. } = inst {
                // Skip alloca bases (already handled by existing fold logic)
                if alloca_set.contains(&base.0) {
                    continue;
                }
                let offset_val = match c {
                    IrConst::I64(n) => *n,
                    IrConst::I32(n) => *n as i64,
                    IrConst::I16(n) => *n as i64,
                    IrConst::I8(n) => *n as i64,
                    _ => continue,
                };
                if offset_val >= i32::MIN as i64 && offset_val <= i32::MAX as i64 {
                    gep_info.insert(dest.0, (base.0, offset_val));
                }
            }
        }
    }

    if gep_info.is_empty() {
        return added;
    }

    // Phase B: Verify each GEP dest is only used as Load/Store ptr operand.
    // If used elsewhere, remove from the map (not foldable).
    let mut non_foldable: FxHashSet<u32> = FxHashSet::default();

    for block in &func.blocks {
        for inst in &block.instructions {
            match inst {
                Instruction::Load { ptr, ty, .. } => {
                    // Load.ptr is foldable unless i128
                    if matches!(ty, IrType::I128 | IrType::U128)
                        && gep_info.contains_key(&ptr.0) {
                            non_foldable.insert(ptr.0);
                        }
                }
                Instruction::Store { val, ptr, ty, .. } => {
                    // Store.val is NOT foldable; Store.ptr is (unless i128)
                    if let Operand::Value(v) = val {
                        if gep_info.contains_key(&v.0) {
                            non_foldable.insert(v.0);
                        }
                    }
                    if matches!(ty, IrType::I128 | IrType::U128)
                        && gep_info.contains_key(&ptr.0) {
                            non_foldable.insert(ptr.0);
                        }
                }
                _ => {
                    // Any other use invalidates folding
                    for_each_operand_in_instruction(inst, |op| {
                        if let Operand::Value(v) = op {
                            if gep_info.contains_key(&v.0) {
                                non_foldable.insert(v.0);
                            }
                        }
                    });
                    for_each_value_use_in_instruction(inst, |v| {
                        if gep_info.contains_key(&v.0) {
                            non_foldable.insert(v.0);
                        }
                    });
                }
            }
        }
        for_each_operand_in_terminator(&block.terminator, |op| {
            if let Operand::Value(v) = op {
                if gep_info.contains_key(&v.0) {
                    non_foldable.insert(v.0);
                }
            }
        });
    }

    for id in &non_foldable {
        gep_info.remove(id);
    }

    if gep_info.is_empty() {
        return added;
    }

    // Phase C: Extend base liveness to Load/Store points that use foldable GEP results.
    let mut block_point: u32 = 0;
    let mut defined_here: FxHashSet<u32> = FxHashSet::default();
    for (bi, block) in func.blocks.iter().enumerate() {
        defined_here.clear();
        for inst in &block.instructions {
            match inst {
                Instruction::Load { .. } | Instruction::Store { .. } => {
                    let ptr_id = match inst {
                        Instruction::Load { ptr, .. } => ptr.0,
                        Instruction::Store { ptr, .. } => ptr.0,
                        _ => unreachable!("GEP analysis matched non-Load/Store instruction"),
                    };
                    if let Some(&(base_id, _offset)) = gep_info.get(&ptr_id) {
                        // Extend base's last_use to this program point
                        if !alloca_set.contains(&base_id) {
                            if let Some(&dense) = id_to_dense.get(&base_id) {
                                let entry = &mut last_use_points[dense];
                                if *entry == u32::MAX || block_point > *entry {
                                    *entry = block_point;
                                }
                                added.push((bi, dense, block_point));
                                // Also add to block's gen set (the base is "used"
                                // here), unless this block defines it first.
                                if !defined_here.contains(&base_id) {
                                    block_gen[bi].insert(dense);
                                }
                            }
                        }
                    }
                }
                _ => {}
            }
            if let Some(dest) = inst.dest() {
                defined_here.insert(dest.0);
            }
            block_point += 1;
        }
        // Account for terminator point
        block_point += 1;
    }
    added
}

/// Extend liveness for F128 load source pointers.
///
/// When an F128 value is loaded from memory, the codegen records which pointer
/// was used (via `track_f128_load` in state.rs). Later, during Call emission,
/// `emit_f128_operand_to_a0_a1` reads the pointer back from its stack slot to
/// reload the full 128-bit value. This creates an implicit dependency: the
/// pointer must remain live until the last use of the F128 dest value.
///
/// Without this extension, the Tier 2 liveness analysis considers the pointer
/// dead after the Load instruction, allowing the register allocator (and
/// subsequently the Tier 3 slot allocator) to reuse its slot. If another value
/// is placed in that slot before the Call, the pointer is corrupted and the
/// Call dereferences garbage (typically causing SIGSEGV).
fn extend_f128_source_liveness(
    func: &IrFunction,
    alloca_set: &FxHashSet<u32>,
    id_to_dense: &FxHashMap<u32, usize>,
    last_use_points: &mut [u32],
    block_gen: &mut [BitSet],
) -> FxHashSet<usize> {
    // Collect (ptr_id, dest_id) pairs for F128 loads with non-alloca pointers.
    let mut f128_loads: Vec<(u32, u32)> = Vec::new();
    for block in &func.blocks {
        for inst in &block.instructions {
            if let Instruction::Load { dest, ptr, ty, .. } = inst {
                if *ty == IrType::F128 && !alloca_set.contains(&ptr.0) {
                    f128_loads.push((ptr.0, dest.0));
                }
            }
        }
    }

    if f128_loads.is_empty() {
        return FxHashSet::default();
    }

    // Extend each pointer's last_use_point to match its dest's last_use_point.
    for &(ptr_id, dest_id) in &f128_loads {
        let dest_dense = id_to_dense.get(&dest_id).copied();
        let ptr_dense = id_to_dense.get(&ptr_id).copied();
        if let (Some(dd), Some(pd)) = (dest_dense, ptr_dense) {
            let dest_last = last_use_points[dd];
            if dest_last != u32::MAX {
                let ptr_entry = &mut last_use_points[pd];
                if *ptr_entry == u32::MAX || dest_last > *ptr_entry {
                    *ptr_entry = dest_last;
                }
            }
        }
    }

    // Update gen sets so backward dataflow propagation keeps the pointer live
    // in predecessor blocks when the dest value is used in a successor block.
    for (bi, block) in func.blocks.iter().enumerate() {
        for inst in &block.instructions {
            let mut check_use = |vid: u32| {
                for &(ptr_id, dest_id) in &f128_loads {
                    if vid == dest_id {
                        if let Some(&pd) = id_to_dense.get(&ptr_id) {
                            block_gen[bi].insert(pd);
                        }
                    }
                }
            };
            for_each_operand_in_instruction(inst, |op| {
                if let Operand::Value(v) = op {
                    check_use(v.0);
                }
            });
            for_each_value_use_in_instruction(inst, |v| {
                check_use(v.0);
            });
        }
        for_each_operand_in_terminator(&block.terminator, |op| {
            if let Operand::Value(v) = op {
                for &(ptr_id, dest_id) in &f128_loads {
                    if v.0 == dest_id {
                        if let Some(&pd) = id_to_dense.get(&ptr_id) {
                            block_gen[bi].insert(pd);
                        }
                    }
                }
            }
        });
    }
    // The pointers extended, for build_live_segments.
    f128_loads.iter().filter_map(|(ptr, _)| id_to_dense.get(ptr).copied()).collect()
}

/// Record uses of operands in an instruction (dense index version).
fn record_instruction_uses_dense(
    inst: &Instruction,
    point: u32,
    alloca_set: &FxHashSet<u32>,
    id_to_dense: &FxHashMap<u32, usize>,
    last_use: &mut [u32],
) {
    let mut record = |vid: u32| {
        if !alloca_set.contains(&vid) {
            if let Some(&dense) = id_to_dense.get(&vid) {
                let entry = &mut last_use[dense];
                if *entry == u32::MAX || point > *entry {
                    *entry = point;
                }
            }
        }
    };

    for_each_operand_in_instruction(inst, |op| {
        if let Operand::Value(v) = op {
            record(v.0);
        }
    });

    for_each_value_use_in_instruction(inst, |v| {
        record(v.0);
    });
}

/// Record uses in a terminator (dense index version).
fn record_terminator_uses_dense(
    term: &Terminator,
    point: u32,
    alloca_set: &FxHashSet<u32>,
    id_to_dense: &FxHashMap<u32, usize>,
    last_use: &mut [u32],
) {
    for_each_operand_in_terminator(term, |op| {
        if let Operand::Value(v) = op {
            if !alloca_set.contains(&v.0) {
                if let Some(&dense) = id_to_dense.get(&v.0) {
                    let entry = &mut last_use[dense];
                    if *entry == u32::MAX || point > *entry {
                        *entry = point;
                    }
                }
            }
        }
    });
}

/// Collect gen set for a block's instruction (dense bitset version).
fn collect_instruction_gen_dense(
    inst: &Instruction,
    alloca_set: &FxHashSet<u32>,
    id_to_dense: &FxHashMap<u32, usize>,
    kill: &BitSet,
    gen: &mut BitSet,
) {
    let mut add_use = |vid: u32| {
        if !alloca_set.contains(&vid) {
            if let Some(&dense) = id_to_dense.get(&vid) {
                if !kill.contains(dense) {
                    gen.insert(dense);
                }
            }
        }
    };

    for_each_operand_in_instruction(inst, |op| {
        if let Operand::Value(v) = op {
            add_use(v.0);
        }
    });

    for_each_value_use_in_instruction(inst, |v| {
        add_use(v.0);
    });
}

/// Collect gen set for a terminator (dense bitset version).
fn collect_terminator_gen_dense(
    term: &Terminator,
    alloca_set: &FxHashSet<u32>,
    id_to_dense: &FxHashMap<u32, usize>,
    kill: &BitSet,
    gen: &mut BitSet,
) {
    for_each_operand_in_terminator(term, |op| {
        if let Operand::Value(v) = op {
            if !alloca_set.contains(&v.0) {
                if let Some(&dense) = id_to_dense.get(&v.0) {
                    if !kill.contains(dense) {
                        gen.insert(dense);
                    }
                }
            }
        }
    });
}

/// Get successor block IDs from a terminator.
pub(crate) fn terminator_targets(term: &Terminator) -> Vec<u32> {
    match term {
        Terminator::Branch(target) => vec![target.0],
        Terminator::CondBranch { true_label, false_label, .. } => {
            vec![true_label.0, false_label.0]
        }
        Terminator::IndirectBranch { possible_targets, .. } => {
            possible_targets.iter().map(|t| t.0).collect()
        }
        Terminator::Switch { cases, default, .. } => {
            let mut targets = vec![default.0];
            for (_, label) in cases {
                targets.push(label.0);
            }
            targets
        }
        _ => vec![],
    }
}

/// Return true if the instruction is a call to setjmp, _setjmp, sigsetjmp, or __sigsetjmp.
/// A call whose arguments are all scalars (integers, pointers, f32/f64) or
/// AArch64 HFAs that fit in argument registers, with a scalar, void or HFA
/// result: no other struct, i128, or f128 values, no variadic or other sret
/// handling, no stack arguments. A
/// backend may let values read only as such a call's arguments, or defined
/// only by its result, use caller-saved registers
/// (`RegAllocConfig::caller_saved_at_call_boundaries`).
pub fn is_reg_args_only_call(info: &crate::ir::reexports::CallInfo) -> bool {
    let scalar = |ty: &IrType| matches!(ty,
        IrType::I8 | IrType::U8 | IrType::I16 | IrType::U16 | IrType::I32 | IrType::U32
        | IrType::I64 | IrType::U64 | IrType::Ptr | IrType::F32 | IrType::F64);
    // AArch64 HFAs (float structs, short vectors) are also register
    // arguments, in FP registers loaded through x17; an HFA result comes back
    // in FP registers without a pointer.
    let hfa = |i: usize| match info.struct_arg_riscv_float_classes.get(i) {
        Some(Some(crate::common::types::RiscvFloatClass::Hfa { count, .. })) => Some(*count),
        _ => None,
    };
    let struct_ok = info.struct_arg_sizes.iter().enumerate().all(|(i, s)| s.is_none() || hfa(i).is_some());
    let hfa_return = info.is_sret && hfa(0).is_some();
    // Struct arguments only: a split complex value's halves are counted as floats.
    let hfa_regs: usize = (0..info.args.len())
        .filter(|&i| !(hfa_return && i == 0) && info.struct_arg_sizes.get(i).copied().flatten().is_some())
        .filter_map(hfa).sum();
    let floats = info.arg_types.iter().filter(|t| t.is_float()).count();
    !info.is_variadic
        && (!info.is_sret || hfa_return)
        && struct_ok
        && info.arg_types.len() == info.args.len()
        && info.arg_types.iter().all(scalar)
        && floats + hfa_regs <= 8
        && info.args.len() - floats <= 8
        && (info.return_type == IrType::Void || scalar(&info.return_type))
}

/// These functions "return twice": once normally (returning 0) and again when longjmp is called.
/// Values live at the call point must have their intervals extended to prevent stack slot reuse.
pub(super) fn is_returns_twice_call(inst: &Instruction) -> bool {
    if let Instruction::Call { func, .. } = inst {
        matches!(func.as_str(), "setjmp" | "_setjmp" | "sigsetjmp" | "__sigsetjmp")
    } else {
        false
    }
}

/// Iterate over all Operand references in an instruction.
/// This is the single canonical source of truth for instruction operand traversal.
/// All code that needs to enumerate operands (liveness, use-counting, GEP fold
/// verification) should call this rather than hand-rolling its own match.
pub(super) fn for_each_operand_in_instruction(inst: &Instruction, mut f: impl FnMut(&Operand)) {
    match inst {
        Instruction::Alloca { .. } => {}
        Instruction::DynAlloca { size, .. } => f(size),
        Instruction::Store { val, .. } => f(val),
        Instruction::Load { .. } => {}
        Instruction::BinOp { lhs, rhs, .. } => { f(lhs); f(rhs); }
        Instruction::UnaryOp { src, .. } => f(src),
        Instruction::Cmp { lhs, rhs, .. } => { f(lhs); f(rhs); }
        Instruction::Call { info, .. } => { for a in &info.args { f(a); } }
        Instruction::CallIndirect { func_ptr, info } => { f(func_ptr); for a in &info.args { f(a); } }
        Instruction::GetElementPtr { offset, .. } => f(offset),
        Instruction::Cast { src, .. } => f(src),
        Instruction::Copy { src, .. } => f(src),
        Instruction::GlobalAddr { .. } => {}
        Instruction::Memcpy { .. } => {}
        Instruction::VaArg { .. } => {}
        Instruction::VaStart { .. } => {}
        Instruction::VaEnd { .. } => {}
        Instruction::VaCopy { .. } => {}
        Instruction::VaArgStruct { .. } => {}
        Instruction::AtomicRmw { ptr, val, .. } => { f(ptr); f(val); }
        Instruction::AtomicCmpxchg { ptr, expected, desired, .. } => { f(ptr); f(expected); f(desired); }
        Instruction::AtomicLoad { ptr, .. } => f(ptr),
        Instruction::AtomicStore { ptr, val, .. } => { f(ptr); f(val); }
        Instruction::Fence { .. } => {}
        Instruction::Phi { incoming, .. } => { for (op, _) in incoming { f(op); } }
        Instruction::LabelAddr { .. } => {}
        Instruction::GetReturnF64Second { .. } => {}
        Instruction::SetReturnF64Second { src } => f(src),
        Instruction::GetReturnF32Second { .. } => {}
        Instruction::SetReturnF32Second { src } => f(src),
        Instruction::GetReturnF128Second { .. } => {},
        Instruction::SetReturnF128Second { src } => f(src),
        Instruction::InlineAsm { inputs, .. } => {
            for (_, op, _) in inputs { f(op); }
        }
        Instruction::Intrinsic { args, .. } => { for a in args { f(a); } }
        Instruction::Select { cond, true_val, false_val, .. } => { f(cond); f(true_val); f(false_val); }
        Instruction::StackSave { .. } => {}
        Instruction::StackRestore { .. } => {}
        Instruction::ParamRef { .. } => {}
    }
}

/// Iterate over Value references (non-Operand) used in an instruction.
/// These are pointer/base values used directly (not wrapped in Operand),
/// e.g., the `ptr` in Store/Load, `base` in GEP, `dest`/`src` in Memcpy.
/// Canonical traversal — shared by liveness, use-counting, and GEP fold analysis.
pub(super) fn for_each_value_use_in_instruction(inst: &Instruction, mut f: impl FnMut(&Value)) {
    match inst {
        Instruction::Store { ptr, .. } => f(ptr),
        Instruction::Load { ptr, .. } => f(ptr),
        Instruction::GetElementPtr { base, .. } => f(base),
        Instruction::Memcpy { dest, src, .. } => { f(dest); f(src); }
        Instruction::VaArg { va_list_ptr, .. } => f(va_list_ptr),
        Instruction::VaStart { va_list_ptr } => f(va_list_ptr),
        Instruction::VaEnd { va_list_ptr } => f(va_list_ptr),
        Instruction::VaCopy { dest_ptr, src_ptr } => { f(dest_ptr); f(src_ptr); }
        Instruction::VaArgStruct { dest_ptr, va_list_ptr, .. } => { f(dest_ptr); f(va_list_ptr); }
        Instruction::InlineAsm { outputs, .. } => {
            for (_, v, _) in outputs { f(v); }
        }
        Instruction::Intrinsic { dest_ptr: Some(dp), .. } => {
            f(dp);
        }
        Instruction::StackRestore { ptr } => f(ptr),
        _ => {}
    }
}

/// Iterate over all Operand references in a terminator.
/// Canonical traversal — shared by liveness, use-counting, and GEP fold analysis.
pub(super) fn for_each_operand_in_terminator(term: &Terminator, mut f: impl FnMut(&Operand)) {
    match term {
        Terminator::Return(Some(op)) => f(op),
        Terminator::CondBranch { cond, .. } => f(cond),
        Terminator::IndirectBranch { target, .. } => f(target),
        Terminator::Switch { val, .. } => f(val),
        _ => {}
    }
}

/// Loop nesting depth per block: the number of loops (find_loops) it is in.
#[cfg(test)]
fn compute_loop_depth(successors: &[Vec<usize>], num_blocks: usize) -> Vec<u32> {
    let mut depth = vec![0u32; num_blocks];
    for (_, members) in find_loops(successors, num_blocks) {
        for b in members {
            depth[b] += 1;
        }
    }
    depth
}

/// Each loop multiplies the frequency of its body by this (a trip count).
const LOOP_TRIPS: f64 = 10.0;

/// Estimated execution frequency of each block, the entry's being 1.
///
/// Within a loop iteration (and the function body outside loops), mass flows
/// from the header along forward edges, split evenly among a block's
/// successor edges (so each case of a switch takes its share), and summed
/// where paths merge. A loop, seen from outside, is one node: its body runs
/// LOOP_TRIPS times per entry, and the mass that entered it leaves by its
/// exits in the proportions the body sends there. Loops are processed
/// innermost first, as nested regions.
///
/// The register allocator weights uses by this. By loop depth alone, a value
/// used in every iteration of an interpreter's dispatch loop (sqlite3VdbeExec's
/// step counter, read at the header and the shared latch) weighed less than
/// ones with many uses spread over its 186 opcode handlers, each of which
/// runs only for its own opcodes.
pub(crate) fn compute_block_freq(successors: &[Vec<usize>], num_blocks: usize) -> Vec<f64> {
    let mut freq = vec![0.0f64; num_blocks];
    if num_blocks == 0 {
        return freq;
    }
    let mut loops = find_loops(successors, num_blocks);
    loops.sort_by_key(|(_, members)| members.len());
    // The loops each block is in, innermost first.
    let mut chain: Vec<Vec<usize>> = vec![Vec::new(); num_blocks];
    for (l, (_, members)) in loops.iter().enumerate() {
        for &b in members {
            chain[b].push(l);
        }
    }
    // A region's nodes: its own blocks, and each loop directly inside it as
    // one node (numbered num_blocks + loop index).
    let node_in = |b: usize, region: Option<usize>| -> Option<usize> {
        let c = &chain[b];
        match region {
            None => Some(c.last().map_or(b, |&l| num_blocks + l)),
            Some(r) => {
                let pos = c.iter().position(|&l| l == r)?;
                Some(if pos == 0 { b } else { num_blocks + c[pos - 1] })
            }
        }
    };
    // Per loop: where its entering mass leaves (normalized), and the mass
    // each node of its body receives per iteration.
    let mut exits: Vec<Vec<(usize, f64)>> = vec![Vec::new(); loops.len()];
    let mut local: Vec<Vec<(usize, f64)>> = vec![Vec::new(); loops.len() + 1];
    for region in (0..loops.len()).map(Some).chain(std::iter::once(None)) {
        let (entry_block, members): (usize, Vec<usize>) = match region {
            Some(r) => (loops[r].0, loops[r].1.clone()),
            None => (0, (0..num_blocks).collect()),
        };
        let Some(entry) = node_in(entry_block, region) else { continue };
        // Out-edges of each node: Some(node) inside the region, None for
        // the back edge to the header, or an exit to a block outside.
        let mut nodes: Vec<usize> = members.iter().filter_map(|&b| node_in(b, region)).collect();
        nodes.sort_unstable();
        nodes.dedup();
        let index = |n: usize| nodes.binary_search(&n).ok();
        let mut out: Vec<Vec<(Result<usize, usize>, f64)>> = vec![Vec::new(); nodes.len()];
        let mut indeg = vec![0usize; nodes.len()];
        for (i, &n) in nodes.iter().enumerate() {
            let targets: Vec<(usize, f64)> = if n < num_blocks {
                let s = &successors[n];
                s.iter().map(|&t| (t, 1.0 / s.len() as f64)).collect()
            } else {
                exits[n - num_blocks].clone()
            };
            for (t, p) in targets {
                let edge = if region.is_some() && t == entry_block {
                    continue; // back edge: the next iteration
                } else if let Some(j) = node_in(t, region).and_then(index) {
                    if j == i {
                        continue;
                    }
                    indeg[j] += 1;
                    Ok(j)
                } else {
                    Err(t)
                };
                out[i].push((edge, p));
            }
        }
        // Forward propagation in topological order. A cycle the loops do not
        // cover (irreducible flow) is broken at its node with the most mass.
        let mut mass = vec![0.0f64; nodes.len()];
        let mut done = vec![false; nodes.len()];
        let mut ready: Vec<usize> = Vec::new();
        if let Some(e) = index(entry) {
            mass[e] = 1.0;
            ready.push(e);
        }
        let mut leaving: Vec<(usize, f64)> = Vec::new();
        loop {
            let i = match ready.pop() {
                Some(i) => i,
                None => match (0..nodes.len()).filter(|&i| !done[i] && mass[i] > 0.0)
                    .max_by(|&a, &b| mass[a].total_cmp(&mass[b])) {
                    Some(i) => i,
                    None => break,
                },
            };
            if done[i] {
                continue;
            }
            done[i] = true;
            for &(edge, p) in &out[i] {
                let m = mass[i] * p;
                match edge {
                    Ok(j) => {
                        mass[j] += m;
                        indeg[j] -= 1;
                        if indeg[j] == 0 && !done[j] {
                            ready.push(j);
                        }
                    }
                    Err(t) => leaving.push((t, m)),
                }
            }
        }
        if let Some(r) = region {
            let total: f64 = leaving.iter().map(|&(_, m)| m).sum();
            leaving.sort_unstable_by_key(|&(t, _)| t);
            let mut dist: Vec<(usize, f64)> = Vec::new();
            for (t, m) in leaving {
                match dist.last_mut() {
                    Some(last) if last.0 == t => last.1 += m / total,
                    _ => dist.push((t, m / total)),
                }
            }
            exits[r] = dist;
        }
        local[region.unwrap_or(loops.len())] = nodes.iter().copied().zip(mass).collect();
    }
    // Outermost region first: a loop node's mass, times the trips, scales
    // its body.
    let mut work: Vec<(usize, f64)> = vec![(loops.len(), 1.0)];
    while let Some((r, scale)) = work.pop() {
        for &(n, m) in &local[r] {
            if n < num_blocks {
                freq[n] = scale * m;
            } else {
                work.push((n - num_blocks, scale * m * LOOP_TRIPS));
            }
        }
    }
    freq
}

/// The natural loops of the CFG, one per header: (header, body including
/// the header).
///
/// Uses DFS-based back-edge detection: an edge src -> dst where dst is an
/// ancestor in the DFS tree is a back edge defining a natural loop. For each
/// back edge (src -> header), all blocks on any path from header to src form
/// the loop body.
fn find_loops(successors: &[Vec<usize>], num_blocks: usize) -> Vec<(usize, Vec<usize>)> {
    let mut loops = Vec::new();
    if num_blocks == 0 {
        return loops;
    }

    // Build predecessor lists from successor lists.
    let mut predecessors: Vec<Vec<usize>> = vec![Vec::new(); num_blocks];
    for (src, succs) in successors.iter().enumerate() {
        for &dst in succs {
            if dst < num_blocks {
                predecessors[dst].push(src);
            }
        }
    }

    // DFS to classify edges. An edge src -> dst is a back edge if dst is an
    // ancestor of src in the DFS tree (i.e., dst was visited but not finished).
    // State: 0 = unvisited, 1 = in-progress (on stack), 2 = finished.
    let mut state = vec![0u8; num_blocks];
    let mut back_edges: Vec<(usize, usize)> = Vec::new(); // (src, header)

    // Iterative DFS to avoid stack overflow on deeply nested CFGs.
    let mut stack: Vec<(usize, usize)> = Vec::new(); // (block, successor_index)
    state[0] = 1; // Mark entry block as in-progress
    stack.push((0, 0));

    while let Some(&mut (block, ref mut succ_idx)) = stack.last_mut() {
        if *succ_idx < successors[block].len() {
            let next = successors[block][*succ_idx];
            *succ_idx += 1;
            if next < num_blocks {
                match state[next] {
                    0 => {
                        // Unvisited: push to stack
                        state[next] = 1;
                        stack.push((next, 0));
                    }
                    1 => {
                        // Back edge: next is an ancestor (in-progress)
                        back_edges.push((block, next));
                    }
                    _ => {
                        // Cross or forward edge: ignore
                    }
                }
            }
        } else {
            // All successors processed: mark as finished
            state[block] = 2;
            stack.pop();
        }
    }

    // For each back edge (src -> header), find the natural loop body.
    // The loop body consists of all blocks that can reach `src` without going
    // through `header`, plus `header` itself. We compute this by a reverse
    // BFS/DFS from `src` following predecessor edges, stopping at `header`.
    // One loop per header: the union of its back edges' bodies, each block
    // in it counted once. (Counted per back edge, sqlite3VdbeExec's
    // dispatch loop, which each opcode's handler jumps back to, nested its
    // blocks hundreds deep, some more than others.)
    //
    // A retreating edge whose target does not dominate its source (a cycle
    // entered in more than one place, irreducible) has no natural loop: the
    // walk back from its source reaches the entry. Its body is then the
    // blocks on the cycle (below). (The whole walk took in the entry, which
    // then got no mass: every block of SQLite's balance_nonroot outside its later loops
    // weighed nothing to the register allocator, and its hottest loop, over
    // the cells of the sibling pages, reloaded b.apCell from the stack every
    // cell.)
    back_edges.sort_unstable_by_key(|&(_, header)| header);
    let mut in_body = vec![false; num_blocks];
    let mut members: Vec<usize> = Vec::new();
    let mut walked: Vec<usize> = Vec::new();
    let mut worklist: Vec<usize> = Vec::new();
    let mut irreducible: Vec<(usize, usize)> = Vec::new();
    for group in back_edges.chunk_by(|a, b| a.1 == b.1) {
        let header = group[0].1;
        in_body[header] = true;
        members.push(header);
        for &(tail, _) in group {
            if in_body[tail] {
                continue;
            }
            // Backwards from the tail, stopping at the header and at blocks
            // already in the body (whose own walks stayed inside).
            in_body[tail] = true;
            walked.push(tail);
            worklist.push(tail);
            let mut reached_entry = tail == 0;
            while let Some(b) = worklist.pop() {
                for &pred in &predecessors[b] {
                    if pred < num_blocks && !in_body[pred] {
                        reached_entry |= pred == 0;
                        in_body[pred] = true;
                        walked.push(pred);
                        worklist.push(pred);
                    }
                }
            }
            if reached_entry {
                for &b in &walked {
                    in_body[b] = false;
                }
                walked.clear();
                irreducible.push((tail, header));
            } else {
                members.append(&mut walked);
            }
        }
        for &b in &members {
            in_body[b] = false;
        }
        if members.len() > 1 || group.iter().any(|&(tail, _)| tail == header) {
            loops.push((header, std::mem::take(&mut members)));
        } else {
            members.clear();
        }
    }
    if irreducible.is_empty() {
        return loops;
    }

    // The irreducible cycles' blocks: reachable from the header (without
    // passing through it) and leading back to the source. Each is taken
    // (joined to its header's loop, if any) only where it nests with every
    // other loop, as compute_block_freq needs; a cycle through
    // sqlite3VdbeExec's dispatch loop, entered by a goto into an opcode's
    // handler, cut across it and scaled the wrong blocks.
    let mut from_header = vec![false; num_blocks];
    let mut seen: Vec<usize> = Vec::new();
    for group in irreducible.chunk_by(|a, b| a.1 == b.1) {
        let header = group[0].1;
        let at = loops.iter().position(|(h, _)| *h == header);
        let mut body: Vec<usize> = at.map_or_else(|| vec![header], |i| loops[i].1.clone());
        for &b in &body {
            in_body[b] = true;
        }
        // Not through the headers of loops around it: from inside a loop,
        // its own back edges reach every block of it.
        let around: Vec<usize> = loops.iter()
            .filter(|(h, body)| *h != header && body.contains(&header))
            .map(|(h, _)| *h)
            .collect();
        worklist.push(header);
        while let Some(b) = worklist.pop() {
            for &s in &successors[b] {
                if s < num_blocks && s != header && !from_header[s] && !around.contains(&s) {
                    from_header[s] = true;
                    seen.push(s);
                    worklist.push(s);
                }
            }
        }
        for &(tail, _) in group {
            if in_body[tail] {
                continue;
            }
            in_body[tail] = true;
            body.push(tail);
            worklist.push(tail);
            while let Some(b) = worklist.pop() {
                for &pred in &predecessors[b] {
                    if pred < num_blocks && !in_body[pred] && from_header[pred] {
                        in_body[pred] = true;
                        body.push(pred);
                        worklist.push(pred);
                    }
                }
            }
        }
        for &s in &seen {
            from_header[s] = false;
        }
        seen.clear();
        let nests = loops.iter().enumerate().filter(|&(i, _)| Some(i) != at).all(|(_, (_, other))| {
            // (One around the header is not inside the cycle.)
            let shared = other.iter().filter(|&&b| in_body[b]).count();
            shared == 0 || (shared == other.len() && !other.contains(&header)) || shared == body.len()
        });
        for &b in &body {
            in_body[b] = false;
        }
        if nests {
            match at {
                Some(i) => loops[i].1 = body,
                None => loops.push((header, body)),
            }
        }
    }

    loops
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::common::types::IrType;
    use crate::ir::reexports::{BasicBlock, BlockId, IrBinOp};

    /// Verify that InlineAsm with register operands is treated as a call point.
    /// This is critical for register allocation: values spanning inline asm with
    /// register constraints must get callee-saved registers, since inline asm may
    /// clobber caller-saved registers (r8-r11 on x86).
    #[test]
    fn test_inline_asm_with_operands_is_call_point() {
        let mut func = IrFunction::new("test".to_string(), IrType::I32, vec![], false);
        func.blocks.push(BasicBlock {
            label: BlockId(0),
            instructions: vec![
                Instruction::BinOp {
                    dest: Value(0), op: IrBinOp::Add,
                    lhs: Operand::Const(IrConst::I32(1)),
                    rhs: Operand::Const(IrConst::I32(2)),
                    ty: IrType::I32,
                },
                // Inline asm with an output register constraint
                Instruction::InlineAsm {
                    template: "nop".to_string(),
                    outputs: vec![("=r".to_string(), Value(1), Some("out".to_string()))],
                    inputs: vec![],
                    clobbers: vec![],
                    operand_types: vec![IrType::I32],
                    goto_labels: vec![],
                    input_symbols: vec![],
                    seg_overrides: vec![],
                },
            ],
            terminator: Terminator::Return(Some(Operand::Value(Value(0)))),
            source_spans: Vec::new(),
        });
        func.next_value_id = 2;

        let result = compute_live_intervals(&func);
        // The InlineAsm instruction should appear as a call point
        assert!(!result.call_points.is_empty(),
            "InlineAsm with register operands should be a call point");
    }

    /// Verify that empty inline asm barriers (no inputs/outputs) are NOT call points.
    /// Memory barriers like `asm volatile("" ::: "memory")` don't use GP registers
    /// and should not force values into callee-saved registers.
    #[test]
    fn test_empty_inline_asm_barrier_not_call_point() {
        let mut func = IrFunction::new("test".to_string(), IrType::I32, vec![], false);
        func.blocks.push(BasicBlock {
            label: BlockId(0),
            instructions: vec![
                Instruction::BinOp {
                    dest: Value(0), op: IrBinOp::Add,
                    lhs: Operand::Const(IrConst::I32(1)),
                    rhs: Operand::Const(IrConst::I32(2)),
                    ty: IrType::I32,
                },
                // Empty inline asm barrier - no outputs or inputs
                Instruction::InlineAsm {
                    template: String::new(),
                    outputs: vec![],
                    inputs: vec![],
                    clobbers: vec!["memory".to_string()],
                    operand_types: vec![],
                    goto_labels: vec![],
                    input_symbols: vec![],
                    seg_overrides: vec![],
                },
            ],
            terminator: Terminator::Return(Some(Operand::Value(Value(0)))),
            source_spans: Vec::new(),
        });
        func.next_value_id = 1;

        let result = compute_live_intervals(&func);
        // Call points should only contain the calls, not the empty barrier
        assert!(result.call_points.is_empty(),
            "Empty inline asm barriers should NOT be call points");
    }

    fn call(dest: u32) -> Instruction {
        Instruction::Call {
            func: "f".to_string(),
            info: crate::ir::reexports::CallInfo {
                dest: Some(Value(dest)),
                args: vec![],
                arg_types: vec![],
                return_type: IrType::I64,
                is_variadic: false,
                num_fixed_args: 0,
                struct_arg_sizes: vec![],
                struct_arg_aligns: vec![],
                struct_arg_classes: Vec::new(),
                struct_arg_riscv_float_classes: Vec::new(),
                is_sret: false,
                is_fastcall: false,
                ret_eightbyte_classes: Vec::new(),
            },
        }
    }

    fn copy(dest: u32, val: i64) -> Instruction {
        Instruction::Copy { dest: Value(dest), src: Operand::Const(IrConst::I64(val)) }
    }

    /// A value whose only use is in a block laid out after a call it never
    /// reaches has an interval that covers the call, but its live segments
    /// skip the block with the call.
    #[test]
    fn test_segments_skip_block_not_reached() {
        let mut func = IrFunction::new("test".to_string(), IrType::I64, vec![], false);
        // .L0: v0 = 5; v1 = 1; condbr v1, .L2, .L1
        // .L1: v2 = call f(); ret v2
        // .L2: ret v0
        func.blocks.push(BasicBlock {
            label: BlockId(0),
            instructions: vec![copy(0, 5), copy(1, 1)],
            terminator: Terminator::CondBranch {
                cond: Operand::Value(Value(1)), true_label: BlockId(2), false_label: BlockId(1),
            },
            source_spans: Vec::new(),
        });
        func.blocks.push(BasicBlock {
            label: BlockId(1),
            instructions: vec![call(2)],
            terminator: Terminator::Return(Some(Operand::Value(Value(2)))),
            source_spans: Vec::new(),
        });
        func.blocks.push(BasicBlock {
            label: BlockId(2),
            instructions: vec![],
            terminator: Terminator::Return(Some(Operand::Value(Value(0)))),
            source_spans: Vec::new(),
        });
        func.next_value_id = 3;

        let result = compute_live_intervals(&func);
        let call_point = result.call_points[0];
        let iv = result.intervals.iter().find(|iv| iv.value_id == 0).unwrap();
        assert!(iv.start < call_point && call_point < iv.end);
        let segs = result.segments_of(iv);
        assert_eq!(segs.len(), 2, "{:?}", segs);
        assert!(segs.iter().all(|&(lo, hi)| call_point < lo || call_point > hi), "{:?}", segs);
        // v1 lives inside .L0 only: a single piece, no entry in the map.
        assert!(!result.segments.contains_key(&1));
    }

    /// A value defined in two predecessors (a lowered phi) is live from each
    /// definition to the join, and not in a sibling block that holds a call.
    #[test]
    fn test_segments_lowered_phi() {
        let mut func = IrFunction::new("test".to_string(), IrType::I64, vec![], false);
        // .L0: v1 = 1; condbr v1, .L1, .L3
        // .L1: v0 = 2; br .L2
        // .L3: v2 = call f(); br .L4
        // .L4: v0 = 3; br .L2
        // .L2: ret v0
        // v0 is defined in .L1 and .L4 and used in .L2; it is dead in .L3.
        func.blocks.push(BasicBlock {
            label: BlockId(0),
            instructions: vec![copy(1, 1)],
            terminator: Terminator::CondBranch {
                cond: Operand::Value(Value(1)), true_label: BlockId(1), false_label: BlockId(3),
            },
            source_spans: Vec::new(),
        });
        func.blocks.push(BasicBlock {
            label: BlockId(1),
            instructions: vec![copy(0, 2)],
            terminator: Terminator::Branch(BlockId(2)),
            source_spans: Vec::new(),
        });
        func.blocks.push(BasicBlock {
            label: BlockId(3),
            instructions: vec![call(2)],
            terminator: Terminator::Branch(BlockId(4)),
            source_spans: Vec::new(),
        });
        func.blocks.push(BasicBlock {
            label: BlockId(4),
            instructions: vec![copy(0, 3)],
            terminator: Terminator::Branch(BlockId(2)),
            source_spans: Vec::new(),
        });
        func.blocks.push(BasicBlock {
            label: BlockId(2),
            instructions: vec![],
            terminator: Terminator::Return(Some(Operand::Value(Value(0)))),
            source_spans: Vec::new(),
        });
        func.next_value_id = 3;

        let result = compute_live_intervals(&func);
        let call_point = result.call_points[0];
        let iv = result.intervals.iter().find(|iv| iv.value_id == 0).unwrap();
        let segs = result.segments_of(iv);
        // .L1 [2,3], then .L4 and .L2 merged [6,8].
        assert_eq!(segs, vec![(2, 3), (6, 8)]);
        assert!(segs.iter().all(|&(lo, hi)| call_point < lo || call_point > hi));
    }

    /// A lowered phi whose step is right before its redefinition keeps two
    /// pieces even though they touch (point 2, then from 3), so the step's
    /// result can take the phi's register.
    #[test]
    fn test_segments_redefinition_stays_apart() {
        let mut func = IrFunction::new("test".to_string(), IrType::I64, vec![], false);
        // .L0: v0 = 0; br .L1
        // .L1: v1 = v0 + 1; v0 = v1; v2 = v0 < 10; condbr v2, .L1, .L2
        // .L2: ret v0
        func.blocks.push(BasicBlock {
            label: BlockId(0),
            instructions: vec![copy(0, 0)],
            terminator: Terminator::Branch(BlockId(1)),
            source_spans: Vec::new(),
        });
        func.blocks.push(BasicBlock {
            label: BlockId(1),
            instructions: vec![
                Instruction::BinOp { dest: Value(1), op: IrBinOp::Add, lhs: Operand::Value(Value(0)),
                                     rhs: Operand::Const(IrConst::I64(1)), ty: IrType::I64 },
                Instruction::Copy { dest: Value(0), src: Operand::Value(Value(1)) },
                Instruction::Cmp { dest: Value(2), op: crate::ir::reexports::IrCmpOp::Slt, lhs: Operand::Value(Value(0)),
                                   rhs: Operand::Const(IrConst::I64(10)), ty: IrType::I64 },
            ],
            terminator: Terminator::CondBranch {
                cond: Operand::Value(Value(2)), true_label: BlockId(1), false_label: BlockId(2),
            },
            source_spans: Vec::new(),
        });
        func.blocks.push(BasicBlock {
            label: BlockId(2),
            instructions: vec![],
            terminator: Terminator::Return(Some(Operand::Value(Value(0)))),
            source_spans: Vec::new(),
        });
        func.next_value_id = 3;

        let result = compute_live_intervals(&func);
        let iv = result.intervals.iter().find(|iv| iv.value_id == 0).unwrap();
        assert_eq!(result.segments_of(iv), vec![(0, 2), (3, 6)]);
    }

    /// A loop whose header three latches jump back to (a dispatch loop) is
    /// one loop deep, not three; one nested in it is two.
    #[test]
    fn test_loop_depth_counts_each_header_once() {
        // 0 -> 1 (header); 1 -> 2, 3, 4, 6; 2, 3, 4 -> 1; 4 -> 5 -> 5 / 1 (inner); 6 exit.
        let succ = vec![vec![1], vec![2, 3, 4, 6], vec![1], vec![1], vec![5], vec![5, 1], vec![]];
        let depth = compute_loop_depth(&succ, 7);
        assert_eq!(depth, vec![0, 1, 1, 1, 1, 2, 0]);
    }

    /// A switch's cases share their block's frequency, the shared latch gets
    /// it back, and the code after the loop runs once, as the entry does.
    #[test]
    fn test_block_freq_splits_switch_cases() {
        // 0 -> 1 (header, switch to 2, 3, 4, 5); 2, 3, 4 -> 6 (latch) -> 1;
        // 5 leaves the loop -> 7; 7 -> 8, 9 (each half) -> 10 (merge).
        let succ = vec![vec![1], vec![2, 3, 4, 5], vec![6], vec![6], vec![6], vec![7], vec![1],
                        vec![8, 9], vec![10], vec![10], vec![]];
        let freq = compute_block_freq(&succ, 11);
        let want = [1.0, 10.0, 2.5, 2.5, 2.5, 1.0, 7.5, 1.0, 0.5, 0.5, 1.0];
        for (b, (&got, &w)) in freq.iter().zip(&want).enumerate() {
            assert!((got - w).abs() < 1e-9, "block {b}: {got} != {w}");
        }
    }

    /// A cycle entered in two places is a loop of its own blocks; the entry
    /// stays outside it, with all of the mass.
    #[test]
    fn test_block_freq_cycle_entered_twice() {
        // 0 -> 1, 2 (each half); 1 -> 2; 2 -> 1 / 3 (exit).
        let succ = vec![vec![1, 2], vec![2], vec![1, 3], vec![]];
        let freq = compute_block_freq(&succ, 4);
        let want = [1.0, 10.0, 10.0, 1.0];
        for (b, (&got, &w)) in freq.iter().zip(&want).enumerate() {
            assert!((got - w).abs() < 1e-9, "block {b}: {got} != {w}");
        }
    }

    /// A cycle entered in two places inside a dispatch loop is a loop
    /// nested in it, not one around it by way of the dispatch loop's latch.
    #[test]
    fn test_block_freq_cycle_entered_twice_inside_a_loop() {
        // 0 -> 1 (dispatch) -> 2, 3; 2 -> 7 and 3 -> 5 (entering the cycle
        // 7 -> 5 -> 7 twice); 7 -> 4 (latch) -> 1 / 6 (exit).
        let succ = vec![vec![1], vec![2, 3], vec![7], vec![5], vec![1, 6], vec![7], vec![], vec![5, 4]];
        let freq = compute_block_freq(&succ, 8);
        let want = [1.0, 10.0, 5.0, 5.0, 10.0, 50.0, 1.0, 100.0];
        for (b, (&got, &w)) in freq.iter().zip(&want).enumerate() {
            assert!((got - w).abs() < 1e-9, "block {b}: {got} != {w}");
        }
    }

    /// A loop inside a loop runs LOOP_TRIPS times per run of its parent's
    /// body, scaled by how often that body reaches it.
    #[test]
    fn test_block_freq_nested_loops() {
        // 0 -> 1 (outer header) -> 2, 4; 2 (inner header) -> 3 -> 2 / 4;
        // 4 (outer latch) -> 1 / 5 (exit).
        let succ = vec![vec![1], vec![2, 4], vec![3], vec![2, 4], vec![1, 5], vec![]];
        let freq = compute_block_freq(&succ, 6);
        // Outer body: 10 per entry; half of it enters the inner loop, which
        // multiplies by 10 again; the latch gets everything back.
        let want = [1.0, 10.0, 50.0, 50.0, 10.0, 1.0];
        for (b, (&got, &w)) in freq.iter().zip(&want).enumerate() {
            assert!((got - w).abs() < 1e-9, "block {b}: {got} != {w}");
        }
    }
}
