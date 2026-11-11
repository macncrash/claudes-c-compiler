//! Function inlining pass.
//!
//! Inlines small static/static-inline functions and `__attribute__((always_inline))`
//! functions at their call sites. Normal inlining is critical for eliminating dead
//! branches guarded by constant-returning inline functions (e.g., kernel's
//! `IS_ENABLED()` patterns). Always-inline is critical for kernel code where
//! functions must remain in their caller's section (e.g., `.noinstr.text`).
//!
//! After inlining, subsequent passes (constant fold, DCE, CFG simplify) clean up
//! the inlined code and eliminate dead branches.

use crate::ir::reexports::{
    BasicBlock, BlockId, CallInfo, GlobalInit, IrFunction, IrModule, Instruction, Operand,
    Terminator, Value, IrConst, IrBinOp, IrParam,
};
use crate::common::types::AliasClass;
use crate::common::asm_constraints::constraint_is_immediate_only;
use crate::common::types::{IrType, AddressSpace};
use std::collections::HashMap;
use crate::common::fx_hash::FxHashSet;

/// Maximum number of IR instructions (across all blocks) in a callee for it
/// to be eligible for inlining. This handles constant-returning helpers
/// like IS_ENABLED() wrappers and small accessor functions, as well as
/// moderately-sized static inline functions with simple control flow.
const MAX_INLINE_INSTRUCTIONS: usize = 60;

/// Maximum number of basic blocks in a callee for inlining eligibility.
/// Must be high enough to handle static inline functions with control flow
/// (e.g., if/else chains, early returns). GCC inlines these aggressively.
const MAX_INLINE_BLOCKS: usize = 6;

/// Maximum total inlining budget per caller function (total inlined instructions).
/// Prevents exponential blowup from recursive inlining chains.
const MAX_INLINE_BUDGET_PER_CALLER: usize = 800;

/// Maximum total instruction count for a caller function after inlining.
/// When the caller exceeds this threshold, normal (non-always_inline) inlining
/// stops. This prevents stack frame bloat: in CCC's codegen model, each SSA
/// value gets a stack slot (~8 bytes), so a function with many instructions
/// can easily produce a multi-KB stack frame that overflows the kernel's 16KB
/// stack. GCC enforces similar limits via -fconserve-stack.
/// Set to 200 to keep stack frames under ~2KB even after optimization,
/// leaving headroom for callers higher on the call stack (kernel functions
/// like mm/page_alloc.c have 10+ level deep call chains that can easily
/// overflow the 16KB kernel stack if individual frames are too large).
const MAX_CALLER_INSTRUCTIONS_AFTER_INLINE: usize = 200;

/// Maximum instructions for __attribute__((always_inline)) functions.
/// GCC always inlines __attribute__((always_inline)) regardless of size, and
/// failing to do so can cause section mismatch errors in the kernel (e.g., when
/// an always_inline function in .text accesses __initconst data, but its caller
/// is in .init.text). Stack frame bloat from large inlined functions is handled
/// separately by MAX_CALLER_INSTRUCTIONS_AFTER_INLINE for normal inlining and
/// by the kernel's -fconserve-stack. Set high enough to cover all real always_inline
/// functions in the kernel (e.g., intel_pmu_init_hybrid at ~250 IR instructions).
const MAX_ALWAYS_INLINE_INSTRUCTIONS: usize = 500;

/// Maximum blocks for __attribute__((always_inline)) functions.
/// GCC has no block limit for always_inline — it always inlines them.
/// Large always_inline kernel functions like __mutex_lock_common in
/// kernel/locking/mutex.c can generate 215+ basic blocks from complex
/// control flow (multiple if/else chains, error handling). Set high
/// enough to handle all real kernel always_inline functions.
const MAX_ALWAYS_INLINE_BLOCKS: usize = 500;

/// Maximum instructions for a callee to be considered "tiny" and always inlined
/// regardless of caller size. Tiny functions like `static inline bool f(void) { return false; }`
/// must always be inlined because:
/// 1. They have negligible impact on code/stack size
/// 2. Not inlining them can cause linker errors (references to symbols that are
///    only needed in dead code paths, e.g., kernel's folio_test_large_rmappable()
///    returns false when CONFIG_TRANSPARENT_HUGEPAGE is disabled, making the call
///    to folio_undo_large_rmappable() dead — but if not inlined, the linker sees
///    the undefined reference)
/// 3. GCC always inlines these trivial static inline functions
const MAX_TINY_INLINE_INSTRUCTIONS: usize = 5;

/// Maximum instructions for a callee to be considered "small" and always inlined
/// regardless of caller size. Small functions like `static inline void f(x, flag) { if (flag) g(x); }`
/// have 2-3 blocks (from if/else) and ~10-20 instructions. They must be inlined
/// because not inlining them can cause linker errors when they contain conditional
/// calls to symbols that don't exist in the current build configuration. Example:
/// kernel's fscache_clear_page_bits() calls __fscache_clear_page_bits() conditionally,
/// but if CONFIG_FSCACHE is disabled, the latter is not compiled. GCC inlines the
/// wrapper, so the conditional call becomes part of the caller — no linker error.
/// Without inlining, the standalone static function has an undefined reference.
const MAX_SMALL_INLINE_INSTRUCTIONS: usize = 20;

/// Maximum blocks for a callee to be considered "small" (see above).
const MAX_SMALL_INLINE_BLOCKS: usize = 3;

/// A call site that passes constants is treated as small when the callee,
/// specialized on those constants and cleaned up by the post-inline passes,
/// shrinks to at most this many instructions (and MAX_SMALL_INLINE_BLOCKS
/// blocks). Callee sizes are otherwise measured before mem2reg and folding, so
/// a helper like `ray_att(DIR, sq, occ)` that indexes a table by its constant
/// argument looks several times larger than the code it actually becomes.
const MAX_SPECIALIZED_INLINE_INSTRUCTIONS: usize = 20;

/// A callee whose cleaned-up size (mem2reg, folding, GVN, with the call
/// site's constant arguments) is at most this many instructions, in at most
/// MAX_INLINE_BLOCKS blocks, is inlined regardless of the caller's size, as
/// long as it fits the caller's inlining budget. Close to clang's default -O2
/// threshold; a function written as a helper usually is about this small
/// once its parameters and locals live in registers.
const MAX_CLEANED_INLINE_INSTRUCTIONS: usize = 40;

/// The cleaned-up size limit instead for a static function with one call
/// in the module and its address never taken: inlined, its body goes, so
/// only the call is saved (clang weighs a local function's last call the
/// same way). scalar-bench's ht_delete, 48 instructions cleaned up, was a
/// call with its constants built again at every key deleted.
const MAX_SOLE_CALL_INLINE_INSTRUCTIONS: usize = 100;
const MAX_SOLE_CALL_INLINE_BLOCKS: usize = 16;

/// Upper bound on the small callees inlined into a specialized copy while
/// measuring it.
const MAX_SPECIALIZED_NESTED_INLINES: usize = 8;

/// Most blocks (before mem2reg) of a static or external function considered
/// for inlining by its cleaned-up size alone. Raw blocks overstate the
/// cleaned-up function: SQLite's sqlite3PagerWrite, five tests and five
/// returns, is 19 raw blocks and 10 cleaned up, five of them returns. One
/// over 2 * MAX_INLINE_BLOCKS, like those returns counting as jumps, is
/// left out of callers past MAX_CALLER_INSTRUCTIONS_ABSOLUTE_CAP. Allowed
/// there, the wider gate made sqlite3VdbeExec, the interpreter loop, run
/// 4% more instructions, its register allocation spilling on the dispatch
/// path as cold opcode handlers grew, and counting only non-return blocks
/// made speedtest as a whole run 4% more.
/// The cleaned-up limit for a call in a loop of its caller (in place of
/// MAX_CLEANED_INLINE_INSTRUCTIONS): chess-perft's add_pawn_move, 48
/// instructions, called per pawn move from generate's loops. (Not for
/// vector code: vector-bench's select_clamp, inlined into main's timing
/// loop, ran its lanes through the stack around main's calls, 614M more
/// instructions. Nor for a callee with calls of its own: zlib's
/// flush_pending, on a rare path in deflate's loops, cost 4.8M.)
const MAX_LOOP_CLEANED_INLINE_INSTRUCTIONS: usize = 60;

const MAX_CLEANED_ONLY_RAW_BLOCKS: usize = 4 * MAX_INLINE_BLOCKS;

/// Maximum instructions for a `static` (non-`inline`) function to be eligible
/// for inlining. GCC at -O2 inlines small static functions even without the
/// `inline` keyword. This is critical for correctness: if a static function
/// references an undefined symbol in a conditionally-compiled code path, GCC
/// eliminates the reference by inlining, but without inlining we get a linker
/// error. Example: kernel's pxp_fw_dependencies_completed() references
/// intel_pxp_gsccs_is_ready_for_sessions() which is not compiled in some configs.
const MAX_STATIC_NONINLINE_INSTRUCTIONS: usize = 30;

/// Maximum blocks for a `static` (non-`inline`) function to be eligible.
const MAX_STATIC_NONINLINE_BLOCKS: usize = 4;

/// Budget for always_inline callees per caller. This budget is ONLY consumed
/// by true __attribute__((always_inline)) callees that exceed the "small"
/// threshold (> MAX_SMALL_INLINE_INSTRUCTIONS or > MAX_SMALL_INLINE_BLOCKS).
/// Non-always_inline callees use the separate budget_remaining, ensuring they
/// can never starve always_inline functions of budget. This separation prevents
/// section mismatch errors (e.g., idle_init → fork_idle) that occur when
/// always_inline functions fail to inline.
///
/// When the caller has a section attribute (e.g., .init.text), always_inline
/// callees bypass this budget entirely. This is critical for kernel __init
/// functions like intel_pmu_init that call hundreds of always_inline helpers
/// referencing .init.rodata — not inlining them causes modpost errors.
///
/// Small always_inline callees (≤ MAX_SMALL_INLINE_INSTRUCTIONS) don't consume
/// this budget because they have negligible impact and must be inlined for
/// linker correctness (inline asm "i" constraints, undefined symbols).
///
/// Set to 200 to keep stack frames from always_inline inlining under ~2KB.
/// CCC allocates ~8 bytes per SSA value, so 200 instructions add ~1.6KB to
/// the stack frame. Combined with the base function's frame, this typically
/// keeps total frame size under ~2KB, leaving headroom in the kernel's 16KB
/// stack for deep call chains (e.g., mm/page_alloc.c has 10+ levels).
/// Functions with section attributes bypass the budget entirely (to avoid
/// modpost errors). The standalone bodies of always_inline functions that
/// aren't fully inlined remain correct because __attribute__((error)) calls
/// are lowered as no-ops (not traps).
const MAX_ALWAYS_INLINE_BUDGET_PER_CALLER: usize = 200;

/// Budget for always_inline callees outside a kernel, which has no 16KB stack
/// to protect. always_inline is a promise there: 42 such calls left as calls
/// (load32, round_fn, g) cost BLAKE3's portable code a fifth of its time.
/// This only bounds runaway growth.
const MAX_ALWAYS_INLINE_BUDGET_USER: usize = 50000;

/// Rounds of the second pass, each inlining one site. Outside a kernel only the
/// budget limits them: a limit here left 666 NEON intrinsics in one function as
/// calls, each moving its vectors through memory.
fn always_inline_second_pass_rounds() -> usize {
    if crate::common::types::building_kernel() { MAX_ALWAYS_INLINE_SECOND_PASS_ROUNDS } else { MAX_ALWAYS_INLINE_BUDGET_USER }
}

fn always_inline_budget(kernel_budget: usize) -> usize {
    if crate::common::types::building_kernel() { kernel_budget } else { MAX_ALWAYS_INLINE_BUDGET_USER }
}

/// Additional always_inline budget for the second (correctness) pass.
/// After the main inlining loop exhausts max_rounds, any remaining
/// always_inline call sites are processed in a second pass with this
/// independent budget. This is separate from the main budget because
/// the second pass handles correctness-critical cases (e.g., KVM nVHE
/// functions where always_inline chains reference section-specific
/// symbols like __kvm_nvhe_gic_nonsecure_priorities that only exist
/// when inlined). Set to 400 to cover typical 2-3 level always_inline
/// chains (e.g., cpucap_is_possible has ~84 instructions and appears
/// 3+ times per function, totaling ~252 instructions). This limits
/// stack frame growth: the worst case adds ~400 * 8 = ~3.2KB to the
/// frame, but in practice many inlined instructions are eliminated by
/// constant folding and DCE.
const MAX_ALWAYS_INLINE_SECOND_PASS_BUDGET: usize = 400;

/// Maximum number of rounds for the second (correctness) pass.
/// Each round inlines one always_inline call site and re-scans.
/// Set high enough to handle functions with many always_inline call
/// sites that weren't reached in the main loop's max_rounds.
const MAX_ALWAYS_INLINE_SECOND_PASS_ROUNDS: usize = 300;

/// Hard cap on caller instruction count. When a caller exceeds this threshold,
/// even always_inline inlining is stopped (except for tiny callees that must be
/// inlined to avoid linker errors). This prevents kernel stack overflow from
/// deeply-nested always_inline chains (e.g., mm/page_alloc.c's __rmqueue ->
/// __rmqueue_smallest -> __rmqueue_fallback chain, all always_inline, which
/// can create functions with 1000+ instructions and 3KB+ stack frames that
/// overflow the kernel's 16KB stack when combined with deep call chains).
/// GCC can tolerate larger functions because its register allocator keeps most
/// values in registers; CCC's codegen spills every SSA value to the stack
/// (~8 bytes each), so we must be more conservative.
const MAX_CALLER_INSTRUCTIONS_HARD_CAP: usize = 500;

/// Absolute hard cap on caller instruction count for normal inlining.
/// When a caller exceeds this threshold, normal inlining stops — only
/// always_inline callees and tiny/small callees continue to be inlined.
///
/// This prevents catastrophic stack frame bloat in functions like the kernel's
/// shrink_folio_list (mm/vmscan.c), which calls hundreds of small inline
/// helpers. CCC's accumulator-based codegen creates one stack slot per SSA value,
/// so inlining many calls can produce thousands of multi-block values
/// with wide liveness intervals, creating 16KB+ stack frames that overflow the
/// kernel's 16KB stack.
///
/// always_inline callees are exempt because not inlining them violates C
/// semantics and causes section mismatch errors in the kernel (e.g., __init
/// callers referencing __initconst data through always_inline helpers that
/// end up as standalone .text functions). GCC always inlines them regardless
/// of caller size.
///
/// Tiny/small callees (≤ MAX_SMALL_INLINE_INSTRUCTIONS) are also exempt because:
/// 1. They have negligible impact on code/stack size
/// 2. Not inlining them can cause linker errors (conditional references to
///    undefined symbols, inline asm "i" constraints, section mismatches)
const MAX_CALLER_INSTRUCTIONS_ABSOLUTE_CAP: usize = 1000;

/// Maximum iterations when tracing IR value chains (Load->Store->Copy->GEP->...)
/// to resolve inline asm operands back to GlobalAddr or constant values.
const MAX_TRACE_CHAIN_LENGTH: usize = 20;

/// Maximum recursion depth for trace_operand_to_const when evaluating BinOp/Cmp
/// trees where both operands themselves need recursive tracing.
const MAX_TRACE_RECURSION_DEPTH: u32 = 10;

/// Select the best call site to inline from the given candidates.
///
/// Uses a two-pass strategy:
/// 1. First pass: pick tiny/small/static-inline callees (always inlined for
///    code correctness, e.g., constant-returning stubs, linker symbol resolution).
/// 2. Second pass: pick the first eligible normal callee that fits within
///    budget and caller size constraints.
///
/// Returns `(site, callee_inst_count, use_relaxed)` or `None` if no eligible site.
fn select_inline_site(
    call_sites: &[InlineCallSite],
    callee_map: &HashMap<String, CalleeData>,
    caller_too_large: bool,
    caller_at_hard_cap: bool,
    caller_at_absolute_cap: bool,
    caller_has_section: bool,
    caller_is_recursive: bool,
    caller_in_cycle: bool,
    budget_remaining: usize,
    always_inline_budget_remaining: usize,
    spec_cache: &mut HashMap<String, Option<CleanedSize>>,
) -> Option<(InlineCallSite, usize, bool, bool)> {
    // First pass: look for tiny/small callees anywhere in the function.
    // These are always inlined regardless of caller size because:
    // 1. They have negligible impact on code/stack size
    // 2. Not inlining them can cause linker errors from conditional
    //    references to undefined symbols (e.g., fscache_clear_page_bits)
    //
    // However, once the always_inline budget is exhausted, only TINY
    // callees (≤5 instructions, single block) and small __always_inline
    // callees are picked. Non-always_inline small callees (6-20 instructions)
    // individually have "negligible" impact but collectively cause catastrophic
    // stack bloat when a function has 200+ call sites (e.g., kernel's
    // shrink_folio_list). Small __always_inline callees are still inlined
    // because they have correctness requirements: inline asm "i" constraints
    // (e.g., arch_static_branch's __jump_table entries) need resolved symbol
    // references, and their standalone bodies emit invalid assembly (like
    // ".dword 0 - .") when the symbol can't be resolved.
    let budget_exhausted = always_inline_budget_remaining == 0;
    for site in call_sites {
        let callee_data = &callee_map[&site.callee_name];
        let callee_inst_count: usize = effective_size(&callee_data.blocks);
        let is_tiny = callee_inst_count <= MAX_TINY_INLINE_INSTRUCTIONS
            && callee_data.blocks.len() <= 1;
        let is_small = callee_inst_count <= MAX_SMALL_INLINE_INSTRUCTIONS
            && callee_data.blocks.len() <= MAX_SMALL_INLINE_BLOCKS;
        // Static inline functions that fit within normal limits should
        // always be inlined, matching GCC behavior. This is critical for
        // functions like ror32 (35 instructions) called from blake2s: without
        // inlining, shift amounts can't be constant-propagated, producing
        // massive unoptimized code with 28KB+ stack frames that overflow
        // the kernel's 16KB stack.
        let is_static_inline_eligible = callee_data.is_static_inline
            && callee_inst_count <= MAX_INLINE_INSTRUCTIONS
            && callee_data.blocks.len() <= MAX_INLINE_BLOCKS;
        // For recursive callers, only inline tiny callees and always_inline callees.
        // Inlining larger callees into recursive functions multiplies the stack frame
        // increase by the recursion depth, easily causing stack overflow.
        // A callee that cleans up small (after mem2reg and folding, with this
        // site's constant arguments) is small, measured as it will be: up to
        // MAX_CLEANED_INLINE_INSTRUCTIONS (within the caller's budget), or
        // MAX_SPECIALIZED_INLINE_INSTRUCTIONS in a recursive caller, where
        // anything larger would multiply its stack frame by the recursion depth.
        let (max_insts, max_blocks) = if caller_is_recursive {
            (MAX_SPECIALIZED_INLINE_INSTRUCTIONS, MAX_SMALL_INLINE_BLOCKS)
        } else if callee_data.sole_call && site.in_loop {
            (MAX_SOLE_CALL_INLINE_INSTRUCTIONS, MAX_SOLE_CALL_INLINE_BLOCKS)
        } else if site.in_loop && !callee_data.vector && !callee_data.calls {
            (MAX_LOOP_CLEANED_INLINE_INSTRUCTIONS, MAX_INLINE_BLOCKS)
        } else {
            (MAX_CLEANED_INLINE_INSTRUCTIONS, MAX_INLINE_BLOCKS)
        };
        // Blocks that only return become jumps to the join once inlined, so
        // outside the largest callers only the others count (see
        // MAX_CLEANED_ONLY_RAW_BLOCKS for why those keep the stricter rules).
        // A call that arrived in a body admitted only that way was not
        // counted in its size, and stays a call by this rule.
        let relaxed = !caller_is_recursive && !caller_at_absolute_cap;
        let cleaned = (!is_tiny && !budget_exhausted && !site.in_cleaned_body)
            .then(|| specialized_size_cached(spec_cache, callee_map, &site.callee_name, &site.const_args))
            .flatten()
            .filter(|c| c.insts <= max_insts && (c.blocks <= max_blocks || ((relaxed || caller_is_recursive) && c.non_return_blocks <= max_blocks))
                && (c.insts <= MAX_SPECIALIZED_INLINE_INSTRUCTIONS || c.insts <= budget_remaining));
        // Whatever its raw size, what cleans up to a tiny body costs a caller nothing.
        let cleans_tiny = cleaned.is_some_and(|c| c.insts <= MAX_TINY_INLINE_INSTRUCTIONS && c.blocks <= 1);
        if callee_data.wide && !relaxed && !cleans_tiny {
            continue;
        }
        if callee_data.in_cycle && (caller_in_cycle || caller_is_recursive || cleaned.is_none()) {
            continue;
        }
        if (caller_is_recursive || callee_data.cleaned_only) && !is_tiny && !callee_data.is_always_inline
            && cleaned.is_none() {
            continue;
        }
        if is_tiny || (is_small && (!budget_exhausted || callee_data.is_always_inline)) || is_static_inline_eligible
            || cleaned.is_some() {
            let use_relaxed = callee_data.is_always_inline || callee_data.exceeds_normal_limits;
            let cost = cleaned.map_or(callee_inst_count, |c| c.insts);
            // Only a body admitted because its return blocks did not count
            // keeps its calls: one within the block limit lets its calls be
            // inlined in turn (chess-perft's rook_att, inlined into generate,
            // brings ray_att, whose lsb and msb follow).
            let by_cleaned = callee_data.in_cycle
                || (!(is_tiny || (is_small && (!budget_exhausted || callee_data.is_always_inline)) || is_static_inline_eligible)
                    && cleaned.is_some_and(|c| c.blocks > max_blocks));
            return Some((site.clone(), cost, use_relaxed, by_cleaned));
        }
    }

    // Second pass: use the first eligible normal callee.
    for site in call_sites {
        let callee_data = &callee_map[&site.callee_name];
        let callee_inst_count: usize = effective_size(&callee_data.blocks);
        // For recursive callers, skip non-tiny, non-always_inline callees.
        // (Tiny callees were handled in the first pass, as were cleaned-only
        // ones that qualify.)
        if (caller_is_recursive || callee_data.cleaned_only || callee_data.in_cycle) && !callee_data.is_always_inline {
            continue;
        }
        // When the caller has a section attribute (e.g., .init.text),
        // allow inlining small callees even into large callers to
        // prevent section mismatch errors.
        if caller_too_large && !callee_data.is_always_inline
            && (!caller_has_section || callee_inst_count > MAX_SMALL_INLINE_INSTRUCTIONS) {
                continue;
            }
        // Absolute cap: stop normal inlining for extremely large callers.
        // always_inline callees MUST still be inlined (C semantic requirement).
        if caller_at_absolute_cap && !callee_data.is_always_inline {
            continue;
        }
        // Hard cap: stop normal inlining to prevent kernel stack overflow.
        // always_inline callees are still inlined (C semantic requirement),
        // but are limited by the always_inline budget.
        if caller_at_hard_cap && !callee_data.is_always_inline {
            continue;
        }
        let use_relaxed = callee_data.is_always_inline || callee_data.exceeds_normal_limits;
        // Budget enforcement: always_inline callees use a separate budget;
        // non-always_inline callees use the normal budget.
        if callee_data.is_always_inline {
            // When the caller has a section attribute, always_inline callees
            // bypass the budget entirely (critical for kernel init functions
            // like intel_pmu_init that call hundreds of always_inline helpers).
            if !caller_has_section {
                let is_tiny = callee_inst_count <= MAX_TINY_INLINE_INSTRUCTIONS
                    && callee_data.blocks.len() <= 1;
                if !is_tiny && callee_inst_count > always_inline_budget_remaining {
                    continue;
                }
            }
        } else if callee_inst_count > budget_remaining {
            continue;
        }
        return Some((site.clone(), callee_inst_count, use_relaxed, false));
    }

    None
}

/// Run the inlining pass on the module.
/// Returns the number of call sites inlined.
pub fn run(module: &mut IrModule) -> usize {
    let total_inlined = inline_within_limits(module) + expand_large_always_inline(module);

    // After ALL inlining is complete, resolve input_symbols for InlineAsm instructions.
    // This must run after the entire inlining pass because multi-level inline chains
    // (e.g., arch_static_branch → static_key_false → trace_tlb_flush) need all levels
    // to be inlined before we can trace values back to their original GlobalAddr/Const.
    // Running resolution per-function would fail for intermediate functions whose
    // parameters haven't been replaced with concrete values yet.
    for func_idx in 0..module.functions.len() {
        if module.functions[func_idx].has_inlined_calls {
            resolve_inline_asm_symbols(&mut module.functions[func_idx]);
        }
    }

    total_inlined
}

/// Expand the always_inline functions too large for `inline_within_limits`,
/// outside a kernel: lz4's match finder, with its flags constants at each call,
/// stayed a function testing them in its inner loop.
///
/// This comes after the other inlining, and bottom up: the helpers inlined
/// into these bodies by then would stay calls if the bodies went into a caller
/// first, past its size limits.
fn expand_large_always_inline(module: &mut IrModule) -> usize {
    if crate::common::types::building_kernel() {
        return 0;
    }
    let skip_list: Vec<String> = std::env::var("CCC_INLINE_SKIP")
        .unwrap_or_default()
        .split(',')
        .filter(|s| !s.is_empty())
        .map(|s| s.trim().to_string())
        .collect();
    let mut global_max_block_id = module.functions.iter().flat_map(|f| &f.blocks)
        .map(|b| b.label.0).max().unwrap_or(0);
    let mut total = 0;
    loop {
        let mut large = build_callee_map_for(module, true);
        if large.is_empty() {
            return total;
        }
        // Those calling no other are finished, so go first. Each round
        // finishes more, or inlined nothing and is the last.
        let names: FxHashSet<String> = large.keys().cloned().collect();
        large.retain(|_, c| !c.blocks.iter().flat_map(|b| &b.instructions).any(|i|
            matches!(i, Instruction::Call { func, .. } if names.contains(func))));
        let mut inlined = 0;
        for func in module.functions.iter_mut().filter(|f| !f.is_declaration) {
            let has_section = func.section.is_some();
            loop {
                let sites = find_inline_call_sites(func, &large, &skip_list, has_section);
                let size = effective_size(&func.blocks);
                let site = sites.iter().find(|s|
                    size + effective_size(&large[&s.callee_name].blocks) <= MAX_ALWAYS_INLINE_BUDGET_USER);
                match site {
                    Some(site) if inline_call_site(func, site, &large[&site.callee_name], &mut global_max_block_id) => inlined += 1,
                    _ => break,
                }
            }
        }
        if inlined == 0 {
            return total;
        }
        total += inlined;
    }
}

fn inline_within_limits(module: &mut IrModule) -> usize {
    let mut total_inlined = 0;
    let debug_inline = std::env::var("CCC_INLINE_DEBUG").is_ok();
    let skip_list: Vec<String> = std::env::var("CCC_INLINE_SKIP")
        .unwrap_or_default()
        .split(',')
        .filter(|s| !s.is_empty())
        .map(|s| s.trim().to_string())
        .collect();

    // Build a snapshot of eligible callees (we can't borrow module mutably while reading callees).
    // We clone the callee function bodies since we need them while mutating callers.
    let callee_map = build_callee_map(module);
    let in_cycle: FxHashSet<String> = recursive_functions(module).into_iter().map(str::to_string).collect();
    let mut spec_cache: HashMap<String, Option<CleanedSize>> = HashMap::new();

    if callee_map.is_empty() {
        return 0;
    }

    if debug_inline {
        eprintln!("[INLINE] Callee map has {} eligible functions:", callee_map.len());
        for (name, data) in &callee_map {
            let ic: usize = effective_size(&data.blocks);
            eprintln!("[INLINE]   '{}': {} blocks, {} instructions, {} params",
                name, data.blocks.len(), ic, data.num_params);
        }
    }

    // Compute the module-global max block ID. Block labels (.LBB{id}) are global in the
    // assembly output, so inlined blocks must use IDs that don't collide with ANY
    // function's block IDs, not just the caller's.
    let mut global_max_block_id: u32 = 0;
    for func in &module.functions {
        for block in &func.blocks {
            if block.label.0 > global_max_block_id {
                global_max_block_id = block.label.0;
            }
        }
    }

    // Process each function as a potential caller
    for func_idx in 0..module.functions.len() {
        if module.functions[func_idx].is_declaration {
            continue;
        }

        let caller_has_section = module.functions[func_idx].section.is_some();
        // Check if the caller is directly recursive (calls itself).
        // If so, we must be very conservative about inlining other callees
        // into it, because each inlined callee increases the stack frame
        // that gets multiplied by the recursion depth.
        let caller_is_recursive = {
            let func = &module.functions[func_idx];
            func.blocks.iter().any(|block| {
                block.instructions.iter().any(|inst| {
                    if let Instruction::Call { func: callee_name, .. } = inst {
                        callee_name == &func.name
                    } else {
                        false
                    }
                })
            })
        };
        let caller_in_cycle = in_cycle.contains(&module.functions[func_idx].name);
        let mut budget_remaining = MAX_INLINE_BUDGET_PER_CALLER;
        let mut always_inline_budget_remaining = always_inline_budget(MAX_ALWAYS_INLINE_BUDGET_PER_CALLER);
        // Iterate to handle chains of inlined calls (A calls B calls C, all small inline).
        // Limit iterations to prevent infinite loops from recursive inline functions.
        let max_rounds = 200;
        // Block labels [first, merge) of the bodies inlined for their
        // cleaned-up size.
        let mut cleaned_bodies: Vec<(u32, u32)> = Vec::new();
        for _round in 0..max_rounds {

            // Check if the caller has grown too large for further normal inlining.
            // Each SSA value in CCC gets an 8-byte stack slot, so functions with
            // too many instructions will have massive stack frames. Stop normal
            // inlining once the caller exceeds the threshold; always_inline
            // callees are still inlined (required by C semantics).
            let caller_inst_count: usize = effective_size(&module.functions[func_idx].blocks);
            let caller_too_large = caller_inst_count > MAX_CALLER_INSTRUCTIONS_AFTER_INLINE;
            let caller_at_hard_cap = caller_inst_count > MAX_CALLER_INSTRUCTIONS_HARD_CAP;
            let caller_at_absolute_cap = caller_inst_count > MAX_CALLER_INSTRUCTIONS_ABSOLUTE_CAP;

            // Find call sites to inline in the current function.
            // When the caller has a custom section attribute, also consider callees
            // that exceed normal limits, to avoid dangerous cross-section calls.
            let mut call_sites = find_inline_call_sites(&module.functions[func_idx], &callee_map, &skip_list, caller_has_section);
            for site in &mut call_sites {
                let label = module.functions[func_idx].blocks[site.block_idx].label.0;
                site.in_cleaned_body = cleaned_bodies.iter().any(|&(lo, hi)| label >= lo && label < hi);
            }
            if call_sites.is_empty() {
                break;
            }

            // Select best call site (prioritizes tiny/small, respects budgets).
            let found_site = select_inline_site(
                &call_sites,
                &callee_map,
                caller_too_large,
                caller_at_hard_cap,
                caller_at_absolute_cap,
                caller_has_section,
                caller_is_recursive,
                caller_in_cycle,
                budget_remaining,
                always_inline_budget_remaining,
                &mut spec_cache,
            );
            let (site, callee_inst_count, _use_relaxed, by_cleaned) = match found_site {
                Some(s) => s,
                None => {
                    if debug_inline && caller_too_large {
                        eprintln!("[INLINE] No more always_inline callees to inline into '{}' (caller has {} instructions)",
                            module.functions[func_idx].name, caller_inst_count);
                    }
                    break;
                }
            };
            let callee_data = &callee_map[&site.callee_name];

            let first_block = global_max_block_id + 1;
            let success = inline_call_site(
                &mut module.functions[func_idx],
                &site,
                callee_data,
                &mut global_max_block_id,
            );

            if success {
                if by_cleaned {
                    cleaned_bodies.push((first_block, global_max_block_id));
                }
                if debug_inline {
                    eprintln!("[INLINE] Inlined '{}' into '{}'", site.callee_name, module.functions[func_idx].name);
                }
                if std::env::var("CCC_INLINE_VALIDATE").is_ok() {
                    validate_function_values(&module.functions[func_idx], &site.callee_name);
                }
                if std::env::var("CCC_INLINE_DUMP_IR").is_ok() {
                    dump_function_ir(&module.functions[func_idx],
                        &format!("after inlining '{}' into '{}'", site.callee_name, module.functions[func_idx].name));
                }
                // Deduct from the always_inline budget only when the callee
                // is actually always_inline. Non-always_inline callees that
                // use the relaxed path (exceeds_normal_limits) should not
                // consume the always_inline budget — otherwise a large
                // exceeds_normal_limits callee (e.g., find_next_bit with 77
                // instructions) can exhaust the budget and prevent true
                // always_inline callees (e.g., idle_init) from being inlined,
                // causing section mismatch errors.
                let callee_is_always_inline = callee_map.get(&site.callee_name)
                    .map(|d| d.is_always_inline).unwrap_or(false);
                if callee_is_always_inline {
                    // Don't deduct small callees from the always_inline budget.
                    // Small callees (≤20 instructions) have negligible individual
                    // impact and are always inlined regardless of budget (handled
                    // in the first pass). Not counting them preserves budget for
                    // larger always_inline callees that actually matter (e.g.,
                    // intel_pmu_init_glc at 211 instructions needs to be inlined
                    // into intel_pmu_init to avoid section mismatches).
                    let callee_blocks = callee_map.get(&site.callee_name)
                        .map(|d| d.blocks.len()).unwrap_or(0);
                    let is_small = callee_inst_count <= MAX_SMALL_INLINE_INSTRUCTIONS
                        && callee_blocks <= MAX_SMALL_INLINE_BLOCKS;
                    if !is_small {
                        always_inline_budget_remaining = always_inline_budget_remaining.saturating_sub(callee_inst_count);
                    }
                } else {
                    budget_remaining = budget_remaining.saturating_sub(callee_inst_count);
                }
                total_inlined += 1;
                module.functions[func_idx].has_inlined_calls = true;
            } else {
                break;
            }
        }

        // Second pass: ensure all remaining __always_inline call sites are inlined.
        // The main loop above may exhaust max_rounds before processing all call sites
        // in functions with 200+ inline sites (e.g., kernel's ___slab_alloc in mm/slub.c).
        // When always_inline functions like cpucap_is_possible() are left un-inlined,
        // their standalone bodies contain BRK traps from unresolved compiletime_assert,
        // causing kernel crashes. This pass also handles cases where the main loop's
        // budget was exhausted, leaving correctness-critical always_inline chains
        // (e.g., KVM nVHE functions referencing section-specific symbols) un-inlined.
        //
        // This pass uses its own independent budget (not shared with the main loop)
        // to allow small always_inline chains needed for correctness while preventing
        // large chains from causing stack overflow. Combined with the main loop, the
        // worst case is 200 + 400 = 600 always_inline instructions per caller.
        //
        // Note: this pass intentionally does NOT check caller_too_large /
        // caller_at_hard_cap / caller_at_absolute_cap. These are correctness-
        // critical inlines (avoiding linker errors and BRK crashes), so they
        // must proceed regardless of caller size. The budget limit (400 inst)
        // provides the growth bound instead.
        let mut second_pass_budget = always_inline_budget(MAX_ALWAYS_INLINE_SECOND_PASS_BUDGET);
        for _round in 0..always_inline_second_pass_rounds() {
            let call_sites = find_inline_call_sites(&module.functions[func_idx], &callee_map, &skip_list, caller_has_section);
            if call_sites.is_empty() {
                break;
            }

            // Only look for always_inline callees, and outside a kernel the
            // tiny ones their bodies bring: zstd's ZSTD_wildcopy, inlined
            // here, left ZSTD_copy16, one 16-byte load and store, a call at
            // 100 sites of its decompression loop.
            let mut found = false;
            for site in &call_sites {
                let callee_data = &callee_map[&site.callee_name];
                let callee_inst_count: usize = effective_size(&callee_data.blocks);
                let is_tiny = callee_inst_count <= MAX_TINY_INLINE_INSTRUCTIONS
                    && callee_data.blocks.len() <= 1;
                if !callee_data.is_always_inline {
                    let mut tiny_once_cleaned = || specialized_size_cached(&mut spec_cache, &callee_map, &site.callee_name, &site.const_args)
                        .is_some_and(|c| c.insts <= MAX_TINY_INLINE_INSTRUCTIONS && c.blocks <= 1);
                    if crate::common::types::building_kernel() || callee_data.in_cycle || caller_is_recursive
                        || !(is_tiny || tiny_once_cleaned()) {
                        continue;
                    }
                }
                let is_small = callee_inst_count <= MAX_SMALL_INLINE_INSTRUCTIONS
                    && callee_data.blocks.len() <= MAX_SMALL_INLINE_BLOCKS;
                // Tiny and small always_inline callees always pass; others must fit
                // in the second pass budget. Small always_inline callees bypass the
                // budget because they have correctness requirements (e.g., inline asm
                // "i" constraints in arch_static_branch). Callers with section
                // attributes bypass budget (section-specific symbols like __kvm_nvhe_*
                // MUST be resolved through inlining).
                if !is_tiny && !is_small && !caller_has_section && callee_inst_count > second_pass_budget {
                    continue;
                }
                let success = inline_call_site(
                    &mut module.functions[func_idx],
                    site,
                    callee_data,
                    &mut global_max_block_id,
                );
                if success {
                    if debug_inline {
                        eprintln!("[INLINE] Inlined always_inline '{}' into '{}' (second pass)",
                            site.callee_name, module.functions[func_idx].name);
                    }
                    if std::env::var("CCC_INLINE_VALIDATE").is_ok() {
                        validate_function_values(&module.functions[func_idx], &site.callee_name);
                    }
                    if std::env::var("CCC_INLINE_DUMP_IR").is_ok() {
                        dump_function_ir(&module.functions[func_idx],
                            &format!("after inlining '{}' into '{}' (second pass)", site.callee_name, module.functions[func_idx].name));
                    }
                    total_inlined += 1;
                    module.functions[func_idx].has_inlined_calls = true;
                    if !is_tiny && !is_small && !caller_has_section {
                        second_pass_budget = second_pass_budget.saturating_sub(callee_inst_count);
                    }
                    found = true;
                    break; // Re-scan after each inline
                }
            }
            if !found {
                break;
            }
        }

    }

    total_inlined
}

/// After inlining, resolve input_symbols for InlineAsm instructions by tracing
/// Value operands back to their definitions. When an always_inline function
/// containing asm goto with "i" constraints is inlined, the constraint operands
/// become IR Values (loaded from parameter allocas) rather than compile-time constants.
/// This function traces those values back through Load/Copy/Store chains to find
/// the original GlobalAddr instruction, recovering the symbol name.
///
/// Without this, the backend sees an "unsatisfiable immediate" and skips the
/// entire asm body (including .pushsection __jump_table entries), breaking the
/// kernel's static branch mechanism and causing boot failures.
fn resolve_inline_asm_symbols(func: &mut IrFunction) {
    // Build a map from Value -> defining instruction for the whole function.
    // We store the instruction itself (cloned) for lookup.
    let mut value_defs: HashMap<u32, Instruction> = HashMap::new();
    // Also track Store instructions: alloca_ptr -> stored value
    // This lets us trace: Load(alloca) -> Store(val, alloca) -> val
    let mut alloca_stores: HashMap<u32, Operand> = HashMap::new();

    for block in func.blocks.iter() {
        for inst in &block.instructions {
            // Record value definitions
            if let Some(v) = inst.dest() {
                value_defs.insert(v.0, inst.clone());
            }
            // Record stores to alloca pointers (the last store wins; for inlined
            // parameter allocas there's typically exactly one store at the top)
            if let Instruction::Store { val, ptr, .. } = inst {
                alloca_stores.insert(ptr.0, *val);
            }
        }
    }

    // Helper: trace a Value back to find a GlobalAddr name + accumulated offset.
    let trace_to_global = |start_val: u32| -> Option<String> {
        trace_value_to_global(start_val, &value_defs, &alloca_stores)
    };

    // Helper: trace a Value or Const operand to find a constant integer value.
    // Delegates to the standalone recursive function.
    let trace_to_const = |op: &Operand| -> Option<i64> {
        trace_operand_to_const(op, &value_defs, &alloca_stores, 0)
    };

    // Now scan all blocks for InlineAsm instructions and fix up input_symbols
    let debug_resolve = std::env::var("CCC_INLINE_DEBUG").is_ok();
    for block in func.blocks.iter_mut() {
        for inst in block.instructions.iter_mut() {
            if let Instruction::InlineAsm { inputs, input_symbols, template, .. } = inst {
                if debug_resolve && template.contains(".pushsection") {
                    eprintln!("[RESOLVE_ASM] Found InlineAsm with .pushsection in func '{}'", func.name);
                    eprintln!("[RESOLVE_ASM]   inputs: {:?}", inputs.iter().map(|(c, o, n)| (c.clone(), format!("{:?}", o), n.clone())).collect::<Vec<_>>());
                    eprintln!("[RESOLVE_ASM]   input_symbols: {:?}", input_symbols);
                }
                let num_outputs_in_sym = if input_symbols.len() > inputs.len() {
                    input_symbols.len() - inputs.len()
                } else {
                    0
                };
                for (i, (constraint, operand, _name)) in inputs.iter_mut().enumerate() {
                    let sym_idx = num_outputs_in_sym + i;
                    if sym_idx >= input_symbols.len() {
                        if debug_resolve { eprintln!("[RESOLVE_ASM]   input[{}]: sym_idx {} >= input_symbols.len() {}, skip", i, sym_idx, input_symbols.len()); }
                        continue;
                    }
                    // Only fix up entries that are currently None
                    if input_symbols[sym_idx].is_some() {
                        if debug_resolve { eprintln!("[RESOLVE_ASM]   input[{}]: already has symbol {:?}, skip", i, input_symbols[sym_idx]); }
                        continue;
                    }
                    // Only care about immediate-only constraints ("i", "n", etc.)
                    if !constraint_is_immediate_only(constraint) {
                        if debug_resolve { eprintln!("[RESOLVE_ASM]   input[{}]: constraint '{}' not imm-only, skip", i, constraint); }
                        continue;
                    }
                    if debug_resolve { eprintln!("[RESOLVE_ASM]   input[{}]: constraint '{}', operand={:?}", i, constraint, operand); }
                    match operand {
                        Operand::Value(v) => {
                            // Try to trace this value back to a GlobalAddr
                            if let Some(sym_name) = trace_to_global(v.0) {
                                if debug_resolve { eprintln!("[RESOLVE_ASM]   -> resolved to symbol '{}'", sym_name); }
                                input_symbols[sym_idx] = Some(sym_name);
                            }
                            // Also try to resolve to a constant and convert the operand
                            else if let Some(const_val) = trace_to_const(&Operand::Value(*v)) {
                                if debug_resolve { eprintln!("[RESOLVE_ASM]   -> resolved to const {}", const_val); }
                                *operand = Operand::Const(IrConst::I64(const_val));
                            } else if debug_resolve { eprintln!("[RESOLVE_ASM]   -> FAILED to resolve Value({})", v.0); }
                        }
                        Operand::Const(_) => {
                            // Already a constant - nothing to fix
                        }
                    }
                }
            }
        }
    }
}

/// Trace a Value back to find a GlobalAddr name + accumulated offset.
/// Follow chains of: Copy(src) -> trace src, Load(ptr) -> Store(val, ptr) -> trace val
/// GEP offsets are accumulated so e.g. GEP(GlobalAddr("foo"), 8) yields "foo+8".
/// GEP with Value offsets are resolved via trace_operand_to_const.
fn trace_value_to_global(
    start_val: u32,
    value_defs: &HashMap<u32, Instruction>,
    alloca_stores: &HashMap<u32, Operand>,
) -> Option<String> {
    let mut current = start_val;
    let mut accumulated_offset: i64 = 0;
    for _ in 0..MAX_TRACE_CHAIN_LENGTH {
        if let Some(inst) = value_defs.get(&current) {
            match inst {
                Instruction::GlobalAddr { name, .. } => {
                    if accumulated_offset > 0 {
                        return Some(format!("{}+{}", name, accumulated_offset));
                    } else if accumulated_offset < 0 {
                        return Some(format!("{}{}", name, accumulated_offset));
                    }
                    return Some(name.clone());
                }
                Instruction::Copy { src: Operand::Value(v), .. } => {
                    current = v.0;
                    continue;
                }
                Instruction::Copy { src: Operand::Const(_), .. } => {
                    return None;
                }
                Instruction::Load { ptr, .. } => {
                    if let Some(stored_val) = alloca_stores.get(&ptr.0) {
                        match stored_val {
                            Operand::Value(v) => {
                                current = v.0;
                                continue;
                            }
                            Operand::Const(_) => return None,
                        }
                    }
                    return None;
                }
                // GEP: accumulate offset (constant or resolvable Value)
                Instruction::GetElementPtr { base, offset, .. } => {
                    let off = match offset {
                        Operand::Const(c) => c.to_i64(),
                        Operand::Value(_) => {
                            trace_operand_to_const(offset, value_defs, alloca_stores, 0)
                        }
                    };
                    if let Some(off) = off {
                        accumulated_offset += off;
                        current = base.0;
                        continue;
                    }
                    return None;
                }
                // Cast preserves pointer identity for address calculations
                Instruction::Cast { src: Operand::Value(v), .. } => {
                    current = v.0;
                    continue;
                }
                // BinOp on pointer: handle Add with constant (pointer arithmetic)
                Instruction::BinOp { op: IrBinOp::Add, lhs, rhs, .. } => {
                    // Try: one operand is a traceable pointer, other is a constant offset
                    if let Some(rhs_val) = trace_operand_to_const(rhs, value_defs, alloca_stores, 0) {
                        if let Operand::Value(v) = lhs {
                            accumulated_offset += rhs_val;
                            current = v.0;
                            continue;
                        }
                    }
                    if let Some(lhs_val) = trace_operand_to_const(lhs, value_defs, alloca_stores, 0) {
                        if let Operand::Value(v) = rhs {
                            accumulated_offset += lhs_val;
                            current = v.0;
                            continue;
                        }
                    }
                    return None;
                }
                _ => return None,
            }
        } else {
            return None;
        }
    }
    None
}

/// Recursively trace an operand to find a compile-time constant integer value.
/// Handles Load/Store/Copy/Cast chains as well as BinOp and Cmp with constant operands.
/// `depth` limits recursion for BinOp/Cmp where both sides need tracing.
fn trace_operand_to_const(
    op: &Operand,
    value_defs: &HashMap<u32, Instruction>,
    alloca_stores: &HashMap<u32, Operand>,
    depth: u32,
) -> Option<i64> {
    if depth > MAX_TRACE_RECURSION_DEPTH {
        return None;
    }
    match op {
        Operand::Const(c) => c.to_i64(),
        Operand::Value(v) => {
            let mut current = v.0;
            for _ in 0..MAX_TRACE_CHAIN_LENGTH {
                if let Some(inst) = value_defs.get(&current) {
                    match inst {
                        Instruction::Copy { src: Operand::Const(c), .. } => {
                            return c.to_i64();
                        }
                        Instruction::Copy { src: Operand::Value(v2), .. } => {
                            current = v2.0;
                            continue;
                        }
                        Instruction::Load { ptr, .. } => {
                            if let Some(stored_val) = alloca_stores.get(&ptr.0) {
                                match stored_val {
                                    Operand::Const(c) => return c.to_i64(),
                                    Operand::Value(v2) => {
                                        current = v2.0;
                                        continue;
                                    }
                                }
                            }
                            return None;
                        }
                        Instruction::Cast { src, .. } => {
                            match src {
                                Operand::Const(c) => return c.to_i64(),
                                Operand::Value(v2) => {
                                    current = v2.0;
                                    continue;
                                }
                            }
                        }
                        // Binary operations: try to evaluate both sides
                        Instruction::BinOp { op: bin_op, lhs, rhs, .. } => {
                            let l = trace_operand_to_const(lhs, value_defs, alloca_stores, depth + 1)?;
                            let r = trace_operand_to_const(rhs, value_defs, alloca_stores, depth + 1)?;
                            return bin_op.eval_i64(l, r);
                        }
                        // Comparisons: try to evaluate both sides
                        Instruction::Cmp { op: cmp_op, lhs, rhs, .. } => {
                            let l = trace_operand_to_const(lhs, value_defs, alloca_stores, depth + 1)?;
                            let r = trace_operand_to_const(rhs, value_defs, alloca_stores, depth + 1)?;
                            return Some(if cmp_op.eval_i64(l, r) { 1 } else { 0 });
                        }
                        _ => return None,
                    }
                } else {
                    return None;
                }
            }
            None
        }
    }
}

/// Debug validation: check that every Value used as an operand is defined by some instruction.
fn validate_function_values(func: &IrFunction, last_inlined_callee: &str) {
    use std::collections::HashSet;

    // Collect all defined values
    let mut defined: HashSet<u32> = HashSet::new();
    for block in &func.blocks {
        for inst in &block.instructions {
            if let Some(v) = inst.dest() {
                defined.insert(v.0);
            }
        }
    }

    // Check all used values
    let mut errors = Vec::new();
    for (block_idx, block) in func.blocks.iter().enumerate() {
        for (inst_idx, inst) in block.instructions.iter().enumerate() {
            for v in inst.used_values() {
                if !defined.contains(&v) {
                    errors.push(format!(
                        "  block[{}] (label .L{}) inst[{}]: uses undefined Value({}), inst={:?}",
                        block_idx, block.label.0, inst_idx, v, short_inst_name(inst)
                    ));
                }
            }
        }
        for v in block.terminator.used_values() {
            if !defined.contains(&v) {
                errors.push(format!(
                    "  block[{}] (label .L{}) terminator: uses undefined Value({})",
                    block_idx, block.label.0, v
                ));
            }
        }
    }

    if !errors.is_empty() {
        eprintln!("[INLINE_VALIDATE] ERRORS in '{}' after inlining '{}': {} undefined value uses",
            func.name, last_inlined_callee, errors.len());
        for e in errors.iter().take(20) {
            eprintln!("{}", e);
        }
        if errors.len() > 20 {
            eprintln!("  ... and {} more", errors.len() - 20);
        }
    }
}

fn short_inst_name(inst: &Instruction) -> &'static str {
    match inst {
        Instruction::Alloca { .. } => "Alloca",
        Instruction::Store { .. } => "Store",
        Instruction::Load { .. } => "Load",
        Instruction::BinOp { .. } => "BinOp",
        Instruction::UnaryOp { .. } => "UnaryOp",
        Instruction::Cmp { .. } => "Cmp",
        Instruction::Call { .. } => "Call",
        Instruction::CallIndirect { .. } => "CallIndirect",
        Instruction::GetElementPtr { .. } => "GEP",
        Instruction::Cast { .. } => "Cast",
        Instruction::Copy { .. } => "Copy",
        Instruction::GlobalAddr { .. } => "GlobalAddr",
        Instruction::Memcpy { .. } => "Memcpy",
        Instruction::Phi { .. } => "Phi",
        Instruction::Select { .. } => "Select",
        _ => "Other",
    }
}

/// Information about a callee function eligible for inlining.
struct CalleeData {
    blocks: Vec<BasicBlock>,
    /// For each param, Some(size) if it's a struct-by-value parameter, None otherwise.
    param_struct_sizes: Vec<Option<usize>>,
    return_type: IrType,
    num_params: usize,
    next_value_id: u32,
    /// Maximum BlockId used in the callee
    max_block_id: u32,
    /// Whether this callee was marked __attribute__((always_inline))
    is_always_inline: bool,
    /// True if this callee exceeds normal inline limits but is within the
    /// relaxed limits used for callers with custom section attributes.
    /// Such callees should only be inlined when the caller has a section attribute,
    /// to avoid cross-section calls that break early boot / noinstr code.
    exceeds_normal_limits: bool,
    /// Whether this callee is a `static inline` function. GCC always inlines
    /// `static inline` functions regardless of caller size. We should do the
    /// same to match GCC behavior and enable critical optimizations (e.g.,
    /// constant propagation of shift amounts in ror32 used by blake2s).
    is_static_inline: bool,
    /// A static function over the raw size limits (measured before mem2reg),
    /// admitted only to be inlined where its cleaned-up size qualifies
    /// (see `select_inline_site`), never by the general rules.
    cleaned_only: bool,
    /// A cleaned-only callee over the raw block limit for the largest
    /// callers (see MAX_CLEANED_ONLY_RAW_BLOCKS).
    wide: bool,
    /// A static function called once, its address never taken: inlined,
    /// its own body goes (see MAX_SOLE_CALL_INLINE_INSTRUCTIONS).
    sole_call: bool,
    /// On a cycle of the call graph: inlined only into callers on none, by
    /// its cleaned-up size, the calls it brings staying calls.
    in_cycle: bool,
    /// Uses vector (NEON) operations, whose registers a caller with calls
    /// saves and restores around each one: no loop bonus.
    vector: bool,
    /// Makes calls of its own (or copies memory with one): no loop bonus
    /// either, as the call it saves is not the only one.
    calls: bool,
    /// The callee's parameters, for building a specialized copy when sizing
    /// call sites that pass constants (see `specialized_size`).
    params: Vec<IrParam>,
}

/// A call site that is eligible for inlining.
#[derive(Clone)]
struct InlineCallSite {
    /// Index of the block containing the call
    block_idx: usize,
    /// Index of the instruction within the block
    inst_idx: usize,
    /// Name of the callee function
    callee_name: String,
    /// The destination value of the call (None for void)
    dest: Option<Value>,
    /// Arguments passed to the call
    args: Vec<Operand>,
    /// For each argument, the constant it is known to be in the caller.
    const_args: Vec<Option<ConstArg>>,
    /// In a body inlined into the caller for its cleaned-up size.
    in_cleaned_body: bool,
    /// In a loop of the caller (a block on a cycle), for a sole-call callee.
    in_loop: bool,
}

/// Whether each block reaches itself: it is in a strongly connected
/// component of more than one block, or branches to itself (iterative
/// Tarjan).
fn blocks_on_cycles(succs: &crate::ir::analysis::FlatAdj, n: usize) -> Vec<bool> {
    let mut num = vec![usize::MAX; n];
    let mut low = vec![0usize; n];
    let mut on_stack = vec![false; n];
    let mut stack: Vec<usize> = Vec::new();
    let mut looped = vec![false; n];
    let mut next = 0;
    for root in 0..n {
        if num[root] != usize::MAX {
            continue;
        }
        let mut work = vec![(root, 0usize)];
        num[root] = next;
        low[root] = next;
        next += 1;
        stack.push(root);
        on_stack[root] = true;
        while let Some(&mut (v, ref mut k)) = work.last_mut() {
            let row = succs.row(v);
            if let Some(&w) = row.get(*k) {
                *k += 1;
                let w = w as usize;
                if w == v {
                    looped[v] = true;
                }
                if num[w] == usize::MAX {
                    num[w] = next;
                    low[w] = next;
                    next += 1;
                    stack.push(w);
                    on_stack[w] = true;
                    work.push((w, 0));
                } else if on_stack[w] {
                    low[v] = low[v].min(num[w]);
                }
                continue;
            }
            work.pop();
            if let Some(&(u, _)) = work.last() {
                low[u] = low[u].min(low[v]);
            }
            if low[v] == num[v] {
                let mut members = Vec::new();
                while let Some(w) = stack.pop() {
                    on_stack[w] = false;
                    members.push(w);
                    if w == v {
                        break;
                    }
                }
                if members.len() > 1 {
                    for w in members {
                        looped[w] = true;
                    }
                }
            }
        }
    }
    looped
}

/// A call argument that is constant in the caller: the constant, plus the cast
/// applied to it when the argument is defined by `cast <from>-><to> <const>`
/// (how the lowering passes integer literals to narrower parameters).
#[derive(Clone, Copy, Debug)]
struct ConstArg {
    value: IrConst,
    cast: Option<(IrType, IrType)>,
}

/// Map each Value in `func` defined as a constant (directly, through copies,
/// or by a cast of a constant) to that constant.
fn constant_values(func: &IrFunction) -> HashMap<u32, ConstArg> {
    let mut consts: HashMap<u32, ConstArg> = HashMap::new();
    let mut copies: Vec<(u32, u32)> = Vec::new();
    for block in &func.blocks {
        for inst in &block.instructions {
            match inst {
                Instruction::Copy { dest, src: Operand::Const(c) } => {
                    consts.insert(dest.0, ConstArg { value: *c, cast: None });
                }
                Instruction::Copy { dest, src: Operand::Value(v) } => copies.push((dest.0, v.0)),
                Instruction::Cast { dest, src: Operand::Const(c), from_ty, to_ty } => {
                    consts.insert(dest.0, ConstArg { value: *c, cast: Some((*from_ty, *to_ty)) });
                }
                _ => {}
            }
        }
    }
    // Resolve copy chains; each round extends them by one link.
    for _ in 0..MAX_TRACE_CHAIN_LENGTH {
        let mut grew = false;
        for &(dest, src) in &copies {
            if !consts.contains_key(&dest) {
                if let Some(c) = consts.get(&src).copied() {
                    consts.insert(dest, c);
                    grew = true;
                }
            }
        }
        if !grew {
            break;
        }
    }
    consts
}

/// `specialized_size`, memoized by callee and argument constants.
/// The instructions of a function body that are expected to survive mem2reg:
/// allocas, ParamRefs and copies, and the loads and stores of promotable
/// scalar locals (allocas only ever loaded from and stored to), are not
/// counted. Bodies are measured before mem2reg, where a parameter alone costs
/// an alloca, a store and a load per use, so the raw count overstates a
/// small helper several times over.
fn effective_size(blocks: &[BasicBlock]) -> usize {
    let mut promotable: FxHashSet<u32> = FxHashSet::default();
    for inst in blocks.iter().flat_map(|b| &b.instructions) {
        if let Instruction::Alloca { dest, volatile: false, .. } = inst {
            promotable.insert(dest.0);
        }
    }
    for inst in blocks.iter().flat_map(|b| &b.instructions) {
        match inst {
            Instruction::Load { .. } => {}
            Instruction::Store { val: Operand::Value(v), .. } => {
                promotable.remove(&v.0);
            }
            Instruction::Store { .. } => {}
            _ => inst.for_each_used_value(|v| { promotable.remove(&v); }),
        }
    }
    blocks.iter().flat_map(|b| &b.instructions).filter(|inst| !match inst {
        Instruction::Alloca { .. } | Instruction::ParamRef { .. } | Instruction::Copy { .. } => true,
        Instruction::Load { ptr, .. } | Instruction::Store { ptr, .. } => promotable.contains(&ptr.0),
        _ => false,
    }).count()
}

/// A callee's size once cleaned up for a call site (see specialized_size).
#[derive(Clone, Copy, Debug)]
struct CleanedSize {
    insts: usize,
    blocks: usize,
    /// Blocks not ending in a return.
    non_return_blocks: usize,
}

fn specialized_size_cached(
    cache: &mut HashMap<String, Option<CleanedSize>>,
    callee_map: &HashMap<String, CalleeData>,
    callee_name: &str,
    const_args: &[Option<ConstArg>],
) -> Option<CleanedSize> {
    let mut key = callee_name.to_string();
    for arg in const_args {
        match arg {
            Some(c) => key.push_str(&format!("|{:?}", c)),
            None => key.push_str("|_"),
        }
    }
    *cache.entry(key.clone()).or_insert_with(|| {
        let size = specialized_size(callee_map, callee_name, const_args);
        if std::env::var("CCC_INLINE_DEBUG").is_ok() {
            eprintln!("[INLINE] specialized size of {}: {:?}", key, size);
        }
        size
    })
}

/// Size of `callee` once the constant arguments of a call site are folded
/// into it: instructions and blocks after mem2reg and the same cleanup passes
/// that follow inlining, not counting the allocas and ParamRefs that inlining
/// removes. The callee's own
/// calls to tiny and small functions are inlined first, as the main loop would
/// do after inlining it. Returns None when no scalar argument is a constant.
fn specialized_size(
    callee_map: &HashMap<String, CalleeData>,
    callee_name: &str,
    const_args: &[Option<ConstArg>],
) -> Option<CleanedSize> {
    let callee = &callee_map[callee_name];
    let mut func = IrFunction::new(callee_name.to_string(), callee.return_type, callee.params.clone(), false);
    func.is_static = true;
    func.next_value_id = callee.next_value_id;
    func.blocks = callee.blocks.clone();
    for block in &mut func.blocks {
        for inst in &mut block.instructions {
            if let Instruction::ParamRef { dest, param_idx, .. } = *inst {
                let is_struct = callee.param_struct_sizes.get(param_idx).copied().flatten().is_some();
                if let (false, Some(Some(arg))) = (is_struct, const_args.get(param_idx)) {
                    *inst = match arg.cast {
                        Some((from_ty, to_ty)) => Instruction::Cast {
                            dest, src: Operand::Const(arg.value), from_ty, to_ty,
                        },
                        None => Instruction::Copy { dest, src: Operand::Const(arg.value) },
                    };
                }
            }
        }
    }
    // Block IDs only need to be unique within the scratch copy.
    let mut max_block_id = callee.max_block_id;
    for _ in 0..MAX_SPECIALIZED_NESTED_INLINES {
        let sites = find_inline_call_sites(&func, callee_map, &[], false);
        let small_site = sites.into_iter().find(|site| {
            let data = &callee_map[&site.callee_name];
            let insts: usize = effective_size(&data.blocks);
            site.callee_name != callee_name
                && insts <= MAX_SMALL_INLINE_INSTRUCTIONS
                && data.blocks.len() <= MAX_SMALL_INLINE_BLOCKS
        });
        match small_site {
            Some(site) if inline_call_site(&mut func, &site, &callee_map[&site.callee_name], &mut max_block_id) => {}
            _ => break,
        }
    }
    let mut scratch = IrModule::new();
    scratch.functions.push(func);
    crate::ir::mem2reg::promote_allocas_with_params(&mut scratch);
    let func = &mut scratch.functions[0];
    for _ in 0..4 {
        let changes = super::constant_fold::fold_function(func)
            + super::copy_prop::propagate_copies(func)
            + super::gvn::run_gvn_function(func)
            + super::simplify::simplify_function(func)
            + super::cfg_simplify::run_function(func)
            + super::dce::eliminate_dead_code(func);
        if changes == 0 {
            break;
        }
    }
    if std::env::var("CCC_INLINE_DUMP_SPECIALIZED").is_ok() {
        dump_function_ir(func, "specialized for size estimate");
    }
    let insts = func.blocks.iter()
        .flat_map(|b| &b.instructions)
        .filter(|inst| !matches!(inst, Instruction::Alloca { .. } | Instruction::ParamRef { .. }))
        .count();
    let non_return_blocks = func.blocks.iter().filter(|b| !matches!(b.terminator, Terminator::Return(_))).count().max(1);
    Some(CleanedSize { insts, blocks: func.blocks.len(), non_return_blocks })
}

/// Check if a GlobalInit contains references to local labels (`.LBBxx`).
/// These are produced by `&&label` (label-as-value) in static local initializers.
fn global_init_contains_local_label(init: &GlobalInit) -> bool {
    match init {
        GlobalInit::GlobalAddr(label) => label.starts_with(".LBB"),
        GlobalInit::GlobalLabelDiff(lab1, lab2, _) => {
            lab1.starts_with(".LBB") || lab2.starts_with(".LBB")
        }
        GlobalInit::Compound(inits) => inits.iter().any(global_init_contains_local_label),
        _ => false,
    }
}

/// Check if a function has static local variables whose initializers reference
/// local labels (from `&&label`). Such functions cannot be safely inlined because
/// the label references in static data are stored as strings and are NOT remapped
/// when the function body's block IDs are remapped during inlining.
fn func_has_static_locals_with_label_refs(module: &IrModule, func_name: &str) -> bool {
    let prefix = format!("{}.", func_name);
    for global in &module.globals {
        if global.name.starts_with(&prefix)
            && global_init_contains_local_label(&global.init) {
                return true;
            }
    }
    false
}

/// Static functions the module calls exactly once and never refers to
/// otherwise: no address taken (in code, a global's initializer or inline
/// assembly), no alias, no mention in top-level assembly.
fn sole_call_statics(module: &IrModule) -> FxHashSet<&str> {
    let mut calls: HashMap<&str, usize> = HashMap::new();
    let mut other: FxHashSet<String> = FxHashSet::default();
    for inst in module.functions.iter().flat_map(|f| &f.blocks).flat_map(|b| &b.instructions) {
        match inst {
            Instruction::Call { func, .. } => *calls.entry(func.as_str()).or_insert(0) += 1,
            Instruction::GlobalAddr { name, .. } => { other.insert(name.clone()); }
            Instruction::InlineAsm { input_symbols, .. } => {
                for s in input_symbols.iter().flatten() {
                    other.insert(s.split('+').next().unwrap_or(s).to_string());
                }
            }
            _ => {}
        }
    }
    for global in &module.globals {
        global.init.for_each_ref(&mut |name: &str| { other.insert(name.to_string()); });
    }
    for (alias, target, _) in &module.aliases {
        other.insert(alias.clone());
        other.insert(target.clone());
    }
    module.functions.iter()
        .filter(|f| f.is_static && !f.is_declaration && calls.get(f.name.as_str()) == Some(&1)
            && !other.contains(&f.name) && !module.toplevel_asm.iter().any(|a| a.contains(f.name.as_str())))
        .map(|f| f.name.as_str())
        .collect()
}

/// Build a map of function name -> callee data for functions eligible for inlining.
/// The functions on a cycle of the module's call graph (calling themselves,
/// directly or through others), by Tarjan's strongly connected components.
fn recursive_functions(module: &IrModule) -> FxHashSet<&str> {
    let index: HashMap<&str, usize> = module.functions.iter().enumerate()
        .filter(|(_, f)| !f.is_declaration)
        .map(|(i, f)| (f.name.as_str(), i))
        .collect();
    let n = module.functions.len();
    let succs: Vec<Vec<usize>> = module.functions.iter().map(|f| {
        let mut s: Vec<usize> = f.blocks.iter().flat_map(|b| &b.instructions)
            .filter_map(|i| match i {
                Instruction::Call { func, .. } => index.get(func.as_str()).copied(),
                _ => None,
            })
            .collect();
        s.sort_unstable();
        s.dedup();
        s
    }).collect();

    let mut out = FxHashSet::default();
    let mut num = vec![usize::MAX; n];
    let mut low = vec![0usize; n];
    let mut on_stack = vec![false; n];
    let mut stack: Vec<usize> = Vec::new();
    let mut next = 0;
    for root in 0..n {
        if num[root] != usize::MAX || module.functions[root].is_declaration {
            continue;
        }
        // (node, index of the next successor to visit)
        let mut work = vec![(root, 0usize)];
        num[root] = next;
        low[root] = next;
        next += 1;
        stack.push(root);
        on_stack[root] = true;
        while let Some(&mut (v, ref mut k)) = work.last_mut() {
            if let Some(&w) = succs[v].get(*k) {
                *k += 1;
                if num[w] == usize::MAX {
                    num[w] = next;
                    low[w] = next;
                    next += 1;
                    stack.push(w);
                    on_stack[w] = true;
                    work.push((w, 0));
                } else if on_stack[w] {
                    low[v] = low[v].min(num[w]);
                }
                continue;
            }
            work.pop();
            if let Some(&(parent, _)) = work.last() {
                low[parent] = low[parent].min(low[v]);
            }
            if low[v] == num[v] {
                let mut scc = Vec::new();
                while let Some(w) = stack.pop() {
                    on_stack[w] = false;
                    scc.push(w);
                    if w == v {
                        break;
                    }
                }
                if scc.len() > 1 || succs[v].contains(&v) {
                    out.extend(scc.iter().map(|&w| module.functions[w].name.as_str()));
                }
            }
        }
    }
    out
}

fn build_callee_map(module: &IrModule) -> HashMap<String, CalleeData> {
    build_callee_map_for(module, false)
}

/// The functions that will hold a large always_inline body once it is expanded,
/// by calling it directly or through smaller always_inline functions. They look
/// small until then, and copied into their callers each brings a copy of the
/// body: lz4's one-line wrappers took its 28,000-line compressor into a dozen
/// entry points. An ordinary function among these stays a function, as clang's
/// bottom-up order leaves it too, and its callers just call it.
fn carrying_large_always_inline(module: &IrModule) -> FxHashSet<&str> {
    if crate::common::types::building_kernel() {
        return FxHashSet::default();
    }
    let calls_one_of = |f: &IrFunction, names: &FxHashSet<&str>| f.blocks.iter().flat_map(|b| &b.instructions)
        .any(|i| matches!(i, Instruction::Call { func, .. } if names.contains(func.as_str())));
    let forced: Vec<&IrFunction> = module.functions.iter()
        .filter(|f| f.is_always_inline && !f.is_noinline && !f.is_declaration)
        .collect();
    let mut heavy: FxHashSet<&str> = forced.iter()
        .filter(|f| effective_size(&f.blocks) > MAX_ALWAYS_INLINE_INSTRUCTIONS || f.blocks.len() > MAX_ALWAYS_INLINE_BLOCKS)
        .map(|f| f.name.as_str())
        .collect();
    if heavy.is_empty() {
        return heavy;
    }
    loop {
        let more: Vec<&str> = forced.iter()
            .filter(|f| !heavy.contains(f.name.as_str()) && calls_one_of(f, &heavy))
            .map(|f| f.name.as_str())
            .collect();
        if more.is_empty() {
            break;
        }
        heavy.extend(more);
    }
    module.functions.iter()
        .filter(|f| !f.is_declaration && calls_one_of(f, &heavy))
        .map(|f| f.name.as_str())
        .collect()
}

/// With `large`, only the always_inline functions the ordinary map leaves out
/// for their size, and none on a cycle, which would never finish.
fn build_callee_map_for(module: &IrModule, large: bool) -> HashMap<String, CalleeData> {
    let mut map = HashMap::new();
    let recursive = recursive_functions(module);
    let sole = sole_call_statics(module);
    let carrying = if large { FxHashSet::default() } else { carrying_large_always_inline(module) };

    let debug_callee = std::env::var("CCC_INLINE_DEBUG").is_ok();
    for func in &module.functions {
        if func.is_declaration {
            continue;
        }
        // __attribute__((noinline)) takes precedence: never inline these functions
        if func.is_noinline {
            continue;
        }
        if !func.is_always_inline && carrying.contains(func.name.as_str()) {
            continue;
        }
        // A recursive function inlined into a caller brings its own call
        // along, which is inlined in turn, up to the round limit: SQLite's
        // 22-instruction sqlite3BitvecDestroy became 5,000 instructions in
        // each of its callers. (always_inline is left to its own limits.)
        // So such a function is inlined only into callers on no cycle, and
        // the calls its body brings stay calls (SQLite's sqlite3_value_text,
        // on a cycle through sqlite3VdbeMemStringify, is a call per row from
        // its SQL functions).
        let in_cycle = !func.is_always_inline && recursive.contains(func.name.as_str());

        // Determine if this is an always_inline function
        let is_always_inline = func.is_always_inline;

        // For always_inline: we inline regardless of whether the function is static,
        // because GCC/Clang semantics dictate that __attribute__((always_inline))
        // means the function must always be inlined at call sites.
        // For normal inlining: only inline static inline functions (internal linkage),
        // OR static functions that are trivially empty (void return, no instructions).
        // Empty static stubs must be inlined so that references to their arguments
        // (which may be undefined symbols) are eliminated by DCE. This matches GCC
        // behavior where empty stub functions like `static void __apply_fineibt(...){}`
        // are inlined away, removing references to symbols like __cfi_sites.
        // A function is "trivially empty" if it is a static void function
        // with a single block whose only instructions are parameter allocas
        // (which are always generated by the lowering even for empty bodies)
        // and terminates with Return(None).
        let is_trivially_empty = func.is_static
            && func.return_type == IrType::Void
            && func.blocks.len() == 1
            && matches!(func.blocks[0].terminator, Terminator::Return(None))
            && func.blocks[0].instructions.iter().all(|inst| {
                matches!(inst, Instruction::Alloca { .. })
            });
        // Check if this is a small static (non-inline) function eligible for inlining.
        // GCC at -O2 inlines small static functions even without the `inline` keyword.
        // This prevents linker errors from undefined references in dead code paths.
        let inst_count_for_static: usize = effective_size(&func.blocks);
        let is_small_static = func.is_static && !func.is_inline
            && inst_count_for_static <= MAX_STATIC_NONINLINE_INSTRUCTIONS
            && func.blocks.len() <= MAX_STATIC_NONINLINE_BLOCKS;
        // Also consider medium-sized static non-inline functions for inlining.
        // GCC at -O2/-Os inlines static functions up to a generous limit even without
        // the `inline` keyword. This is critical for avoiding section mismatch errors
        // in the kernel: e.g., ssb_select_mitigation (~6 blocks, ~21 IR instructions)
        // is called from cpu_select_mitigations (.init.text) and calls
        // __ssb_select_mitigation (.init.text). Without inlining the wrapper,
        // modpost flags a .text -> .init.text section mismatch.
        // These are treated as normal inline candidates (not exceeds_normal_limits)
        // since they fit within MAX_INLINE_INSTRUCTIONS/MAX_INLINE_BLOCKS.
        let is_medium_static = func.is_static && !func.is_inline
            && !is_small_static
            && inst_count_for_static <= MAX_INLINE_INSTRUCTIONS
            && func.blocks.len() <= MAX_INLINE_BLOCKS;
        // Larger static functions (often small loops, whose blocks inflate the
        // raw count) are considered only by their cleaned-up size, as are
        // functions with external linkage: a call in this file may use the
        // definition it sees (as clang does, the function keeps its own
        // copy), unless it is weak and the linker may pick another, or naked
        // or placed in a section of its own. SQLite's sqlite3_free and
        // sqlite3_value_type are API functions its own code calls millions
        // of times.
        let linkable = func.is_static
            || (!func.is_weak && !func.is_naked && func.section.is_none());
        // A static function's only call may take a larger one (see
        // MAX_SOLE_CALL_INLINE_INSTRUCTIONS), judged by its cleaned size too:
        // sqlite3VarintValue, 135 instructions before cleanup, in
        // sqlite3BtreeTableMoveto's binary search.
        let (raw_insts, raw_blocks) = if sole.contains(func.name.as_str()) {
            (2 * MAX_SOLE_CALL_INLINE_INSTRUCTIONS, 2 * MAX_SOLE_CALL_INLINE_BLOCKS)
        } else {
            (2 * MAX_INLINE_INSTRUCTIONS, MAX_CLEANED_ONLY_RAW_BLOCKS)
        };
        let cleaned_only = linkable && !func.is_inline && !is_always_inline
            && !is_trivially_empty && !is_small_static && !is_medium_static
            && inst_count_for_static <= raw_insts && func.blocks.len() <= raw_blocks;
        if !is_always_inline && !is_trivially_empty && !is_small_static && !is_medium_static && !cleaned_only
            && (!func.is_static || !func.is_inline) {
                if debug_callee {
                    eprintln!("[INLINE_DEBUG] {} skipped: is_static={}, is_inline={}, is_declaration={}, blocks={}, inst_count={}",
                        func.name, func.is_static, func.is_inline, func.is_declaration, func.blocks.len(), inst_count_for_static);
                }
                continue;
            }
        if debug_callee {
            let ic: usize = effective_size(&func.blocks);
            eprintln!("[INLINE_DEBUG] {} candidate: blocks={}, inst_count={}, is_variadic={}, params={}",
                func.name, func.blocks.len(), ic, func.is_variadic, func.params.len());
        }
        // Don't inline variadic functions (complex ABI)
        if func.is_variadic {
            continue;
        }

        // Check size limits.
        // For always_inline: use generous limits.
        // For static inline: use normal limits, but also admit functions that exceed
        // normal limits but fit within always_inline limits. These "exceeds_normal_limits"
        // callees will only be inlined when the caller has a custom section attribute
        // (e.g., .head.text, .noinstr.text), where cross-section calls are dangerous.
        let inst_count: usize = effective_size(&func.blocks);
        let fits_normal = inst_count <= MAX_INLINE_INSTRUCTIONS && func.blocks.len() <= MAX_INLINE_BLOCKS;
        let fits_relaxed = inst_count <= MAX_ALWAYS_INLINE_INSTRUCTIONS && func.blocks.len() <= MAX_ALWAYS_INLINE_BLOCKS;
        let exceeds_normal = !is_always_inline && !fits_normal && !cleaned_only;
        if large && (!is_always_inline || fits_relaxed || recursive.contains(func.name.as_str())) {
            continue;
        }
        if is_always_inline {
            if !fits_relaxed && !large {
                continue;
            }
        } else {
            // For static inline: admit if within normal limits OR within relaxed limits
            // (the latter only used for section-attributed callers).
            if !fits_normal && !fits_relaxed {
                continue;
            }
        }

        // Skip functions containing constructs that are hard to inline correctly.
        // Inline asm is allowed: the inliner handles it correctly and the
        // resolve_inline_asm_symbols post-pass resolves operand symbols.
        // This is important because many kernel static inline functions use
        // inline asm (cr reads/writes, atomic ops, barriers, RIP_REL_REF, etc.)
        // and must be inlinable to avoid cross-section call issues.
        let mut has_problematic = false;
        for block in &func.blocks {
            for inst in &block.instructions {
                match inst {
                    // Inline asm is allowed for all inlined functions
                    Instruction::InlineAsm { .. } => {}
                    Instruction::VaStart { .. }
                    | Instruction::VaEnd { .. }
                    | Instruction::VaArg { .. }
                    | Instruction::VaArgStruct { .. }
                    | Instruction::VaCopy { .. }
                    | Instruction::DynAlloca { .. }
                    | Instruction::StackSave { .. }
                    | Instruction::StackRestore { .. } => {
                        has_problematic = true;
                        break;
                    }
                    _ => {}
                }
            }
            if has_problematic {
                break;
            }
            if matches!(block.terminator, Terminator::IndirectBranch { .. }) {
                has_problematic = true;
                break;
            }
        }
        if has_problematic {
            continue;
        }

        // Don't inline functions whose static local variables contain label
        // address references (&&label). The label references in static data are
        // stored as assembly label strings (e.g., ".L3") and are NOT remapped
        // when block IDs are remapped during inlining. This causes dangling
        // references to non-existent labels, resulting in linker errors.
        if func_has_static_locals_with_label_refs(module, &func.name) {
            continue;
        }

        // Clone the function's blocks for use during inlining
        let max_block_id = func.blocks.iter()
            .map(|b| b.label.0)
            .max()
            .unwrap_or(0);

        let param_struct_sizes: Vec<Option<usize>> = func.params.iter().map(|p| p.struct_size).collect();

        map.insert(func.name.clone(), CalleeData {
            blocks: func.blocks.clone(),
            param_struct_sizes,
            return_type: func.return_type,
            num_params: func.params.len(),
            next_value_id: func.next_value_id,
            max_block_id,
            is_always_inline,
            exceeds_normal_limits: exceeds_normal,
            is_static_inline: func.is_static && func.is_inline,
            cleaned_only,
            wide: cleaned_only && func.blocks.len() > 2 * MAX_INLINE_BLOCKS,
            vector: func.blocks.iter().flat_map(|b| &b.instructions)
                .any(|i| matches!(i, Instruction::Intrinsic { op: crate::ir::reexports::IntrinsicOp::VecOp { .. }, .. })),
            calls: func.blocks.iter().flat_map(|b| &b.instructions)
                .any(|i| matches!(i, Instruction::Call { .. } | Instruction::CallIndirect { .. } | Instruction::Memcpy { .. })),
            sole_call: sole.contains(func.name.as_str()),
            in_cycle,
            params: func.params.clone(),
        });
    }

    map
}

/// Find call sites in a function that are eligible for inlining.
/// `caller_has_section`: true if the caller has a custom section attribute.
/// When true, callees that exceed normal inline limits (but fit relaxed limits)
/// are also eligible, since cross-section calls from section-attributed functions
/// (e.g., .head.text, .noinstr.text) can cause boot/runtime failures.
fn find_inline_call_sites(
    func: &IrFunction,
    callee_map: &HashMap<String, CalleeData>,
    skip_list: &[String],
    caller_has_section: bool,
) -> Vec<InlineCallSite> {
    let mut sites = Vec::new();

    for (block_idx, block) in func.blocks.iter().enumerate() {
        for (inst_idx, inst) in block.instructions.iter().enumerate() {
            if let Instruction::Call { func: callee_name, info } = inst {
                if let Some(callee_data) = callee_map.get(callee_name) {
                    // Don't inline recursive calls
                    if callee_name != &func.name {
                        // Skip functions listed in CCC_INLINE_SKIP
                        if skip_list.iter().any(|s| s == callee_name) {
                            continue;
                        }
                        // Skip callees that exceed normal limits unless caller has a section
                        if callee_data.exceeds_normal_limits && !caller_has_section {
                            continue;
                        }
                        sites.push(InlineCallSite {
                            block_idx,
                            inst_idx,
                            callee_name: callee_name.clone(),
                            dest: info.dest,
                            args: info.args.clone(),
                            const_args: Vec::new(),
                            in_cleaned_body: false,
                            in_loop: false,
                        });
                    }
                }
            }
        }
    }

    if !sites.is_empty() {
        let labels = crate::ir::analysis::build_label_map(func);
        let (_, succs) = crate::ir::analysis::build_cfg(func, &labels);
        let looped = blocks_on_cycles(&succs, func.blocks.len());
        for site in &mut sites {
            site.in_loop = looped[site.block_idx];
        }
    }
    if !sites.is_empty() {
        let consts = constant_values(func);
        for site in &mut sites {
            site.const_args = site.args.iter().map(|arg| match arg {
                Operand::Const(c) => Some(ConstArg { value: *c, cast: None }),
                Operand::Value(v) => consts.get(&v.0).copied(),
            }).collect();
        }
    }

    sites
}

/// Inline a single call site. Returns true if successful.
/// `global_max_block_id` is the module-global max block ID, updated on success.
fn inline_call_site(
    caller: &mut IrFunction,
    site: &InlineCallSite,
    callee: &CalleeData,
    global_max_block_id: &mut u32,
) -> bool {
    if callee.blocks.is_empty() {
        return false;
    }

    // Compute ID offsets for remapping callee values and blocks into caller's namespace
    let caller_next_value = if caller.next_value_id > 0 {
        caller.next_value_id
    } else {
        caller.max_value_id() + 1
    };

    // Use the global max block ID to avoid collisions with ANY function's blocks
    let value_offset = caller_next_value;
    let block_offset = *global_max_block_id + 1;

    let debug_inline_detail = std::env::var("CCC_INLINE_DEBUG_DETAIL").is_ok();
    if debug_inline_detail {
        eprintln!("[INLINE_DETAIL] Inlining '{}' into '{}': value_offset={}, block_offset={}, callee.next_value_id={}, caller.next_value_id={}",
            site.callee_name, caller.name, value_offset, block_offset, callee.next_value_id, caller.next_value_id);
        eprintln!("[INLINE_DETAIL]   site.block_idx={}, site.inst_idx={}", site.block_idx, site.inst_idx);
        for (i, arg) in site.args.iter().enumerate() {
            eprintln!("[INLINE_DETAIL]   arg[{}] = {:?}", i, arg);
        }
    }

    // Clone and remap the callee's blocks
    let mut inlined_blocks: Vec<BasicBlock> = Vec::with_capacity(callee.blocks.len());
    let offsets = Remap { value: &|v| Value(v.0 + value_offset), block: &|b| BlockId(b.0 + block_offset) };

    for callee_block in &callee.blocks {
        let mut new_block = BasicBlock {
            label: BlockId(callee_block.label.0 + block_offset),
            instructions: Vec::with_capacity(callee_block.instructions.len()),
            source_spans: callee_block.source_spans.clone(),
            terminator: remap_terminator(&callee_block.terminator, &offsets),
        };

        for inst in &callee_block.instructions {
            new_block.instructions.push(remap_instruction(inst, &offsets));
        }

        inlined_blocks.push(new_block);
    }

    // Create a merge block that the callee's return statements will branch to
    let merge_block_id = BlockId(block_offset + callee.max_block_id + 1);

    // Collect return values from all return blocks to build a Phi node.
    // Each Return(Some(val)) becomes a branch to the merge block, and
    // the return value feeds into a Phi in the merge block.
    let mut phi_incoming: Vec<(Operand, BlockId)> = Vec::new();

    // Replace Return terminators in inlined blocks
    for block in &mut inlined_blocks {
        if let Terminator::Return(ret_val) = &block.terminator {
            if let (Some(_call_dest), Some(ret_operand)) = (site.dest, ret_val) {
                phi_incoming.push((*ret_operand, block.label));
            }
            block.terminator = Terminator::Branch(merge_block_id);
        }
    }

    // Now we need to wire up the arguments. The callee's first N allocas are parameter allocas.
    // We need to store the caller's arguments into those allocas.
    // The param allocas are the first N Alloca instructions in the callee's entry block.
    let entry_block = &mut inlined_blocks[0];
    let mut param_alloca_info: Vec<(Value, IrType, usize)> = Vec::new(); // (dest, ty, size)
    for inst in &entry_block.instructions {
        if let Instruction::Alloca { dest, ty, size, .. } = inst {
            param_alloca_info.push((*dest, *ty, *size));
            if param_alloca_info.len() >= callee.num_params {
                break;
            }
        }
    }

    // Insert stores/memcpys of arguments into param allocas at the beginning of the
    // entry block (after the allocas themselves)
    let mut insert_pos = 0;
    // Find position after all allocas in the entry block
    for (i, inst) in entry_block.instructions.iter().enumerate() {
        if matches!(inst, Instruction::Alloca { .. }) {
            insert_pos = i + 1;
        } else {
            break;
        }
    }

    // Insert stores in reverse order so indices stay valid
    let has_spans = !entry_block.source_spans.is_empty();
    let num_args_to_store = std::cmp::min(site.args.len(), param_alloca_info.len());
    for i in (0..num_args_to_store).rev() {
        let param_struct_size = callee.param_struct_sizes.get(i).copied().flatten();
        if let Some(struct_size) = param_struct_size {
            // Struct-by-value parameter: the caller passes a pointer to the struct data.
            // We must copy the struct data from that pointer into the callee's param alloca.
            if let Operand::Value(src_ptr) = site.args[i] {
                entry_block.instructions.insert(insert_pos, Instruction::Memcpy {
                    dest: param_alloca_info[i].0,
                    src: src_ptr,
                    size: struct_size,
                });
                if has_spans {
                    entry_block.source_spans.insert(insert_pos, crate::common::source::Span::dummy());
                }
            } else {
                // Struct arg should always be a Value (pointer), not a Const.
                // If somehow it's a Const, bail out of inlining.
                return false;
            }
        } else {
            // Scalar parameter: store the value directly into the param alloca.
            let store_ty = param_alloca_info[i].1;
            entry_block.instructions.insert(insert_pos, Instruction::Store {
                val: site.args[i],
                ptr: param_alloca_info[i].0,
                ty: store_ty,
                seg_override: AddressSpace::Default,
                volatile: false, alias: AliasClass::ANY
            });
            if has_spans {
                entry_block.source_spans.insert(insert_pos, crate::common::source::Span::dummy());
            }
        }
    }

    // Remove ParamRef instructions from inlined blocks. After inlining, ParamRef
    // instructions are invalid because they reference param_idx of the callee,
    // but at codegen time they would be interpreted as param_idx of the caller.
    // The inliner already handles argument passing via stores above, so ParamRef
    // instructions (and their associated stores to param allocas) are redundant.
    // We also remove the Store that immediately follows each ParamRef since it
    // stores the (now-removed) ParamRef dest into a param alloca.
    let param_alloca_set: std::collections::HashSet<u32> = param_alloca_info.iter().map(|(v, _, _)| v.0).collect();
    for block in &mut inlined_blocks {
        let has_spans = block.source_spans.len() == block.instructions.len() && !block.source_spans.is_empty();
        let old_spans = std::mem::take(&mut block.source_spans);
        let mut new_insts = Vec::with_capacity(block.instructions.len());
        let mut new_spans = Vec::new();
        let mut paramref_dests: std::collections::HashSet<u32> = std::collections::HashSet::new();
        for (idx, inst) in block.instructions.drain(..).enumerate() {
            if let Instruction::ParamRef { dest, .. } = &inst {
                paramref_dests.insert(dest.0);
                // Don't emit this instruction; mark that the next Store to a param alloca should be skipped
                continue;
            }
            // Skip stores of ParamRef dests to param allocas
            if let Instruction::Store { val: Operand::Value(v), ptr, .. } = &inst {
                if paramref_dests.contains(&v.0) && param_alloca_set.contains(&ptr.0) {
                    continue;
                }
            }
            new_insts.push(inst);
            if has_spans {
                new_spans.push(old_spans[idx]);
            }
        }
        block.instructions = new_insts;
        if has_spans {
            block.source_spans = new_spans;
        }
    }
    // Now split the caller's block at the call site:
    // Block before call -> instructions before the call + branch to callee entry
    // Block after call (merge block) -> instructions after the call + original terminator

    let call_block_idx = site.block_idx;
    let call_inst_idx = site.inst_idx;

    // Save instructions after the call and the terminator
    let after_call_instructions: Vec<Instruction> = caller.blocks[call_block_idx]
        .instructions
        .split_off(call_inst_idx + 1);
    let after_call_spans: Vec<crate::common::source::Span> = {
        let spans = &mut caller.blocks[call_block_idx].source_spans;
        if spans.len() > call_inst_idx + 1 {
            spans.split_off(call_inst_idx + 1)
        } else {
            Vec::new()
        }
    };
    let original_terminator = std::mem::replace(
        &mut caller.blocks[call_block_idx].terminator,
        Terminator::Branch(inlined_blocks[0].label),
    );

    // Remove the call instruction itself
    caller.blocks[call_block_idx].instructions.pop();
    if !caller.blocks[call_block_idx].source_spans.is_empty() {
        caller.blocks[call_block_idx].source_spans.pop();
    }

    // Create the merge block with the remaining instructions and original terminator.
    // If the callee had a non-void return, insert a Phi (or Copy for single-predecessor)
    // at the start of the merge block to define the call's result value.
    let mut merge_instructions = Vec::new();
    let mut merge_spans: Vec<crate::common::source::Span> = Vec::new();
    if let Some(call_dest) = site.dest {
        if phi_incoming.len() == 1 {
            // Single return path: just copy the value directly (no phi needed)
            merge_instructions.push(Instruction::Copy {
                dest: call_dest,
                src: phi_incoming[0].0,
            });
            merge_spans.push(crate::common::source::Span::dummy());
        } else if phi_incoming.len() > 1 {
            // Multiple return paths: need a Phi node
            merge_instructions.push(Instruction::Phi {
                dest: call_dest,
                ty: callee.return_type,
                incoming: phi_incoming,
            });
            merge_spans.push(crate::common::source::Span::dummy());
        }
        // If phi_incoming is empty, the callee never returns a value (e.g., all paths
        // are noreturn/unreachable). The call_dest will be undefined, which is fine
        // since it won't be used.
    }
    merge_instructions.extend(after_call_instructions);
    merge_spans.extend(after_call_spans);

    let merge_block = BasicBlock {
        label: merge_block_id,
        instructions: merge_instructions,
        source_spans: merge_spans,
        terminator: original_terminator,
    };

    // Insert the inlined blocks and merge block after the call block
    let insert_position = call_block_idx + 1;
    // Insert merge block first, then inlined blocks before it
    caller.blocks.insert(insert_position, merge_block);
    for (i, block) in inlined_blocks.into_iter().enumerate() {
        caller.blocks.insert(insert_position + i, block);
    }

    // Update Phi nodes: the original caller block was split at the call site.
    // The merge block inherited the original block's terminator (and thus its
    // successors). Any Phi node in a successor block that references the original
    // split block as an incoming predecessor must now reference the merge block
    // instead, since control flow from the split block now goes through the
    // inlined code and arrives at the successor via the merge block.
    let split_block_label = caller.blocks[call_block_idx].label;
    for block in &mut caller.blocks {
        for inst in &mut block.instructions {
            if let Instruction::Phi { incoming, .. } = inst {
                for (_operand, block_id) in incoming.iter_mut() {
                    if *block_id == split_block_label {
                        *block_id = merge_block_id;
                    }
                }
            }
        }
    }

    // Update caller's next_value_id to account for the new values
    let new_next_value_id = value_offset + callee.next_value_id;
    if caller.next_value_id > 0 || new_next_value_id > caller.next_value_id {
        caller.next_value_id = std::cmp::max(
            new_next_value_id,
            caller.next_value_id,
        );
    }
    if debug_inline_detail {
        eprintln!("[INLINE_DETAIL]   after inline: caller.next_value_id={}", caller.next_value_id);
    }

    // Update the global max block ID so subsequent inlines use fresh IDs.
    // The merge block has the highest ID we assigned.
    *global_max_block_id = merge_block_id.0;

    true
}

/// How remap_instruction and remap_terminator rename values and blocks:
/// by an offset (the inliner's copies of a callee) or any other way.
pub(crate) struct Remap<'a> {
    pub value: &'a dyn Fn(Value) -> Value,
    pub block: &'a dyn Fn(BlockId) -> BlockId,
}

fn remap_value(v: Value, r: &Remap) -> Value {
    (r.value)(v)
}

fn remap_block(b: BlockId, r: &Remap) -> BlockId {
    (r.block)(b)
}

/// Remap an Operand (only Value operands need remapping; constants stay the same).
fn remap_operand(op: &Operand, r: &Remap) -> Operand {
    match op {
        Operand::Value(v) => Operand::Value(remap_value(*v, r)),
        Operand::Const(c) => Operand::Const(*c),
    }
}

/// Remap all values in a CallInfo (shared between Call and CallIndirect remapping).
fn remap_call_info(info: &CallInfo, vo: &Remap) -> CallInfo {
    CallInfo {
        dest: info.dest.map(|v| remap_value(v, vo)),
        args: info.args.iter().map(|a| remap_operand(a, vo)).collect(),
        arg_types: info.arg_types.clone(),
        return_type: info.return_type,
        is_variadic: info.is_variadic,
        num_fixed_args: info.num_fixed_args,
        struct_arg_sizes: info.struct_arg_sizes.clone(),
        struct_arg_aligns: info.struct_arg_aligns.clone(),
        struct_arg_classes: info.struct_arg_classes.clone(),
        struct_arg_riscv_float_classes: info.struct_arg_riscv_float_classes.clone(),
        is_sret: info.is_sret,
        is_fastcall: info.is_fastcall,
        ret_eightbyte_classes: info.ret_eightbyte_classes.clone(),
    }
}

/// Remap all values and block references in an instruction.
pub(crate) fn remap_instruction(inst: &Instruction, vo: &Remap) -> Instruction {
    let bo = vo;
    match inst {
        Instruction::Alloca { dest, ty, size, align, volatile } => Instruction::Alloca {
            dest: remap_value(*dest, vo),
            ty: *ty,
            size: *size,
            align: *align,
            volatile: *volatile,
        },
        Instruction::DynAlloca { dest, size, align } => Instruction::DynAlloca {
            dest: remap_value(*dest, vo),
            size: remap_operand(size, vo),
            align: *align,
        },
        Instruction::Store { val, ptr, ty, seg_override, volatile, alias } => Instruction::Store {
            val: remap_operand(val, vo),
            ptr: remap_value(*ptr, vo),
            ty: *ty,
            seg_override: *seg_override,
            volatile: *volatile,
            alias: *alias,
        },
        Instruction::Load { dest, ptr, ty, seg_override, volatile, alias } => Instruction::Load {
            dest: remap_value(*dest, vo),
            ptr: remap_value(*ptr, vo),
            ty: *ty,
            seg_override: *seg_override,
            volatile: *volatile,
            alias: *alias,
        },
        Instruction::BinOp { dest, op, lhs, rhs, ty } => Instruction::BinOp {
            dest: remap_value(*dest, vo),
            op: *op,
            lhs: remap_operand(lhs, vo),
            rhs: remap_operand(rhs, vo),
            ty: *ty,
        },
        Instruction::UnaryOp { dest, op, src, ty } => Instruction::UnaryOp {
            dest: remap_value(*dest, vo),
            op: *op,
            src: remap_operand(src, vo),
            ty: *ty,
        },
        Instruction::Cmp { dest, op, lhs, rhs, ty } => Instruction::Cmp {
            dest: remap_value(*dest, vo),
            op: *op,
            lhs: remap_operand(lhs, vo),
            rhs: remap_operand(rhs, vo),
            ty: *ty,
        },
        Instruction::Call { func, info } => Instruction::Call {
            func: func.clone(),
            info: remap_call_info(info, vo),
        },
        Instruction::CallIndirect { func_ptr, info } => Instruction::CallIndirect {
            func_ptr: remap_operand(func_ptr, vo),
            info: remap_call_info(info, vo),
        },
        Instruction::GetElementPtr { dest, base, offset, ty } => Instruction::GetElementPtr {
            dest: remap_value(*dest, vo),
            base: remap_value(*base, vo),
            offset: remap_operand(offset, vo),
            ty: *ty,
        },
        Instruction::Cast { dest, src, from_ty, to_ty } => Instruction::Cast {
            dest: remap_value(*dest, vo),
            src: remap_operand(src, vo),
            from_ty: *from_ty,
            to_ty: *to_ty,
        },
        Instruction::Copy { dest, src } => Instruction::Copy {
            dest: remap_value(*dest, vo),
            src: remap_operand(src, vo),
        },
        Instruction::GlobalAddr { dest, name } => Instruction::GlobalAddr {
            dest: remap_value(*dest, vo),
            name: name.clone(),
        },
        Instruction::Memcpy { dest, src, size } => Instruction::Memcpy {
            dest: remap_value(*dest, vo),
            src: remap_value(*src, vo),
            size: *size,
        },
        Instruction::VaArg { dest, va_list_ptr, result_ty } => Instruction::VaArg {
            dest: remap_value(*dest, vo),
            va_list_ptr: remap_value(*va_list_ptr, vo),
            result_ty: *result_ty,
        },
        Instruction::VaStart { va_list_ptr } => Instruction::VaStart {
            va_list_ptr: remap_value(*va_list_ptr, vo),
        },
        Instruction::VaEnd { va_list_ptr } => Instruction::VaEnd {
            va_list_ptr: remap_value(*va_list_ptr, vo),
        },
        Instruction::VaCopy { dest_ptr, src_ptr } => Instruction::VaCopy {
            dest_ptr: remap_value(*dest_ptr, vo),
            src_ptr: remap_value(*src_ptr, vo),
        },
        Instruction::VaArgStruct { dest_ptr, va_list_ptr, size, ref eightbyte_classes } => Instruction::VaArgStruct {
            dest_ptr: remap_value(*dest_ptr, vo),
            va_list_ptr: remap_value(*va_list_ptr, vo),
            size: *size,
            eightbyte_classes: eightbyte_classes.clone(),
        },
        Instruction::AtomicRmw { dest, op, ptr, val, ty, ordering } => Instruction::AtomicRmw {
            dest: remap_value(*dest, vo),
            op: *op,
            ptr: remap_operand(ptr, vo),
            val: remap_operand(val, vo),
            ty: *ty,
            ordering: *ordering,
        },
        Instruction::AtomicCmpxchg { dest, ptr, expected, desired, ty, success_ordering, failure_ordering, returns_bool } => Instruction::AtomicCmpxchg {
            dest: remap_value(*dest, vo),
            ptr: remap_operand(ptr, vo),
            expected: remap_operand(expected, vo),
            desired: remap_operand(desired, vo),
            ty: *ty,
            success_ordering: *success_ordering,
            failure_ordering: *failure_ordering,
            returns_bool: *returns_bool,
        },
        Instruction::AtomicLoad { dest, ptr, ty, ordering } => Instruction::AtomicLoad {
            dest: remap_value(*dest, vo),
            ptr: remap_operand(ptr, vo),
            ty: *ty,
            ordering: *ordering,
        },
        Instruction::AtomicStore { ptr, val, ty, ordering } => Instruction::AtomicStore {
            ptr: remap_operand(ptr, vo),
            val: remap_operand(val, vo),
            ty: *ty,
            ordering: *ordering,
        },
        Instruction::Fence { ordering } => Instruction::Fence {
            ordering: *ordering,
        },
        Instruction::Phi { dest, ty, incoming } => Instruction::Phi {
            dest: remap_value(*dest, vo),
            ty: *ty,
            incoming: incoming.iter().map(|(op, bid)| {
                (remap_operand(op, vo), remap_block(*bid, bo))
            }).collect(),
        },
        Instruction::LabelAddr { dest, label } => Instruction::LabelAddr {
            dest: remap_value(*dest, vo),
            label: remap_block(*label, bo),
        },
        Instruction::GetReturnF64Second { dest } => Instruction::GetReturnF64Second {
            dest: remap_value(*dest, vo),
        },
        Instruction::SetReturnF64Second { src } => Instruction::SetReturnF64Second {
            src: remap_operand(src, vo),
        },
        Instruction::GetReturnF32Second { dest } => Instruction::GetReturnF32Second {
            dest: remap_value(*dest, vo),
        },
        Instruction::SetReturnF32Second { src } => Instruction::SetReturnF32Second {
            src: remap_operand(src, vo),
        },
        Instruction::GetReturnF128Second { dest } => Instruction::GetReturnF128Second {
            dest: remap_value(*dest, vo),
        },
        Instruction::SetReturnF128Second { src } => Instruction::SetReturnF128Second {
            src: remap_operand(src, vo),
        },
        Instruction::InlineAsm { template, outputs, inputs, clobbers, operand_types, goto_labels, input_symbols, seg_overrides } => Instruction::InlineAsm {
            template: template.clone(),
            outputs: outputs.iter().map(|(c, v, n)| (c.clone(), remap_value(*v, vo), n.clone())).collect(),
            inputs: inputs.iter().map(|(c, op, n)| (c.clone(), remap_operand(op, vo), n.clone())).collect(),
            clobbers: clobbers.clone(),
            operand_types: operand_types.clone(),
            goto_labels: goto_labels.iter().map(|(name, bid)| (name.clone(), remap_block(*bid, bo))).collect(),
            input_symbols: input_symbols.clone(),
            seg_overrides: seg_overrides.clone(),
        },
        Instruction::Intrinsic { dest, op, dest_ptr, args } => Instruction::Intrinsic {
            dest: dest.map(|v| remap_value(v, vo)),
            op: *op,
            dest_ptr: dest_ptr.map(|v| remap_value(v, vo)),
            args: args.iter().map(|a| remap_operand(a, vo)).collect(),
        },
        Instruction::Select { dest, cond, true_val, false_val, ty } => Instruction::Select {
            dest: remap_value(*dest, vo),
            cond: remap_operand(cond, vo),
            true_val: remap_operand(true_val, vo),
            false_val: remap_operand(false_val, vo),
            ty: *ty,
        },
        Instruction::StackSave { dest } => Instruction::StackSave {
            dest: remap_value(*dest, vo),
        },
        Instruction::StackRestore { ptr } => Instruction::StackRestore {
            ptr: remap_value(*ptr, vo),
        },
        Instruction::ParamRef { dest, param_idx, ty } => Instruction::ParamRef {
            dest: remap_value(*dest, vo),
            param_idx: *param_idx,
            ty: *ty,
        },
    }
}

/// Remap block references in a terminator.
pub(crate) fn remap_terminator(term: &Terminator, vo: &Remap) -> Terminator {
    let bo = vo;
    match term {
        Terminator::Return(op) => Terminator::Return(op.map(|o| remap_operand(&o, vo))),
        Terminator::Branch(bid) => Terminator::Branch(remap_block(*bid, bo)),
        Terminator::CondBranch { cond, true_label, false_label } => Terminator::CondBranch {
            cond: remap_operand(cond, vo),
            true_label: remap_block(*true_label, bo),
            false_label: remap_block(*false_label, bo),
        },
        Terminator::IndirectBranch { target, possible_targets } => Terminator::IndirectBranch {
            target: remap_operand(target, vo),
            possible_targets: possible_targets.iter().map(|b| remap_block(*b, bo)).collect(),
        },
        Terminator::Switch { val, cases, default, ty } => Terminator::Switch {
            val: remap_operand(val, vo),
            cases: cases.iter().map(|&(v, bid)| (v, remap_block(bid, bo))).collect(),
            default: remap_block(*default, bo),
            ty: *ty,
        },
        Terminator::Unreachable => Terminator::Unreachable,
    }
}

/// Debug: dump function IR in a readable text format.
pub(crate) fn dump_function_ir(func: &IrFunction, context: &str) {
    eprintln!("=== IR DUMP {} ===", context);
    eprintln!("function {} (next_value_id={})", func.name, func.next_value_id);
    for (bi, block) in func.blocks.iter().enumerate() {
        eprintln!("  block[{}] .L{}:", bi, block.label.0);
        for (ii, inst) in block.instructions.iter().enumerate() {
            eprintln!("    [{}] {}", ii, format_instruction(inst));
        }
        eprintln!("    terminator: {}", format_terminator(&block.terminator));
    }
    eprintln!("=== END IR DUMP ===");
}

fn format_operand(op: &Operand) -> String {
    match op {
        Operand::Value(v) => format!("v{}", v.0),
        Operand::Const(c) => format!("{:?}", c),
    }
}

/// An access's alias class, when it has one (" !tbaa2").
fn format_alias(alias: AliasClass) -> String {
    if alias == AliasClass::ANY { String::new() } else { format!(" !tbaa{}", alias.0) }
}

fn format_instruction(inst: &Instruction) -> String {
    match inst {
        Instruction::Alloca { dest, ty, size, align, .. } => {
            format!("v{} = alloca {:?} size={} align={}", dest.0, ty, size, align)
        }
        Instruction::Store { val, ptr, ty, volatile, alias, .. } => {
            let v = if *volatile { "volatile " } else { "" };
            format!("store {}{:?} {} -> v{}{}", v, ty, format_operand(val), ptr.0, format_alias(*alias))
        }
        Instruction::Load { dest, ptr, ty, volatile, alias, .. } => {
            let v = if *volatile { "volatile " } else { "" };
            format!("v{} = load {}{:?} v{}{}", dest.0, v, ty, ptr.0, format_alias(*alias))
        }
        Instruction::BinOp { dest, op, lhs, rhs, ty } => {
            format!("v{} = {:?} {:?} {}, {}", dest.0, op, ty, format_operand(lhs), format_operand(rhs))
        }
        Instruction::UnaryOp { dest, op, src, ty } => {
            format!("v{} = {:?} {:?} {}", dest.0, op, ty, format_operand(src))
        }
        Instruction::Cmp { dest, op, lhs, rhs, ty } => {
            format!("v{} = cmp {:?} {:?} {}, {}", dest.0, op, ty, format_operand(lhs), format_operand(rhs))
        }
        Instruction::Call { func, info } => {
            let args_str: Vec<String> = info.args.iter().map(format_operand).collect();
            if let Some(d) = info.dest {
                format!("v{} = call {}({})", d.0, func, args_str.join(", "))
            } else {
                format!("call {}({})", func, args_str.join(", "))
            }
        }
        Instruction::Cast { dest, src, from_ty, to_ty } => {
            format!("v{} = cast {:?}->{:?} {}", dest.0, from_ty, to_ty, format_operand(src))
        }
        Instruction::Copy { dest, src } => {
            format!("v{} = copy {}", dest.0, format_operand(src))
        }
        Instruction::GetElementPtr { dest, base, offset, ty } => {
            format!("v{} = gep {:?} v{}, {}", dest.0, ty, base.0, format_operand(offset))
        }
        Instruction::Phi { dest, ty, incoming } => {
            let inc_str: Vec<String> = incoming.iter()
                .map(|(op, bid)| format!("[{}, .L{}]", format_operand(op), bid.0))
                .collect();
            format!("v{} = phi {:?} {}", dest.0, ty, inc_str.join(", "))
        }
        Instruction::GlobalAddr { dest, name } => {
            format!("v{} = globaladdr @{}", dest.0, name)
        }
        Instruction::Memcpy { dest, src, size } => {
            format!("memcpy v{}, v{}, {}", dest.0, src.0, size)
        }
        Instruction::Select { dest, cond, true_val, false_val, ty } => {
            format!("v{} = select {:?} {}, {}, {}", dest.0, ty, format_operand(cond), format_operand(true_val), format_operand(false_val))
        }
        _ => format!("{:?}", inst),
    }
}

fn format_terminator(term: &Terminator) -> String {
    match term {
        Terminator::Return(Some(op)) => format!("ret {}", format_operand(op)),
        Terminator::Return(None) => "ret void".to_string(),
        Terminator::Branch(bid) => format!("br .L{}", bid.0),
        Terminator::CondBranch { cond, true_label, false_label } => {
            format!("condbr {}, .L{}, .L{}", format_operand(cond), true_label.0, false_label.0)
        }
        Terminator::IndirectBranch { target, possible_targets } => {
            let t: Vec<String> = possible_targets.iter().map(|b| format!(".L{}", b.0)).collect();
            format!("indirectbr {} [{}]", format_operand(target), t.join(", "))
        }
        Terminator::Switch { val, cases, default, .. } => {
            let cases_str: Vec<String> = cases.iter()
                .map(|(v, bid)| format!("{} => .L{}", v, bid.0))
                .collect();
            format!("switch {}, default .L{}, [{}]", format_operand(val), default.0, cases_str.join(", "))
        }
        Terminator::Unreachable => "unreachable".to_string(),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::ir::reexports::{BasicBlock, BlockId, CallInfo};



    /// A function of `size` casts (copies count for nothing) calling each of `calls`.
    fn function(name: &str, size: usize, calls: &[&str], always_inline: bool) -> IrFunction {
        let mut func = IrFunction::new(name.to_string(), IrType::I64, vec![], false);
        func.is_always_inline = always_inline;
        func.is_static = true;
        func.next_value_id = size as u32 + 1;
        let mut instructions: Vec<Instruction> = (0..size as u32)
            .map(|i| Instruction::Cast { dest: Value(i), src: Operand::Const(IrConst::I64(1)), from_ty: IrType::I64, to_ty: IrType::I32 })
            .collect();
        for callee in calls {
            instructions.push(Instruction::Call { func: callee.to_string(), info: CallInfo {
                dest: None, args: Vec::new(), arg_types: Vec::new(), return_type: IrType::Void,
                is_variadic: false, num_fixed_args: 0, struct_arg_sizes: Vec::new(),
                struct_arg_aligns: Vec::new(), struct_arg_classes: Vec::new(), struct_arg_riscv_float_classes: Vec::new(),
                is_sret: false, is_fastcall: false, ret_eightbyte_classes: Vec::new(),
            } });
        }
        func.blocks.push(BasicBlock { label: BlockId(0), instructions, terminator: Terminator::Return(None), source_spans: Vec::new() });
        func
    }

    #[test]
    fn test_wrappers_of_a_large_always_inline_body() {
        let big = MAX_ALWAYS_INLINE_INSTRUCTIONS + 1;
        let mut module = IrModule::new();
        module.functions = vec![
            function("large", big, &[], true),
            function("forced_step", 2, &["large"], true),
            function("wrapper", 2, &["forced_step"], false),
            function("direct", 2, &["large"], false),
            function("calls_wrapper", 2, &["wrapper"], false),
            function("unrelated", 2, &[], false),
        ];
        let carrying = carrying_large_always_inline(&module);
        let mut names: Vec<&str> = carrying.iter().copied().collect();
        names.sort_unstable();
        // Weight passes through the always_inline step, and stops at the
        // ordinary wrapper: its callers just call it.
        assert_eq!(names, vec!["direct", "forced_step", "wrapper"]);
    }

    #[test]
    fn test_nothing_carried_without_a_large_body() {
        let mut module = IrModule::new();
        module.functions = vec![
            function("small", MAX_ALWAYS_INLINE_INSTRUCTIONS, &[], true),
            function("wrapper", 2, &["small"], false),
        ];
        assert!(carrying_large_always_inline(&module).is_empty());
    }

    #[test]
    fn test_large_body_expanded_bottom_up() {
        let big = MAX_ALWAYS_INLINE_INSTRUCTIONS + 1;
        let mut module = IrModule::new();
        module.functions = vec![
            function("outer", big, &["inner", "inner"], true),
            function("inner", big, &[], true),
            function("caller", 2, &["outer"], false),
        ];
        assert_eq!(expand_large_always_inline(&mut module), 3);
        let caller = module.functions.iter().find(|f| f.name == "caller").unwrap();
        assert!(!caller.blocks.iter().flat_map(|b| &b.instructions).any(|i| matches!(i, Instruction::Call { .. })));
        assert!(effective_size(&caller.blocks) >= 3 * big);
    }

    /// More calls than the rounds a kernel gets: each is one round, and
    /// outside a kernel only the budget ends them.
    #[test]
    fn test_more_always_inline_calls_than_kernel_rounds() {
        let calls = vec!["step"; MAX_ALWAYS_INLINE_SECOND_PASS_ROUNDS + 400];
        let mut module = IrModule::new();
        module.functions = vec![function("step", 2, &[], true), function("caller", 2, &calls, false)];
        run(&mut module);
        let caller = module.functions.iter().find(|f| f.name == "caller").unwrap();
        assert!(!caller.blocks.iter().flat_map(|b| &b.instructions).any(|i| matches!(i, Instruction::Call { .. })));
    }

    /// Tiny calls an always_inline body brings in the second pass are inlined
    /// too: more sites than the first pass has rounds, each `wrap` calling `leaf`.
    #[test]
    fn test_tiny_calls_brought_by_the_second_pass() {
        let calls = vec!["wrap"; 300];
        let mut module = IrModule::new();
        module.functions = vec![
            function("leaf", 2, &[], false),
            function("wrap", 2, &["leaf"], true),
            function("caller", 2, &calls, false),
        ];
        run(&mut module);
        let caller = module.functions.iter().find(|f| f.name == "caller").unwrap();
        let left: Vec<&str> = caller.blocks.iter().flat_map(|b| &b.instructions)
            .filter_map(|i| match i { Instruction::Call { func, .. } => Some(func.as_str()), _ => None })
            .collect();
        assert!(left.is_empty(), "{} calls left, to {:?}", left.len(), left.first());
    }

    #[test]
    fn test_large_recursive_body_left_alone() {
        let big = MAX_ALWAYS_INLINE_INSTRUCTIONS + 1;
        let mut module = IrModule::new();
        module.functions = vec![
            function("itself", big, &["itself"], true),
            function("caller", 2, &["itself"], false),
        ];
        assert_eq!(expand_large_always_inline(&mut module), 0);
    }

    /// A function calling each of `callees`, then returning.
    fn caller(name: &str, callees: &[&str]) -> IrFunction {
        let mut f = IrFunction::new(name.to_string(), IrType::Void, vec![], false);
        let instructions = callees.iter().map(|c| Instruction::Call { func: c.to_string(), info: CallInfo {
            dest: None, args: vec![], arg_types: vec![], return_type: IrType::Void, is_variadic: false, num_fixed_args: 0,
            struct_arg_sizes: vec![], struct_arg_aligns: vec![], struct_arg_classes: Vec::new(),
            struct_arg_riscv_float_classes: Vec::new(), is_sret: false, is_fastcall: false, ret_eightbyte_classes: Vec::new(),
        } }).collect();
        f.blocks = vec![BasicBlock { label: BlockId(0), instructions, terminator: Terminator::Return(None), source_spans: Vec::new() }];
        f
    }

    /// A static callee of `insts` additions in `blocks` blocks, too large
    /// for the raw size rules.
    fn cleaned_only_callee(insts: usize, blocks: usize) -> CalleeData {
        let per_block = insts / blocks;
        let blocks = (0..blocks).map(|b| BasicBlock {
            label: BlockId(b as u32),
            instructions: (0..per_block).map(|i| Instruction::BinOp {
                dest: Value((b * per_block + i) as u32), op: IrBinOp::Add, lhs: Operand::Const(IrConst::I64(i as i64)),
                rhs: Operand::Const(IrConst::I64(1)), ty: IrType::I64,
            }).collect(),
            terminator: Terminator::Return(None),
            source_spans: Vec::new(),
        }).collect();
        CalleeData {
            blocks, param_struct_sizes: vec![], return_type: IrType::Void, num_params: 0, next_value_id: insts as u32,
            max_block_id: 0, is_always_inline: false, exceeds_normal_limits: false, is_static_inline: false,
            cleaned_only: true, wide: false, sole_call: false, in_cycle: false, vector: false, calls: false, params: vec![],
        }
    }

    /// The calls a body inlined for its cleaned-up size brings stay
    /// candidates, unless only its return blocks not counting admitted it
    /// (chess-perft ran 31% more instructions when rook_att's calls to
    /// ray_att stayed calls).
    #[test]
    fn calls_in_inlined_bodies_stay_candidates_within_the_block_limit() {
        let mut callee_map: HashMap<String, CalleeData> = HashMap::new();
        callee_map.insert("rook".to_string(), cleaned_only_callee(30, 1));
        callee_map.insert("spread".to_string(), cleaned_only_callee(30, MAX_INLINE_BLOCKS + 3));
        let mut cache: HashMap<String, Option<CleanedSize>> = HashMap::new();
        cache.insert("rook".to_string(), Some(CleanedSize { insts: 7, blocks: 1, non_return_blocks: 1 }));
        cache.insert("spread".to_string(),
                     Some(CleanedSize { insts: 30, blocks: MAX_INLINE_BLOCKS + 3, non_return_blocks: 2 }));
        let site = |name: &str| InlineCallSite {
            block_idx: 0, inst_idx: 0, callee_name: name.to_string(), dest: None, args: vec![], const_args: vec![],
            in_cleaned_body: false, in_loop: false,
        };
        for (name, keeps_calls) in [("rook", false), ("spread", true)] {
            let picked = select_inline_site(&[site(name)], &callee_map, false, false, false, false, false, false,
                                            MAX_INLINE_BUDGET_PER_CALLER, MAX_ALWAYS_INLINE_BUDGET_PER_CALLER, &mut cache);
            let (s, _, _, by_cleaned) = picked.expect("inlined for its cleaned-up size");
            assert_eq!((s.callee_name.as_str(), by_cleaned), (name, keeps_calls));
        }
    }

    /// A callee wide as written is kept out of the largest callers, unless it
    /// cleans up to a tiny body (lz4's LZ4_NbCommonBytes: 14 blocks written, 4
    /// instructions cleaned, 140 calls with registers saved around each).
    #[test]
    fn wide_callees_enter_the_largest_callers_only_when_tiny_once_cleaned() {
        let wide = || CalleeData { wide: true, ..cleaned_only_callee(30, 2 * MAX_INLINE_BLOCKS + 2) };
        let mut callee_map: HashMap<String, CalleeData> = HashMap::new();
        callee_map.insert("tiny".to_string(), wide());
        callee_map.insert("two_blocks".to_string(), wide());
        callee_map.insert("six_insts".to_string(), wide());
        let mut cache: HashMap<String, Option<CleanedSize>> = HashMap::new();
        cache.insert("tiny".to_string(),
                     Some(CleanedSize { insts: MAX_TINY_INLINE_INSTRUCTIONS, blocks: 1, non_return_blocks: 1 }));
        cache.insert("two_blocks".to_string(), Some(CleanedSize { insts: 4, blocks: 2, non_return_blocks: 2 }));
        cache.insert("six_insts".to_string(),
                     Some(CleanedSize { insts: MAX_TINY_INLINE_INSTRUCTIONS + 1, blocks: 1, non_return_blocks: 1 }));
        let site = |name: &str| InlineCallSite {
            block_idx: 0, inst_idx: 0, callee_name: name.to_string(), dest: None, args: vec![], const_args: vec![],
            in_cleaned_body: false, in_loop: false,
        };
        for (name, inlined) in [("tiny", true), ("two_blocks", false), ("six_insts", false)] {
            let picked = select_inline_site(&[site(name)], &callee_map, true, true, true, false, false, false,
                                            MAX_INLINE_BUDGET_PER_CALLER, MAX_ALWAYS_INLINE_BUDGET_PER_CALLER, &mut cache);
            assert_eq!(picked.is_some(), inlined, "{name}");
        }
    }

    /// A static function called once is a candidate up to twice the
    /// sole-call limits before cleanup (sqlite3VarintValue, 135
    /// instructions, 70 once cleaned up); called twice, it is held to the
    /// cleaned-only limits.
    #[test]
    fn sole_calls_are_candidates_by_their_own_raw_limits() {
        let body = |name: &str| {
            let mut f = IrFunction::new(name.to_string(), IrType::Void, vec![], false);
            f.is_static = true;
            f.blocks = cleaned_only_callee(140, 20).blocks;
            f.blocks.iter_mut().enumerate().for_each(|(b, blk)| {
                blk.label = BlockId(b as u32);
                if b + 1 < 20 {
                    blk.terminator = Terminator::Branch(BlockId(b as u32 + 1));
                }
            });
            f
        };
        let mut module = IrModule::new();
        module.functions.push(caller("main", &["once", "twice", "twice"]));
        module.functions.push(body("once"));
        module.functions.push(body("twice"));
        let map = build_callee_map(&module);
        assert!(map.get("once").is_some_and(|c| c.sole_call));
        assert!(!map.contains_key("twice"));
    }

    #[test]
    fn blocks_on_cycles_are_the_loop_blocks() {
        // 0 -> 1 -> 2 -> 1, 2 -> 3, 3 -> 3, 3 -> 4.
        let succ: Vec<Vec<usize>> = vec![vec![1], vec![2], vec![1, 3], vec![3, 4], vec![]];
        let adj = crate::ir::analysis::FlatAdj::from_vecs_usize(&succ);
        assert_eq!(blocks_on_cycles(&adj, 5), [false, true, true, true, false]);
    }

    /// A call in a loop takes a callee up to MAX_LOOP_CLEANED_INLINE_INSTRUCTIONS
    /// cleaned up (chess-perft's add_pawn_move, 48), unless it uses vector
    /// operations or makes calls of its own.
    #[test]
    fn calls_in_loops_take_larger_callees_without_vectors_or_calls() {
        let site = |in_loop: bool| InlineCallSite {
            block_idx: 0, inst_idx: 0, callee_name: "pawn".to_string(), dest: None, args: vec![], const_args: vec![],
            in_cleaned_body: false, in_loop,
        };
        let pick = |in_loop: bool, vector: bool, calls: bool| {
            let mut callee_map: HashMap<String, CalleeData> = HashMap::new();
            let mut pawn = cleaned_only_callee(64, 4);
            pawn.vector = vector;
            pawn.calls = calls;
            callee_map.insert("pawn".to_string(), pawn);
            let mut cache: HashMap<String, Option<CleanedSize>> = HashMap::new();
            cache.insert("pawn".to_string(), Some(CleanedSize { insts: 48, blocks: 5, non_return_blocks: 4 }));
            select_inline_site(&[site(in_loop)], &callee_map, false, false, false, false, false, false,
                               MAX_INLINE_BUDGET_PER_CALLER, MAX_ALWAYS_INLINE_BUDGET_PER_CALLER, &mut cache).is_some()
        };
        assert!(pick(true, false, false));
        assert!(!pick(false, false, false));
        assert!(!pick(true, true, false));
        assert!(!pick(true, false, true));
    }

    /// A function on a call-graph cycle is inlined into a caller on none,
    /// the calls it brings staying calls (so a recursive one does not
    /// unfold), and not into a caller on a cycle.
    #[test]
    fn functions_on_cycles_inline_only_into_callers_on_none() {
        let mut callee_map: HashMap<String, CalleeData> = HashMap::new();
        let mut text = cleaned_only_callee(20, 2);
        text.in_cycle = true;
        callee_map.insert("value_text".to_string(), text);
        let mut cache: HashMap<String, Option<CleanedSize>> = HashMap::new();
        cache.insert("value_text".to_string(), Some(CleanedSize { insts: 12, blocks: 2, non_return_blocks: 2 }));
        let site = InlineCallSite {
            block_idx: 0, inst_idx: 0, callee_name: "value_text".to_string(), dest: None, args: vec![], const_args: vec![],
            in_cleaned_body: false, in_loop: false,
        };
        let pick = |caller_in_cycle: bool, cache: &mut HashMap<String, Option<CleanedSize>>| {
            select_inline_site(std::slice::from_ref(&site), &callee_map, false, false, false, false, false, caller_in_cycle,
                               MAX_INLINE_BUDGET_PER_CALLER, MAX_ALWAYS_INLINE_BUDGET_PER_CALLER, cache)
        };
        let (_, _, _, keeps_calls) = pick(false, &mut cache).expect("inlined off the cycle");
        assert!(keeps_calls);
        assert!(pick(true, &mut cache).is_none());
    }

    /// In a recursive caller a small callee's blocks that only return do not
    /// count either (jsonAppendChar, 15 instructions in 4 blocks, one a
    /// return, called per character by the recursive
    /// jsonTranslateBlobToText); its instructions still do.
    #[test]
    fn recursive_callers_take_small_callees_by_their_non_return_blocks() {
        let mut callee_map: HashMap<String, CalleeData> = HashMap::new();
        callee_map.insert("append".to_string(), cleaned_only_callee(16, 4));
        callee_map.insert("bigger".to_string(), cleaned_only_callee(24, 4));
        let mut cache: HashMap<String, Option<CleanedSize>> = HashMap::new();
        cache.insert("append".to_string(), Some(CleanedSize { insts: 15, blocks: 4, non_return_blocks: 3 }));
        cache.insert("bigger".to_string(),
                     Some(CleanedSize { insts: MAX_SPECIALIZED_INLINE_INSTRUCTIONS + 4, blocks: 4, non_return_blocks: 3 }));
        let site = |name: &str| InlineCallSite {
            block_idx: 0, inst_idx: 0, callee_name: name.to_string(), dest: None, args: vec![], const_args: vec![],
            in_cleaned_body: false, in_loop: false,
        };
        let pick = |name: &str, cache: &mut HashMap<String, Option<CleanedSize>>| {
            select_inline_site(&[site(name)], &callee_map, false, false, false, false, true, false,
                               MAX_INLINE_BUDGET_PER_CALLER, MAX_ALWAYS_INLINE_BUDGET_PER_CALLER, cache)
        };
        assert!(pick("append", &mut cache).is_some());
        assert!(pick("bigger", &mut cache).is_none());
    }

    #[test]
    fn recursive_functions_are_the_call_graph_cycles() {
        let mut module = IrModule::new();
        // a <-> b, c -> c, d -> a (calls into a cycle, not on one), e -> extern.
        for (name, callees) in [("a", &["b"][..]), ("b", &["a", "d"]), ("c", &["c"]), ("d", &["e"]), ("e", &["puts"])] {
            module.functions.push(caller(name, callees));
        }
        module.functions.push(caller("f", &["a", "c"]));
        let rec = recursive_functions(&module);
        let mut names: Vec<&str> = rec.into_iter().collect();
        names.sort_unstable();
        assert_eq!(names, ["a", "b", "c"]);
    }

    /// Static functions called once: not one called twice, one whose
    /// address is taken, one a global points to, or an external one.
    #[test]
    fn sole_call_statics_are_called_once_and_never_referenced() {
        let mut module = IrModule::new();
        module.functions.push(caller("main", &["once", "twice", "twice", "taken", "pointed", "ext"]));
        for name in ["once", "twice", "taken", "pointed", "ext"] {
            let mut f = caller(name, &[]);
            f.is_static = name != "ext";
            module.functions.push(f);
        }
        module.functions[0].blocks[0].instructions.push(Instruction::GlobalAddr { dest: Value(1), name: "taken".to_string() });
        module.globals.push(crate::ir::reexports::IrGlobal {
            name: "table".to_string(), ty: IrType::Ptr, size: 8, align: 8,
            init: crate::ir::reexports::GlobalInit::GlobalAddr("pointed".to_string()), is_static: true,
            is_extern: false, is_common: false, section: None, is_weak: false, visibility: None,
            has_explicit_align: false, is_const: true, is_used: false, is_thread_local: false,
        });
        let mut names: Vec<&str> = sole_call_statics(&module).into_iter().collect();
        names.sort_unstable();
        assert_eq!(names, ["once"]);
    }
}
