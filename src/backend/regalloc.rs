//! Linear scan register allocator.
//!
//! Assigns physical registers to IR values based on their live intervals.
//! Values with the longest live ranges and most uses get priority for register
//! assignment. Values that don't fit in available registers remain on the stack.
//!
//! Three-phase allocation:
//! 1. **Callee-saved registers** (x86: rbx, r12-r15; ARM: x20-x28; RISC-V: s1, s7-s11):
//!    Assigned to values whose live ranges span function calls. These registers
//!    are preserved across calls by the ABI, so no save/restore is needed at call
//!    sites (but prologue/epilogue must save them).
//!
//! 2. **Caller-saved registers** (x86: r11, r10, r8, r9; ARM: x13, x14):
//!    Assigned to values whose live ranges do NOT span any function call. These
//!    registers are destroyed by calls, so they can only hold values between calls.
//!    No prologue/epilogue save/restore is needed since we never assign them to
//!    values that cross call boundaries.
//!
//! 3. **Callee-saved spillover**: After phases 1 and 2, any remaining callee-saved
//!    registers are assigned to the highest-priority non-call-spanning values that
//!    didn't fit in the caller-saved pool. This is critical for call-free hot loops
//!    (e.g., hash functions, matrix multiply, sorting) where all values compete for
//!    only a few caller-saved registers. The one-time prologue/epilogue save/restore
//!    cost is amortized over many loop iterations.

use crate::common::fx_hash::{FxHashMap, FxHashSet};
use crate::common::types::IrType;
use crate::ir::reexports::{
    Instruction,
    IntrinsicOp,
    IrConst,
    IrFunction,
    Operand,
};
use super::liveness::{LiveInterval, LivenessResult, compute_live_intervals, for_each_operand_in_instruction, for_each_operand_in_terminator};

/// A physical register assignment.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct PhysReg(pub u8);

/// Result of register allocation for a function.
pub struct RegAllocResult {
    /// Map from value ID -> assigned physical register.
    pub assignments: FxHashMap<u32, PhysReg>,
    /// Set of physical registers actually used (for prologue/epilogue save/restore).
    pub used_regs: Vec<PhysReg>,
    /// The liveness analysis computed during register allocation, if any.
    /// Cached here so that calculate_stack_space_common can reuse it for
    /// Tier 2 liveness-based stack slot packing, avoiding a redundant
    /// O(blocks * values * iterations) dataflow computation.
    /// None when no registers were available (empty available_regs).
    pub liveness: Option<super::liveness::LivenessResult>,
}

/// Configuration for the register allocator.
pub struct RegAllocConfig {
    /// Available callee-saved registers for allocation (e.g., s1-s11 for RISC-V).
    pub available_regs: Vec<PhysReg>,
    /// Available caller-saved registers for allocation.
    /// These are assigned to values whose live ranges do NOT span any call.
    /// Since they don't cross calls, no prologue/epilogue save/restore is needed.
    /// Examples: x86 r11, r10, r8, r9.
    pub caller_saved_regs: Vec<PhysReg>,
    /// Whether to allow inline asm operands to be register-allocated.
    /// Only enable this when the backend's asm emitter checks reg_assignments
    /// before falling back to stack access. Currently only RISC-V does this.
    pub allow_inline_asm_regalloc: bool,
    /// Whether a value may use a caller-saved register when a register-args-
    /// only call (`liveness::is_reg_args_only_call`) is only where its live
    /// range starts (it is the call's result) or ends (it is only read as an
    /// argument). Requires a backend whose argument setup and result copy
    /// leave the caller-saved pool untouched (AArch64).
    pub caller_saved_at_call_boundaries: bool,
    /// Whether Memcpy pointer operands may be register-allocated. Requires a
    /// backend whose memcpy reads register-assigned pointers (AArch64).
    pub allow_memcpy_regalloc: bool,
    /// Whether the dest of a Copy, Cast, BinOp, UnaryOp, Cmp, GEP or Load
    /// may share a register with an operand whose live range ends at that
    /// instruction (`i = i + 1` in place, and a loop's latch copy becoming
    /// a no-op). Requires a backend that reads every operand of those
    /// instructions before writing the dest's register (AArch64).
    pub dest_reuses_operand_reg: bool,
}

/// Run the linear scan register allocator on a function.
///
/// Strategy: We assign callee-saved registers to values with the longest
/// live intervals. This is a simplified linear scan that doesn't split
/// intervals — values either get a register for their entire lifetime or
/// remain on the stack.
///
/// We avoid allocating registers to:
/// - Alloca values (they represent stack addresses)
/// - i128/float values (they need special register paths)
/// - Values used only once right after definition (no benefit from register)
pub fn allocate_registers(
    func: &IrFunction,
    config: &RegAllocConfig,
) -> RegAllocResult {
    if config.available_regs.is_empty() && config.caller_saved_regs.is_empty() {
        return RegAllocResult {
            assignments: FxHashMap::default(),
            used_regs: Vec::new(),
            liveness: None,
        };
    }

    // Note: Register allocation is now enabled for functions with atomics.
    // Atomic operations in all backends (x86, ARM, RISC-V) access their operands
    // exclusively through regalloc-aware helpers (operand_to_rax/x0/t0 and
    // store_rax_to/x0_to/t0_to), so register-allocated values work correctly.
    // The atomic pointer operands are individually excluded from register
    // allocation eligibility below since they need stable stack addresses
    // for the memory access instructions.

    // On 32-bit targets, I64/U64 values need two registers (eax:edx) and cannot
    // be allocated to a single callee-saved register. Exclude them from eligibility.
    let is_32bit = crate::common::types::target_is_32bit();

    // Liveness analysis now uses backward dataflow iteration to correctly
    // handle loops (values live across back-edges have their intervals extended).
    let liveness = compute_live_intervals(func);

    // Count uses per value for prioritization, weighted by loop depth.
    //
    // Uses inside loops are weighted more heavily because they execute more
    // frequently. A use inside a loop at depth D contributes 10^D to the
    // weighted use count (so a use in a singly-nested loop counts 10x, doubly-
    // nested counts 100x, etc.). This ensures inner-loop temporaries get
    // priority for register allocation over values in straight-line code,
    // which is critical for performance in compute-heavy loops like zlib's
    // deflate_slow, longest_match, and slide_hash.
    let use_count = weighted_use_counts(func, &liveness);

    // Collect values whose types don't fit in a single GPR.
    let non_gpr_values = collect_non_gpr_values(func, is_32bit);

    // Helper closure to check if a type is unsuitable for GPR allocation
    let is_non_gpr_type = |ty: &IrType| -> bool {
        ty.is_float() || ty.is_long_double()
            || matches!(ty, IrType::I128 | IrType::U128)
            || (is_32bit && matches!(ty, IrType::I64 | IrType::U64))
    };

    // Use a whitelist approach: only allocate registers for values produced
    // by simple, well-understood instructions that store results via the
    // standard accumulator path (e.g., store_rax_to on x86, store_t0_to on RISC-V).
    let mut eligible: FxHashSet<u32> = FxHashSet::default();

    for block in &func.blocks {
        for inst in &block.instructions {
            // Values eligible for register allocation: those stored via the
            // standard accumulator path (store_rax_to on x86, store_t0_to on RISC-V).
            // Exclude float and i128 types since they use different register paths.
            match inst {
                Instruction::BinOp { dest, ty, .. }
                | Instruction::UnaryOp { dest, ty, .. } => {
                    if !is_non_gpr_type(ty) {
                        eligible.insert(dest.0);
                    }
                }
                Instruction::Cmp { dest, .. } => {
                    eligible.insert(dest.0);
                }
                Instruction::Cast { dest, to_ty, from_ty, .. } => {
                    if !is_non_gpr_type(to_ty) && !is_non_gpr_type(from_ty) {
                        eligible.insert(dest.0);
                    }
                }
                Instruction::Load { dest, ty, .. } => {
                    if !is_non_gpr_type(ty) {
                        eligible.insert(dest.0);
                    }
                }
                Instruction::GetElementPtr { dest, .. } => {
                    eligible.insert(dest.0);
                }
                Instruction::Copy { dest, src: _ } => {
                    // Copy instructions are eligible unless the source produces a
                    // non-GPR value (float, i128, or i64 on 32-bit). We check both
                    // constant types and propagated non-GPR status from Value sources.
                    if !non_gpr_values.contains(&dest.0) {
                        eligible.insert(dest.0);
                    }
                }
                // Call results are eligible for callee-saved register allocation.
                // The result arrives in the accumulator (rax on x86, x0 on ARM, a0 on
                // RISC-V), and emit_call_store_result calls emit_store_result which
                // uses store_rax_to/store_t0_to — both of which are register-aware
                // and will emit a reg-to-reg move (e.g., movq %rax, %rbx) instead of
                // a stack spill.
                Instruction::Call { info, .. }
                | Instruction::CallIndirect { info, .. } => {
                    if let Some(dest) = info.dest {
                        if !is_non_gpr_type(&info.return_type) {
                            eligible.insert(dest.0);
                        }
                    }
                }
                Instruction::Select { dest, ty, .. } => {
                    if !is_non_gpr_type(ty) {
                        eligible.insert(dest.0);
                    }
                }
                Instruction::GlobalAddr { dest, .. } | Instruction::LabelAddr { dest, .. } => {
                    eligible.insert(dest.0);
                }
                // Atomic operations store their results via store_rax_to/store_t0_to.
                Instruction::AtomicLoad { dest, ty, .. }
                | Instruction::AtomicRmw { dest, ty, .. }
                | Instruction::AtomicCmpxchg { dest, ty, .. } => {
                    if !is_non_gpr_type(ty) {
                        eligible.insert(dest.0);
                    }
                }
                Instruction::ParamRef { dest, ty, .. } => {
                    if !is_non_gpr_type(ty) {
                        eligible.insert(dest.0);
                    }
                }
                _ => {}
            }
        }
    }

    // Exclude values used as pointers in instructions whose codegen paths use
    // resolve_slot_addr() directly (not register-aware).
    remove_ineligible_operands(func, &mut eligible, config);

    let call_points = &liveness.call_points;
    let boundary_ok: &[(u32, Option<u32>)] = if config.caller_saved_at_call_boundaries {
        &liveness.reg_args_call_points
    } else {
        &[]
    };

    // Candidates are assigned in priority order, not program order, so each
    // register keeps all its intervals and a candidate may fill any gap.
    // Intervals are inclusive, so a value used by the instruction that defines
    // another never shares its register, except at the instructions in
    // `reuse_points` (see RegAllocConfig::dest_reuses_operand_reg).
    let reuse_points = if config.dest_reuses_operand_reg { dest_reuse_points(func) } else { FxHashMap::default() };
    // Whether a piece of `a` ending at `p` and a piece of `b` starting there
    // may share a register: the instruction at `p` defines `b` and reads `a`.
    let handoff = |a: u32, b: u32, p: u32| {
        reuse_points.get(&p).is_some_and(|(dest, ops)| *dest == b && ops.contains(&a))
    };
    let callee = &config.available_regs;
    let caller = &config.caller_saved_regs;
    // Each register's busy pieces: (start, end, value).
    let mut callee_busy: Vec<Vec<(u32, u32, u32)>> = vec![Vec::new(); callee.len()];
    let mut caller_busy: Vec<Vec<(u32, u32, u32)>> = vec![Vec::new(); caller.len()];
    let fits = |busy: &[(u32, u32, u32)], segs: &[(u32, u32)], v: u32| {
        segs.iter().all(|&(lo, hi)| busy.iter().all(|&(s, e, w)| {
            hi < s || lo > e || (e == lo && handoff(w, v, lo)) || (hi == s && handoff(v, w, s))
        }))
    };
    // Prefer a callee-saved register that is already saved by the prologue.
    let pick_callee = |busy: &[Vec<(u32, u32, u32)>], segs: &[(u32, u32)], v: u32| {
        let free = || (0..callee.len()).filter(|&i| fits(&busy[i], segs, v));
        free().find(|&i| !busy[i].is_empty()).or_else(|| free().next())
    };
    let occupy = |busy: &mut Vec<(u32, u32, u32)>, segs: &[(u32, u32)], v: u32| {
        busy.extend(segs.iter().map(|&(lo, hi)| (lo, hi, v)));
    };
    // Values that may hand a register to each other at a reuse point. A
    // candidate first tries its assigned partners' registers, so that the
    // handoff actually happens (`add x20, x20, #1`, no copy).
    let mut partners: FxHashMap<u32, Vec<u32>> = FxHashMap::default();
    for (dest, ops) in reuse_points.values() {
        for &op in ops {
            partners.entry(*dest).or_default().push(op);
            partners.entry(op).or_default().push(*dest);
        }
    }
    let partner_reg = |v: u32, assignments: &FxHashMap<u32, PhysReg>, pool: &[PhysReg],
                       busy: &[Vec<(u32, u32, u32)>], segs: &[(u32, u32)]| {
        partners.get(&v)?.iter()
            .filter_map(|p| assignments.get(p))
            .filter_map(|r| pool.iter().position(|x| x == r))
            .find(|&i| fits(&busy[i], segs, v))
    };
    let mut assignments: FxHashMap<u32, PhysReg> = FxHashMap::default();

    // Phase 1: Callee-saved registers for call-spanning values.
    for interval in build_sorted_candidates(&liveness, &eligible, &assignments, call_points, boundary_ok, &use_count, Some(true)) {
        let segs = liveness.segments_of(interval);
        let v = interval.value_id;
        // A value never read from its register (a constant-offset address
        // folded into every access) may share a register already saved, but
        // is not worth saving another one for.
        let weightless = use_count.get(&v).copied().unwrap_or(0) == 0;
        let pick = partner_reg(v, &assignments, callee, &callee_busy, &segs)
            .or_else(|| pick_callee(&callee_busy, &segs, v))
            .filter(|&i| !weightless || !callee_busy[i].is_empty());
        if let Some(i) = pick {
            occupy(&mut callee_busy[i], &segs, v);
            assignments.insert(v, callee[i]);
        }
    }

    // Phase 2: Caller-saved registers for non-call-spanning values (no
    // prologue/epilogue cost).
    // Phase 3: Callee-saved spillover for non-call-spanning values that did
    // not fit in the caller-saved pool (e.g. high-priority values in call-free
    // loops).
    for interval in build_sorted_candidates(&liveness, &eligible, &assignments, call_points, boundary_ok, &use_count, Some(false)) {
        let segs = liveness.segments_of(interval);
        let v = interval.value_id;
        // A partner's register first (the handoff saves a move), but a
        // callee-saved one only when it is already saved; then any free
        // caller-saved register; a callee-saved register costs a save and
        // restore, so it comes last.
        let callee_partner = partner_reg(v, &assignments, callee, &callee_busy, &segs);
        if let Some(i) = partner_reg(v, &assignments, caller, &caller_busy, &segs) {
            occupy(&mut caller_busy[i], &segs, v);
            assignments.insert(v, caller[i]);
        } else if let Some(i) = callee_partner.filter(|&i| !callee_busy[i].is_empty()) {
            occupy(&mut callee_busy[i], &segs, v);
            assignments.insert(v, callee[i]);
        } else if let Some(i) = (0..caller.len()).find(|&i| fits(&caller_busy[i], &segs, v)) {
            occupy(&mut caller_busy[i], &segs, v);
            assignments.insert(v, caller[i]);
        } else if let Some(i) = callee_partner {
            occupy(&mut callee_busy[i], &segs, v);
            assignments.insert(v, callee[i]);
        } else if let Some(i) = pick_callee(&callee_busy, &segs, v) {
            occupy(&mut callee_busy[i], &segs, v);
            assignments.insert(v, callee[i]);
        }
    }

    // CCC_DUMP_REGALLOC=<function|all>: each candidate's interval, weight,
    // and register (or "-" for a stack slot).
    if let Ok(want) = std::env::var("CCC_DUMP_REGALLOC") {
        if want == "all" || want == func.name {
            eprintln!("=== regalloc {} (calls at {:?})", func.name, call_points);
            let mut ivs: Vec<&LiveInterval> = liveness.intervals.iter().filter(|iv| eligible.contains(&iv.value_id)).collect();
            ivs.sort_by_key(|iv| std::cmp::Reverse(use_count.get(&iv.value_id).copied().unwrap_or(0)));
            for iv in ivs {
                let reg = assignments.get(&iv.value_id).map_or("-".to_string(), |r| format!("r{}", r.0));
                eprintln!("  v{:<5} [{:>4}, {:>4}] weight {:>6} spans_call {:5} {}", iv.value_id, iv.start, iv.end,
                    use_count.get(&iv.value_id).copied().unwrap_or(0),
                    spans_clobbering_call(&liveness, iv, call_points, boundary_ok), reg);
            }
        }
    }

    let mut used_regs: Vec<PhysReg> = (0..callee.len())
        .filter(|&i| !callee_busy[i].is_empty())
        .map(|i| callee[i])
        .collect();
    used_regs.sort_by_key(|r| r.0);

    RegAllocResult {
        assignments,
        used_regs,
        liveness: Some(liveness),
    }
}

/// Program points (numbered as in liveness: one per instruction, then one
/// for the block's terminator) of the instructions whose dest may take the
/// register of an operand that dies there: point -> (dest, operands).
fn dest_reuse_points(func: &IrFunction) -> FxHashMap<u32, (u32, Vec<u32>)> {
    let mut points = FxHashMap::default();
    let mut point = 0u32;
    for block in &func.blocks {
        for inst in &block.instructions {
            let simple = matches!(inst,
                Instruction::Copy { .. } | Instruction::Cast { .. } | Instruction::BinOp { .. }
                | Instruction::UnaryOp { .. } | Instruction::Cmp { .. }
                | Instruction::GetElementPtr { .. } | Instruction::Load { .. });
            if let (true, Some(dest)) = (simple, inst.dest()) {
                // (All read values: a GEP's base and a load's pointer are not
                // Operands.)
                let ops = inst.used_values();
                points.insert(point, (dest.0, ops));
            }
            point += 1;
        }
        point += 1;
    }
    points
}

/// Count uses per value, weighted by loop depth: a use inside a loop at depth D
/// contributes 10^D (capped at 10K), so inner-loop values get priority.
///
/// A use of a constant-offset GEP also counts as a use of its base: codegen
/// folds such GEPs into the loads and stores that use them (`[base, #off]`),
/// so those accesses read the base register directly.
fn weighted_use_counts(func: &IrFunction, liveness: &LivenessResult) -> FxHashMap<u32, u64> {
    let mut use_count: FxHashMap<u32, u64> = FxHashMap::default();
    let mut gep_base: FxHashMap<u32, u32> = FxHashMap::default();
    for inst in func.blocks.iter().flat_map(|b| &b.instructions) {
        if let Instruction::GetElementPtr { dest, base, offset: Operand::Const(_), .. } = inst {
            gep_base.insert(dest.0, base.0);
        }
    }
    let add = |use_count: &mut FxHashMap<u32, u64>, v: u32, weight: u64| {
        // A constant-offset GEP's uses read its base ([base, #off]); the GEP
        // itself is not worth a register.
        if !gep_base.contains_key(&v) {
            *use_count.entry(v).or_insert(0) += weight;
        }
        // Follow folded GEP chains a few levels.
        let mut cur = v;
        for _ in 0..4 {
            match gep_base.get(&cur) {
                Some(&b) if b != cur => {
                    *use_count.entry(b).or_insert(0) += weight;
                    cur = b;
                }
                _ => break,
            }
        }
    };
    for (block_idx, block) in func.blocks.iter().enumerate() {
        let weight: u64 = match liveness.block_loop_depth.get(block_idx).copied().unwrap_or(0) {
            0 => 1,
            1 => 10,
            2 => 100,
            3 => 1000,
            _ => 10_000, // cap at 10K for very deep nesting
        };
        // Every read value counts, including the pointers and bases that are
        // not Operands (loads, stores, GEPs, memcpy).
        for inst in &block.instructions {
            inst.for_each_used_value(|v| add(&mut use_count, v, weight));
        }
        for_each_operand_in_terminator(&block.terminator, |op| {
            if let Operand::Value(v) = op {
                add(&mut use_count, v.0, weight);
            }
        });
    }
    use_count
}

/// Configuration for floating-point register allocation.
pub struct FpRegAllocConfig {
    /// FP registers preserved across calls (AArch64: d8-d15).
    pub callee_saved_regs: Vec<PhysReg>,
    /// FP registers clobbered by calls (AArch64: v16-v31). Assigned only to
    /// values whose live ranges cross no call or helper-calling instruction.
    pub caller_saved_regs: Vec<PhysReg>,
}

/// Result of floating-point register allocation.
pub struct FpRegAllocResult {
    /// Map from value ID -> (FP register, value is F32 rather than F64).
    pub assignments: FxHashMap<u32, (PhysReg, bool)>,
    /// Callee-saved FP registers actually used (for prologue/epilogue save/restore).
    pub used_callee_saved: Vec<PhysReg>,
}

/// Allocate FP registers to F32/F64 values, using the same linear scan as
/// `allocate_registers`.
///
/// The backend moves floats through the integer accumulator as bit patterns,
/// and several codegen paths (call arguments, intrinsics, inline asm) read a
/// value's stack slot directly. So a value is eligible only if it is defined
/// and used exclusively by instructions whose codegen goes through the
/// backend's register-aware load/store-result helpers.
pub fn allocate_fp_registers(
    func: &IrFunction,
    liveness: &LivenessResult,
    config: &FpRegAllocConfig,
) -> FpRegAllocResult {
    let mut result = FpRegAllocResult {
        assignments: FxHashMap::default(),
        used_callee_saved: Vec::new(),
    };
    let has_barrier = func.blocks.iter().flat_map(|b| &b.instructions).any(|inst| {
        matches!(inst, Instruction::InlineAsm { .. }) || super::liveness::is_returns_twice_call(inst)
    });
    if has_barrier || (config.callee_saved_regs.is_empty() && config.caller_saved_regs.is_empty()) {
        return result;
    }

    let is_fp = |ty: &IrType| matches!(ty, IrType::F32 | IrType::F64);
    let is_wide = |ty: &IrType| matches!(ty, IrType::I128 | IrType::U128 | IrType::F128);

    // Float width of each candidate value, and values that cannot be candidates.
    let mut width: FxHashMap<u32, bool> = FxHashMap::default();
    let mut bad: FxHashSet<u32> = FxHashSet::default();
    fn set_width(width: &mut FxHashMap<u32, bool>, bad: &mut FxHashSet<u32>, id: u32, is_f32: bool) {
        if *width.entry(id).or_insert(is_f32) != is_f32 {
            bad.insert(id);
        }
    }

    for block in &func.blocks {
        for inst in &block.instructions {
            let float_def = match inst {
                Instruction::BinOp { dest, ty, .. }
                | Instruction::UnaryOp { dest, ty, .. }
                | Instruction::Load { dest, ty, .. }
                | Instruction::Select { dest, ty, .. } if is_fp(ty) => Some((*dest, *ty)),
                Instruction::Cast { dest, from_ty, to_ty, .. }
                    if is_fp(to_ty) && !is_wide(from_ty) => Some((*dest, *to_ty)),
                Instruction::Intrinsic { dest: Some(dest), op: IntrinsicOp::Fma { f64, .. }, .. } =>
                    Some((*dest, if *f64 { IrType::F64 } else { IrType::F32 })),
                _ => None,
            };
            match (float_def, inst) {
                (Some((dest, ty)), _) => set_width(&mut width, &mut bad, dest.0, ty == IrType::F32),
                (None, Instruction::Copy { dest, src: Operand::Const(c) }) => match c {
                    IrConst::F32(_) => set_width(&mut width, &mut bad, dest.0, true),
                    IrConst::F64(_) => set_width(&mut width, &mut bad, dest.0, false),
                    _ => { bad.insert(dest.0); }
                },
                (None, Instruction::Copy { .. }) => {} // resolved below
                (None, _) => {
                    if let Some(dest) = inst.dest() {
                        bad.insert(dest.0);
                    }
                }
            }

            // Only these instructions read their operands through the
            // register-aware helpers on every codegen path.
            let safe_use = match inst {
                Instruction::BinOp { ty, .. }
                | Instruction::UnaryOp { ty, .. }
                | Instruction::Cmp { ty, .. } => is_fp(ty),
                Instruction::Cast { from_ty, to_ty, .. } => is_fp(from_ty) && !is_wide(to_ty),
                Instruction::Store { ty, .. } | Instruction::Select { ty, .. } => !is_wide(ty),
                Instruction::Copy { .. } => true,
                // (Only AArch64 emits it, through float_operand_to_fp_reg.)
                Instruction::Intrinsic { op: IntrinsicOp::Fma { .. }, .. } => true,
                _ => false,
            };
            if !safe_use {
                for_each_operand_in_instruction(inst, |op| {
                    if let Operand::Value(v) = op {
                        bad.insert(v.0);
                    }
                });
                super::liveness::for_each_value_use_in_instruction(inst, |v| {
                    bad.insert(v.0);
                });
            }
        }
        let returns_fp = matches!(block.terminator, crate::ir::reexports::Terminator::Return(_))
            && is_fp(&func.return_type);
        if !returns_fp {
            for_each_operand_in_terminator(&block.terminator, |op| {
                if let Operand::Value(v) = op {
                    bad.insert(v.0);
                }
            });
        }
    }

    // Copies take the width of their source (phi elimination produces chains
    // and values with several defining Copies). A Copy from a value of unknown
    // width (e.g. a float parameter) makes its dest ineligible.
    for taint_unknown in [false, true] {
        loop {
            let mut changed = false;
            for inst in func.blocks.iter().flat_map(|b| &b.instructions) {
                if let Instruction::Copy { dest, src: Operand::Value(v) } = inst {
                    if bad.contains(&v.0) {
                        changed |= bad.insert(dest.0);
                    } else if let Some(&is_f32) = width.get(&v.0) {
                        match width.get(&dest.0) {
                            None => { width.insert(dest.0, is_f32); changed = true; }
                            Some(&w) if w != is_f32 => { changed |= bad.insert(dest.0); }
                            _ => {}
                        }
                    } else if taint_unknown {
                        changed |= bad.insert(dest.0);
                    }
                }
            }
            if !changed {
                break;
            }
        }
    }
    let eligible: FxHashSet<u32> = width.keys().copied().filter(|id| !bad.contains(id)).collect();
    if eligible.is_empty() {
        return result;
    }

    // Caller-saved FP registers are also clobbered by instructions that the
    // backend lowers to helper calls (F128 soft-float, intrinsics).
    let mut clobber_points = liveness.call_points.clone();
    let mut point: u32 = 0;
    for block in &func.blocks {
        for inst in &block.instructions {
            let calls_helper = match inst {
                // Whole-vector ops use only v0-v3 and the vector temp registers;
                // a fused multiply-add only d0-d2 and its operands' registers.
                Instruction::Intrinsic { op, .. } => !matches!(op, IntrinsicOp::VecOp { .. } | IntrinsicOp::Fma { .. }),
                Instruction::BinOp { ty, .. } | Instruction::UnaryOp { ty, .. }
                | Instruction::Cmp { ty, .. } | Instruction::Load { ty, .. }
                | Instruction::Store { ty, .. } => *ty == IrType::F128,
                Instruction::Cast { from_ty, to_ty, .. } => *from_ty == IrType::F128 || *to_ty == IrType::F128,
                _ => false,
            };
            if calls_helper {
                clobber_points.push(point);
            }
            point += 1;
        }
        point += 1; // terminator
    }
    clobber_points.sort_unstable();
    clobber_points.dedup();

    // Candidates are assigned in priority order, not program order, so each
    // register keeps all its intervals and a candidate may fill any gap.
    // Intervals are inclusive, so a value used by the instruction that defines
    // another never shares its register.
    let use_count = weighted_use_counts(func, liveness);
    let callee = &config.callee_saved_regs;
    let caller = &config.caller_saved_regs;
    let mut callee_busy: Vec<Vec<(u32, u32)>> = vec![Vec::new(); callee.len()];
    let mut caller_busy: Vec<Vec<(u32, u32)>> = vec![Vec::new(); caller.len()];
    let fits = |busy: &[(u32, u32)], segs: &[(u32, u32)]| {
        segs.iter().all(|&(lo, hi)| busy.iter().all(|&(s, e)| hi < s || lo > e))
    };
    // Prefer a callee-saved register that is already saved by the prologue.
    let pick_callee = |busy: &[Vec<(u32, u32)>], segs: &[(u32, u32)]| {
        let free = || (0..callee.len()).filter(|&i| fits(&busy[i], segs));
        free().find(|&i| !busy[i].is_empty()).or_else(|| free().next())
    };

    // Phase 1: callee-saved registers for values that live across clobbers.
    // Phase 2: caller-saved registers for values that cross no clobber.
    // Phase 3: leftover callee-saved registers for the remaining values.
    let no_gpr = FxHashMap::default();
    for iv in build_sorted_candidates(liveness, &eligible, &no_gpr, &clobber_points, &[], &use_count, Some(true)) {
        let segs = liveness.segments_of(iv);
        if let Some(i) = pick_callee(&callee_busy, &segs) {
            callee_busy[i].extend_from_slice(&segs);
            result.assignments.insert(iv.value_id, (callee[i], width[&iv.value_id]));
        }
    }
    for iv in build_sorted_candidates(liveness, &eligible, &no_gpr, &clobber_points, &[], &use_count, Some(false)) {
        let segs = liveness.segments_of(iv);
        if let Some(i) = (0..caller.len()).find(|&i| fits(&caller_busy[i], &segs)) {
            caller_busy[i].extend_from_slice(&segs);
            result.assignments.insert(iv.value_id, (caller[i], width[&iv.value_id]));
        } else if let Some(i) = pick_callee(&callee_busy, &segs) {
            callee_busy[i].extend_from_slice(&segs);
            result.assignments.insert(iv.value_id, (callee[i], width[&iv.value_id]));
        }
    }

    result.used_callee_saved = (0..callee.len()).filter(|&i| !callee_busy[i].is_empty()).map(|i| callee[i]).collect();
    result.used_callee_saved.sort_by_key(|r| r.0);
    result
}

/// Collect values whose types don't fit in a single GPR (floats, i128, and
/// on 32-bit targets: i64/u64). Copy instructions that chain from these
/// values must also be excluded via fixpoint propagation.
fn collect_non_gpr_values(func: &IrFunction, is_32bit: bool) -> FxHashSet<u32> {
    let is_non_gpr_type = |ty: &IrType| -> bool {
        ty.is_float() || ty.is_long_double()
            || matches!(ty, IrType::I128 | IrType::U128)
            || (is_32bit && matches!(ty, IrType::I64 | IrType::U64))
    };

    let mut non_gpr_values: FxHashSet<u32> = FxHashSet::default();

    // First pass: collect non-GPR values from typed instructions
    for block in &func.blocks {
        for inst in &block.instructions {
            match inst {
                Instruction::BinOp { dest, ty, .. }
                | Instruction::UnaryOp { dest, ty, .. } => {
                    if is_non_gpr_type(ty) {
                        non_gpr_values.insert(dest.0);
                    }
                }
                Instruction::Cast { dest, to_ty, from_ty, .. } => {
                    if is_non_gpr_type(to_ty) || is_non_gpr_type(from_ty) {
                        non_gpr_values.insert(dest.0);
                    }
                }
                Instruction::Load { dest, ty, .. } => {
                    if is_non_gpr_type(ty) {
                        non_gpr_values.insert(dest.0);
                    }
                }
                Instruction::Call { info, .. }
                | Instruction::CallIndirect { info, .. } => {
                    if let Some(dest) = info.dest {
                        if is_non_gpr_type(&info.return_type) {
                            non_gpr_values.insert(dest.0);
                        }
                    }
                }
                Instruction::Select { dest, ty, .. } => {
                    if is_non_gpr_type(ty) {
                        non_gpr_values.insert(dest.0);
                    }
                }
                Instruction::AtomicLoad { dest, ty, .. }
                | Instruction::AtomicRmw { dest, ty, .. }
                | Instruction::AtomicCmpxchg { dest, ty, .. } => {
                    if is_non_gpr_type(ty) {
                        non_gpr_values.insert(dest.0);
                    }
                }
                _ => {}
            }
        }
    }

    // Propagate non-GPR status through Copy chains: if a Copy's source is a
    // non-GPR value, the dest is also non-GPR. Iterate until fixpoint since
    // Copies can chain (Copy a->b, Copy b->c).
    loop {
        let mut changed = false;
        for block in &func.blocks {
            for inst in &block.instructions {
                if let Instruction::Copy { dest, src } = inst {
                    if non_gpr_values.contains(&dest.0) {
                        continue;
                    }
                    let src_is_non_gpr = match src {
                        Operand::Value(v) => non_gpr_values.contains(&v.0),
                        Operand::Const(IrConst::F32(_)) | Operand::Const(IrConst::F64(_))
                        | Operand::Const(IrConst::LongDouble(..))
                        | Operand::Const(IrConst::I128(_)) => true,
                        Operand::Const(IrConst::I64(_)) if is_32bit => true,
                        _ => false,
                    };
                    if src_is_non_gpr {
                        non_gpr_values.insert(dest.0);
                        changed = true;
                    }
                }
            }
        }
        if !changed {
            break;
        }
    }

    non_gpr_values
}

/// Remove values from the eligible set that are used as operands in instructions
/// whose codegen paths use resolve_slot_addr() directly (not register-aware).
/// This includes CallIndirect func pointers, Memcpy pointers, va_arg pointers,
/// atomic pointers, StackRestore, and InlineAsm operands.
fn remove_ineligible_operands(func: &IrFunction, eligible: &mut FxHashSet<u32>, config: &RegAllocConfig) {
    for block in &func.blocks {
        for inst in &block.instructions {
            match inst {
                Instruction::CallIndirect { func_ptr: Operand::Value(v), .. } => {
                    eligible.remove(&v.0);
                }
                Instruction::Memcpy { dest, src, .. } if !config.allow_memcpy_regalloc => {
                    eligible.remove(&dest.0);
                    eligible.remove(&src.0);
                }
                Instruction::VaArg { va_list_ptr, .. } => {
                    eligible.remove(&va_list_ptr.0);
                }
                Instruction::VaStart { va_list_ptr } => {
                    eligible.remove(&va_list_ptr.0);
                }
                Instruction::VaEnd { va_list_ptr } => {
                    eligible.remove(&va_list_ptr.0);
                }
                Instruction::VaCopy { dest_ptr, src_ptr } => {
                    eligible.remove(&dest_ptr.0);
                    eligible.remove(&src_ptr.0);
                }
                Instruction::VaArgStruct { dest_ptr, va_list_ptr, .. } => {
                    eligible.remove(&dest_ptr.0);
                    eligible.remove(&va_list_ptr.0);
                }
                Instruction::AtomicRmw { ptr: Operand::Value(v), .. } => {
                    eligible.remove(&v.0);
                }
                Instruction::AtomicCmpxchg { ptr: Operand::Value(v), .. } => {
                    eligible.remove(&v.0);
                }
                Instruction::AtomicLoad { ptr: Operand::Value(v), .. } => {
                    eligible.remove(&v.0);
                }
                Instruction::AtomicStore { ptr: Operand::Value(v), .. } => {
                    eligible.remove(&v.0);
                }
                Instruction::StackRestore { ptr } => {
                    eligible.remove(&ptr.0);
                }
                Instruction::InlineAsm { outputs, inputs, .. } => {
                    if !config.allow_inline_asm_regalloc {
                        // Inline asm operands are accessed via stack slots
                        // in codegen. Exclude them from register allocation
                        // unless the backend's asm emitter checks reg_assignments.
                        for (_, val, _) in outputs {
                            eligible.remove(&val.0);
                        }
                        for (_, op, _) in inputs {
                            if let Operand::Value(v) = op {
                                eligible.remove(&v.0);
                            }
                        }
                    }
                    // When allow_inline_asm_regalloc is true (RISC-V), the
                    // asm emitter checks reg_assignments before falling back
                    // to stack slot access.
                }
                _ => {}
            }
        }
    }
}

/// Check whether a value is live at any call point (sorted), looking only at
/// the segments in which it is live. A call in `boundary_ok` (sorted by point)
/// does not count when it defines the value, or when it is the value's last
/// use: the value then needs no register while the callee runs. (An interval
/// can also start at a call that it is live into, when the call opens a block;
/// that call does count.)
fn spans_clobbering_call(liveness: &LivenessResult, iv: &LiveInterval, call_points: &[u32], boundary_ok: &[(u32, Option<u32>)]) -> bool {
    let at_boundary = |cp: u32| match boundary_ok.binary_search_by_key(&cp, |&(p, _)| p) {
        Ok(i) => cp == iv.end || (cp == iv.start && boundary_ok[i].1 == Some(iv.value_id)),
        Err(_) => false,
    };
    liveness.segments_of(iv).iter().any(|&(lo, hi)| {
        let start_idx = call_points.partition_point(|&cp| cp < lo);
        call_points[start_idx..].iter()
            .take_while(|&&cp| cp <= hi)
            .any(|&cp| !at_boundary(cp))
    })
}

/// Build a sorted list of allocation candidates from live intervals.
///
/// Filters by eligibility, minimum span length, and call-spanning behavior:
/// - `spans_call == Some(true)`: only intervals that span a call
/// - `spans_call == Some(false)`: only intervals that do NOT span a call
/// - `spans_call == None`: all eligible intervals
///
/// Results are sorted by weighted use count (descending), with interval length
/// as tiebreaker.
fn build_sorted_candidates<'a>(
    liveness: &'a LivenessResult,
    eligible: &FxHashSet<u32>,
    already_assigned: &FxHashMap<u32, PhysReg>,
    call_points: &[u32],
    boundary_ok: &[(u32, Option<u32>)],
    use_count: &FxHashMap<u32, u64>,
    spans_call: Option<bool>,
) -> Vec<&'a LiveInterval> {
    let mut candidates: Vec<&LiveInterval> = liveness.intervals.iter()
        .filter(|iv| eligible.contains(&iv.value_id))
        .filter(|iv| !already_assigned.contains_key(&iv.value_id))
        .filter(|iv| iv.end > iv.start)
        .filter(|iv| match spans_call {
            Some(true) => spans_clobbering_call(liveness, iv, call_points, boundary_ok),
            Some(false) => !spans_clobbering_call(liveness, iv, call_points, boundary_ok),
            None => true,
        })
        .collect();

    candidates.sort_by(|a, b| {
        let score_a = use_count.get(&a.value_id).copied().unwrap_or(1);
        let score_b = use_count.get(&b.value_id).copied().unwrap_or(1);
        score_b.cmp(&score_a)
            .then_with(|| {
                let len_a = (a.end - a.start) as u64;
                let len_b = (b.end - b.start) as u64;
                len_b.cmp(&len_a)
            })
    });

    candidates
}


#[cfg(test)]
mod tests {
    use super::*;

    fn liveness(intervals: Vec<LiveInterval>, call: u32, dest: Option<u32>) -> LivenessResult {
        LivenessResult {
            intervals,
            call_points: vec![call],
            reg_args_call_points: vec![(call, dest)],
            block_loop_depth: Vec::new(),
            segments: FxHashMap::default(),
        }
    }

    /// A call at the start or end of an interval only lets the value stay in
    /// a caller-saved register when the call defines it or is its last use.
    /// An interval that starts at a call because the value is live into the
    /// block the call opens is live across that call.
    #[test]
    fn test_call_boundary_exemptions() {
        let result = LiveInterval { start: 10, end: 20, value_id: 1 };
        let arg = LiveInterval { start: 2, end: 10, value_id: 2 };
        let live_in = LiveInterval { start: 10, end: 30, value_id: 3 };
        let across = LiveInterval { start: 5, end: 15, value_id: 4 };
        let lv = liveness(vec![result, arg, live_in, across], 10, Some(1));
        let ok = lv.reg_args_call_points.clone();
        assert!(!spans_clobbering_call(&lv, &result, &lv.call_points, &ok));
        assert!(!spans_clobbering_call(&lv, &arg, &lv.call_points, &ok));
        assert!(spans_clobbering_call(&lv, &live_in, &lv.call_points, &ok));
        assert!(spans_clobbering_call(&lv, &across, &lv.call_points, &ok));
        // Without the exemption list every one of them spans the call.
        assert!(spans_clobbering_call(&lv, &result, &lv.call_points, &[]));
        assert!(spans_clobbering_call(&lv, &arg, &lv.call_points, &[]));
    }

    /// A call that falls in a hole between a value's live segments is not
    /// one the value spans.
    #[test]
    fn test_call_in_segment_hole() {
        let iv = LiveInterval { start: 2, end: 30, value_id: 7 };
        let mut lv = liveness(vec![iv], 15, None);
        assert!(spans_clobbering_call(&lv, &iv, &lv.call_points.clone(), &[]));
        lv.segments.insert(7, vec![(2, 8), (20, 30)]);
        assert!(!spans_clobbering_call(&lv, &iv, &lv.call_points.clone(), &[]));
    }
}
