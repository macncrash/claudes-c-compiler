# Optimization Passes

This document describes the design and implementation of the optimization pass
pipeline. The pipeline transforms the compiler's intermediate representation (IR)
to produce better machine code by eliminating redundant computation, simplifying
control flow, and replacing expensive operations with cheaper equivalents.

All optimization levels (`-O0` through `-O3`, `-Os`, `-Oz`) run the same full set
of passes. While the compiler is still maturing, having separate tiers creates
hard-to-find bugs where code works at one level but breaks at another. We always
run all passes to maximize test coverage of the optimizer and catch issues early.

## Table of Contents

1. [Pipeline Overview](#pipeline-overview)
2. [Phase Structure](#phase-structure)
3. [Dirty Tracking and Iteration Strategy](#dirty-tracking-and-iteration-strategy)
4. [Shared CFG Analysis](#shared-cfg-analysis)
5. [Strict Aliasing](#strict-aliasing)
6. [Pass Descriptions](#pass-descriptions)
7. [Pass Dependency Graph](#pass-dependency-graph)
8. [Disabling Individual Passes](#disabling-individual-passes)
9. [Files](#files)

---

## Pipeline Overview

The optimizer executes in four sequential phases:

```
 Phase 0: Inlining
     inline
     (convert gnu_inline defs to declarations)
     mem2reg
     constant_fold
     copy_prop
     simplify
     constant_fold
     copy_prop
     dce, mem2reg (locals passed by address to inlined calls)
     resolve_asm
     addr_arg_promote
     global_promote
         |
         v
 Phase 0.5: IsConstant Resolution
     resolve_remaining_is_constant
         |
         v
 Main Loop (up to 3 iterations, with dirty tracking)
   +-------------------------------------------------------+
   |  1.  cfg_simplify                                     |
   |  2.  copy_prop                                        |
   |  2a. div_by_const      (iteration 0 only, 64-bit targets only) |
   |  2b. narrow                                           |
   |  3.  simplify                                         |
   |  4.  constant_fold                                    |
   |  4a. range_fold                                       |
   |  4b. loop_unroll      (iteration 0 only)              |
   |  5.  gvn              \                               |
   |  6.  licm               > shared CFG analysis         |
   |  6a. iv_strength_reduce /  (iteration 0 only)         |
   |  7.  if_convert                                       |
   |  8.  copy_prop        (second round)                  |
   |  9.  dce                                              |
   | 10.  cfg_simplify     (second round)                  |
   | 10.5 ipcp             (interprocedural)               |
   +-------------------------------------------------------+
         |
         v
 Phase 9b: Small Memory Calls In Place
     mem_inline
         |
         v
 Phase 9c: Loads Joined Across Merges
     load_phi
         |
         v
 Phase 9d: Stores to Locals Nothing Reads
     dce::eliminate_unread_local_stores
         |
         v
 Phase 10a: Loop Bound Hoisting
     bound_hoist
         |
         v
 Phase 10b: Induction Variable Widening (64-bit targets, not with -fwrapv)
     iv_widen
         |
         v
 Phase 10c: Linear Function Test Replacement (same conditions)
     lftr
         |
         v
 Phase 10c1: In-Place Loop Vectorization (AArch64)
     loop_vectorize
         |
         v
 Phase 10c2: GEP Offset Cancellation
     gep_linear
         |
         v
 Phase 10d: Pressure Hoisting
     pressure_hoist
         |
         v
 Phase 10e: GEP Offset Reassociation
     gep_reassoc
         |
         v
 Phase 10e2: Loop Exit Addresses (AArch64)
     exit_addr
         |
         v
 Phase 10e3: Edge Addresses (64-bit)
     edge_addr
         |
         v
 Phase 10f: Leaf Rematerialization (AArch64)
     remat
         |
         v
 Phase 10g: Return Tail Duplication
     ret_dup
         |
         v
 Phase 10g2: Flags Compared Where Paths Meet
     phi_cmp
         |
         v
 Phase 10h: Extensions Loads Already Made (AArch64)
     ext_elide
         |
         v
 Phase 10i: Counter Steps After Their Last Use
     step_sink
         |
         v
 Phase 10j: Addresses Down to Their Access (AArch64)
     addr_sink (in step_sink.rs)
         |
         v
 Phase 10j2: Next Words' Loads Next to Each Other (AArch64)
     load_group (group_loads in step_sink.rs)
         |
         v
 Phase 10k: Large GEP Offsets From Nearby Ones (AArch64)
     chain_large_offsets (in gep_reassoc.rs)
         |
         v
 Phase 11: Dead Static Elimination
     dead_statics
         |
         v
 Phi elimination (src/ir/mem2reg), then:
     loop_rotate, loop_hoist, loop_promote, loop_layout, latch_dup, step_to_load, const_hoist
```

## Phase Structure

### Phase 0 -- Inlining

Function inlining runs first, before the main optimization loop. The inliner
substitutes the bodies of small static and `static inline` functions (as well as
`__attribute__((always_inline))` functions) into their call sites. After
inlining, `extern inline` functions with the `gnu_inline` attribute are
converted to declarations: their bodies existed only for the inliner, and
emitting them as definitions would cause infinite recursion when their internal
calls resolve to the local definition instead of the intended external library
symbol.

After inlining, a sequence of cleanup passes runs once on the freshly inlined
code:

1. **mem2reg** -- promotes stack allocations created during inlining back into
   SSA registers.
2. **constant_fold** -- folds constants that became visible after inlining
   (e.g., an inlined function that returns a constant).
3. **copy_prop** -- propagates copies introduced by phi elimination and mem2reg.
4. **simplify** -- applies algebraic identities to the simplified code.
5. **constant_fold** (again) -- catches additional folding opportunities exposed
   by simplification.
6. **copy_prop** (again) -- cleans up any remaining copies.
7. **dce** and **mem2reg** (again) -- a local passed by address to an inlined
   callee (`find(key, &v)`) is still reached through `gep v, 0` and the
   callee's promoted parameter (a phi when the call is in a loop). Once
   simplification has made the GEP a copy and DCE has removed the dead
   copies and phis, the local is only loaded and stored, and is promoted.
   Only in functions with such a local (a small alloca read by a copy or a
   phi): DCE this early changes what later passes do elsewhere.
8. **resolve_asm** -- resolves inline assembly symbol references that became
   computable after inlining with constant arguments.
9. **addr_arg_promote** -- a local whose address only goes to calls of
   functions that do not capture it (per `mod_ref`), and is otherwise only
   loaded and stored with its own type, is kept in a register: the loads
   and stores move to a fresh local that mem2reg promotes, and the original
   slot is written just before each such call and read back just after
   (only if the callee may write it). `n = 0; get_varint(p, &n)` no longer
   puts every read of `n` through the stack. A variable stored in more than
   32 places is left alone: as one value live across a large function (an
   interpreter's `rc`) it held a register the hot paths needed. A scalar
   parameter passed on by address counts as a local (its value is stored
   into its slot by the IR): SQLite's patternCompare advances `zPattern` and
   `zString` inline for ASCII and through `sqlite3Utf8Read(&zString)`
   otherwise, and loaded and stored them on every character.
10. **global_promote** -- a `static` variable whose address the module uses
   only for direct loads and stores of its type, accessed in a loop with no
   calls, inline assembly or other unseen memory effects, is kept in a
   register through the loop: copied to a fresh local before it, stored back
   at each exit (each reached only from the loop), and promoted by mem2reg.
   The outermost such loop is used. An inlined generator's `state += ...`
   was otherwise loaded and stored on every iteration, each load waiting on
   the previous store.

A scalar declared without an initializer in a loop body is lowered with a
store of 0 at its declaration: it is a new object each iteration, and
without it mem2reg would carry the last iteration's value around the loop
(the `v` that an inlined lookup sets only on a hit).

### Phase 0.5 -- IsConstant Resolution

After inlining and its cleanup passes, any `__builtin_constant_p` call whose
operand became a compile-time constant has already been resolved to `1` (true)
by constant folding. The remaining `IsConstant` instructions have operands that
are definitively not compile-time constants (parameters, globals, etc.), so this
phase resolves them all to `0` (false).

This resolution is critical: it allows `cfg_simplify` in the main loop to fold
conditional branches that test `__builtin_constant_p` results and eliminate dead
code paths. Without it, unreachable function calls guarded by
`__builtin_constant_p` would survive into the object file and cause linker
errors for intentionally undefined symbols (e.g., the Linux kernel's
`__bad_udelay()`, which is left undefined to generate a link error for invalid
`udelay()` arguments).

### Main Loop

The main optimization loop runs for up to 3 iterations. Each iteration executes
the full sequence of intraprocedural passes plus one interprocedural pass (IPCP)
at the end. The loop uses per-function dirty tracking and per-pass skip logic to
avoid redundant work, and it terminates early when either:

- No pass made any changes (the IR has reached a fixed point), or
- The iteration produced fewer than 5% as many changes as the first iteration
  (diminishing returns), unless IPCP made changes that require a full cleanup
  iteration.

Two passes run only during the first iteration: **div_by_const** (the expansion
is a one-time rewrite, and subsequent iterations clean up the generated code) and
**iv_strength_reduce** (a loop transformation that creates work for later cleanup
rather than benefiting from iteration).

### Phase 11 -- Dead Static Elimination

After all intraprocedural optimizations are complete, a final interprocedural
pass removes internal-linkage (`static`) functions and `static const` globals
that are no longer referenced by any live symbol. This is essential for `static
inline` functions from headers: after optimization eliminates dead code paths,
some callees become completely unreferenced and can be removed. Without this
cleanup, dead functions may reference undefined external symbols and cause
linker errors.

---

## Dirty Tracking and Iteration Strategy

The optimizer maintains two boolean vectors, each with one entry per function in
the module: **dirty** and **changed**.

- **dirty** indicates which functions should be visited during the current
  iteration. At the start of the first iteration, every function is dirty.
- **changed** accumulates which functions were modified by any pass during the
  current iteration.

The helper function `run_on_visited` enforces the dirty-tracking contract: it
skips declaration-only functions entirely, skips non-dirty functions, and marks
a function as changed whenever a pass reports modifications.

At the end of each iteration, `dirty` and `changed` are swapped: only functions
that were actually modified become dirty for the next iteration. This ensures
that functions which have already converged are not revisited.

### Per-Pass Skip Logic

In addition to per-function dirty tracking, the pipeline tracks how many changes
each pass made in the previous iteration. Before running a pass, the pipeline
checks whether the pass itself or any of its upstream dependencies made changes
last time. If neither the pass nor its upstream passes produced any changes, the
pass is skipped entirely for the current iteration.

This is driven by an explicit dependency graph encoded in the `should_run!`
macro. For example, `constant_fold` depends on `copy_prop`, `narrow`,
`simplify`, `if_convert`, and the second `copy_prop`; if none of those passes
made changes in the previous iteration, constant folding is skipped.

On the first iteration, all previous-change counts are set to `MAX` so every
pass runs unconditionally.

### Diminishing Returns and Early Exit

The pipeline tracks the total change count (excluding DCE) from the first
iteration as a baseline. If a subsequent iteration produces fewer than 5% of
that baseline, and IPCP did not make changes, the loop exits early. DCE is
excluded from this comparison because it is a cleanup pass whose large change
count (often thousands of removed dead instructions) would inflate the baseline
and make productive later iterations look like diminishing returns.

An exception is made for IPCP: if interprocedural constant propagation made
changes in an iteration, the pipeline always runs at least one more iteration
regardless of diminishing returns, because IPCP changes (constant arguments,
dead call elimination) require a full round of constant folding, DCE, and CFG
simplification to clean up.

The pipeline also guarantees at least two full iterations before the diminishing-
returns check can trigger, because multi-step constant propagation chains
(e.g., switch folding through layers of inlined helper functions) need at least
two iterations to fully propagate.

---

## Shared CFG Analysis

Three passes in the main loop -- GVN, LICM, and IVSR -- all require the same
expensive CFG analysis: label maps, predecessor/successor adjacency, dominator
trees, and natural loop detection. Rather than computing this independently in
each pass, the function `run_gvn_licm_ivsr_shared` builds a single
`CfgAnalysis` per function and passes it to all three.

This sharing is sound because:

- **GVN** only replaces operands within existing instructions; it does not add,
  remove, or reorder blocks, so the CFG structure is unchanged after GVN.
- **LICM** hoists instructions into preheader blocks but does not add or remove
  blocks from the CFG, so the analysis remains valid for IVSR.

For single-block functions, only GVN runs (via a fast path that skips CFG
analysis entirely), since LICM and IVSR require at least two blocks to form a
loop.

---

## Strict Aliasing

C forbids reading an object through an lvalue of an unrelated type (C11
6.5p7), so a store of one type cannot change memory a load of another type
reads in a program without undefined behaviour. Lowering gives each load and
store an alias class (`AliasClass`, the `alias` field of `Load`/`Store`): the
lvalue's scalar type, with signedness and same-size integer types merged
(`int` and `unsigned`, `long` and `long long`), separate classes for `float`,
`double`, `long double` and pointers. Everything else is `ANY`, which aliases
every class: character types, aggregates, vectors, complex values, bitfields,
anything reached through a union member (punning through a union is allowed),
and every access the compiler makes itself (struct copies, ABI moves, va_arg,
temporaries, the loads and stores passes create). A class that does not match
the access's IR type (a scalar read in pieces) also becomes `ANY`.

Four places use the classes: GVN keeps cached loads and forwardable stores of
other classes across a store; load_phi walks past a store of another class;
loop_hoist hoists a load the loop writes only with stores of other classes;
loop_promote keeps a local's field in a register through a loop whose other
accesses that may reach it are of other classes.

Classes are dropped before optimization (every access is then `ANY`) with
`-fno-strict-aliasing`, at `-O0` (strict aliasing is on by default from -O1,
as in GCC), in a translation unit that mentions `may_alias` or
`no-strict-aliasing` (neither the attribute nor a function's own
`optimize("no-strict-aliasing")` is tracked), and with
`CCC_DISABLE_PASSES=tbaa`.

---

## Pass Descriptions

### inline -- Function Inlining

Substitutes callee function bodies into call sites to eliminate call overhead
and expose cross-function optimization opportunities. The inliner runs before
the main optimization loop because inlining creates the most significant
optimization opportunities: constant propagation through function arguments,
dead code elimination of unused branches, and elimination of small wrapper
functions.

**Eligibility.** Not all functions can be inlined. Variadic functions, functions
containing `VaStart`/`VaEnd`/`VaArg`, `DynAlloca`, `StackSave`/`StackRestore`,
`IndirectBranch`, or static locals with label references are excluded.
`__attribute__((noinline))` functions and recursive calls are also excluded, as
are (unless `always_inline`) functions on a cycle of the call graph, found as
strongly connected components: an inlined recursive function brings its own
call along, which was inlined again up to the round limit (SQLite's
22-instruction `sqlite3BitvecDestroy` became 5,000 instructions in each of its
callers, and the whole file took twice as long to compile).

**Linkage.** A function with external linkage is a candidate too, judged by
its cleaned-up size (after mem2reg and folding, as for larger static
functions): a call in the same file may use the definition it sees, as
clang's does, and the function keeps its own copy for other callers and
function pointers. Weak functions are excluded (the linker may choose
another definition), as are naked ones and ones in a section of their own.
Inlining SQLite's `sqlite3_free` and `sqlite3_value_type`, API functions its
own code calls tens of millions of times in speedtest, cut 0.3% of its
instructions.

**Cleaned-up size.** A function too large by its raw size (up to 24 blocks
and 120 instructions before mem2reg) is inlined where, specialized on the
call's constant arguments and cleaned up, it has at most 40 instructions
in 6 blocks. Outside callers past the absolute cap, blocks that only return
don't count, as inlined they become jumps to the caller's continuation:
`sqlite3PagerWrite`, five tests and five returns, is 19 raw blocks and 10
cleaned up. In the largest callers (SQLite's `sqlite3VdbeExec`) the
stricter limits stay, raw blocks at 12 and every block counted: there, the
wider raw limit added cold code to opcode handlers and made it run 4% more
instructions, mostly spills on the dispatch path, and counting only
non-return blocks everywhere cost speedtest 4% as a whole. A cleaned-up size counts only
the small callees inlined into the callee, so a call arriving with a body
inlined this way is not inlined by this rule: `sqlite3DbNNFreeNN`, 21
instructions, otherwise brought `sqlite3_free` and its statistics into each
of its 82 callers. Together these cut speedtest's instructions 0.4%, for
8% more code.

**Heuristics.** The inliner uses a tiered size-based heuristic:

| Category | Instruction limit | Block limit | Notes |
|---|---|---|---|
| Tiny | 5 | 1 | Always inlined, ignores caller size |
| Small / static inline | 20 | 3 | Always inlined if budget not exhausted, or if `always_inline` |
| Normal static | 30 | 4 | Inlined if under caller budget |
| Normal eligible | 60 | 6 | Inlined if under caller budget |
| `always_inline` | 500 | 500 | Separate 200-instruction budget (main) + 400 (second pass) |

Inlining respects per-caller budgets: normal inlining stops when the caller
exceeds 200 instructions or 800 total inlined instructions, with a hard cap at
500 instructions (1000 absolute cap). The `always_inline` attribute has its own
200-instruction budget in the main loop, kept low to prevent stack frame bloat
(CCC allocates ~8 bytes per SSA value on the stack). When the caller has a
section attribute (e.g., `.init.text` in the Linux kernel), `always_inline`
callees bypass the budget entirely, ensuring kernel initialization code is always
fully inlined.

Each caller function is processed for up to 200 inlining rounds to handle chains
of inlined calls (e.g., A calls B calls C, where B is inlined into A, exposing
the call to C).

After the main loop, a **second pass** processes any remaining `always_inline`
call sites with an independent 400-instruction budget and up to 300 rounds. This
handles correctness-critical cases where the main loop's budget or round limit
was exhausted before resolving all always_inline chains (e.g., KVM nVHE functions
referencing section-specific symbols, or `cpucap_is_possible()` chains that must
be inlined to eliminate `__attribute__((error))` calls and undefined symbol
references). Small `always_inline` callees (≤20 instructions, ≤3 blocks) bypass
the budget in both passes because they have correctness requirements: inline asm
`"i"` constraints (e.g., `arch_static_branch`'s `__jump_table` entries) need
resolved symbol references. The two budgets are independent, so the combined
maximum is 200 + 400 = 600 always_inline instructions per caller. The second
pass does not enforce caller size caps (hard cap, absolute cap) because these
are correctness-critical inlines that must proceed regardless of caller size.

**Mechanics.** The inliner clones the callee's blocks with remapped value and
block IDs, wires arguments by inserting stores into the callee's parameter
allocas, creates a merge block for the return value (using a phi if the callee
has multiple return paths), and splices the cloned blocks into the caller.
After all inlining is complete, inline assembly symbol references are resolved
by tracing value chains back through `Load`, `Store`, `Copy`, `GEP`, and
`GlobalAddr` instructions.

### cfg_simplify -- CFG Simplification

Simplifies the control flow graph through seven sub-passes that run to a
fixpoint (repeating until no sub-pass makes changes):

1. **Fold constant conditional branches.** Resolves `CondBranch` conditions
   by walking Copy, Phi, Cmp, Select, and Cast chains -- both locally within a
   block and globally across single-predecessor chains (up to 8 hops) and across
   all blocks (up to 16 recursion depth). When the condition resolves to a
   constant, replaces the branch with an unconditional jump to the taken target.

2. **Fold constant switches.** Same idea for `Switch` terminators: if the
   discriminant resolves to a constant, replaces the switch with a direct branch
   to the matching case (or default).

3. **Simplify redundant conditional branches.** When a `CondBranch` has both
   targets pointing to the same block, replaces it with an unconditional branch.
   A branch on a value that a dominating branch on the same value already
   decided (the block lies under that branch's true or false edge, into a
   block only it enters) becomes a jump: OP_Column's
   `if (VdbeMemDynamic(pDest)) sqlite3VdbeMemSetNull(pDest)` tested the flags
   again in the inlined helper, GVN making it the same value.

4. **Thread jump chains.** Identifies empty blocks (no instructions,
   unconditional branch terminator) and redirects their predecessors to the
   final target. Chains are followed transitively with cycle detection, up to
   a depth limit of 32 hops. A safety check prevents threading when it would
   merge `CondBranch` edges that carry conflicting phi values. A block that
   only merges a boolean (phi of constants) and branches on it is threaded
   too: each predecessor goes straight to the target its constant selects
   (`a || b || c`). It may merge other values as well (the pointers of
   `*++scan == *++match && ...`, or a counter of `m > limit && --n != 0`)
   when each use of them is the target's phi entry from the block (the
   predecessor's entry takes its own value), lies past the other target
   (which only the block enters), or lies past the target itself: then a
   new phi at the target merges the value for the uses there (at most one
   such target per round). When the block also merges other values its targets
   read (the `v` an inlined `find(key, &v)` sets on a hit), every
   predecessor is redirected, and each such value is merged again by a phi
   at the start of the one target whose region reads it, which must be
   reached only from the block. Two tests of one value against constant
   bounds, each in its own block and failing to the same target
   (`c >= 'a' && c <= 'z'`, or negated, `x < lo || x > hi`), become one:
   `x - lo <=u hi - lo` (an add of `-lo` for a negative bound), when the
   second block does nothing else, is reached only from the first, both
   compares are signed or both unsigned, and the failing target's phis
   take the same values from both.

5. **Remove dead blocks.** BFS from the entry block to compute reachability
   (also marking blocks referenced by `LabelAddr`, inline asm goto labels, and
   global initializer label blocks). Unreachable blocks are removed and their
   phi entries in surviving blocks are cleaned up.

6. **Simplify trivial phis.** Phi nodes with a single incoming edge, or where
   all incoming values are identical, are replaced with `Copy` instructions.

7. **Merge single-predecessor blocks.** When block A ends with an unconditional
   branch to block B and B has exactly one predecessor (A), fuses B into A.
   Skips blocks referenced by `LabelAddr`, inline asm goto labels, or global
   initializer labels.

This pass appears twice in each iteration of the main loop: once at the
beginning (to clean up from the previous iteration's constant folding and DCE)
and once at the end (to clean up after the current iteration's DCE and
if-conversion).

### copy_prop -- Copy Propagation

Replaces uses of a `Copy` instruction's destination with the `Copy`'s source
operand, transitively following chains of copies. This is important because many
other passes produce copies: phi simplification, mem2reg, algebraic
simplification (when an identity like `x + 0` reduces to `x`), and GVN (when a
redundant computation is replaced with a reference to an earlier equivalent).
Without copy propagation, each copy becomes a redundant register move or
load-store pair in code generation.

Uses a flat `Vec<Option<Operand>>` indexed by value ID for O(1) lookups, taking
advantage of the dense sequential nature of SSA value IDs. Chain resolution uses
union-find-style path compression: when resolving `%a = Copy %b` and
`%b = Copy %c`, all intermediate entries are updated to point directly to `%c`
in a single pass, giving amortized O(1) resolution. Multi-def detection prevents
propagating ambiguous values (e.g., a value defined by `Copy` in two different
branches). Chain depth is limited to 64 hops to prevent pathological cases.

After copy propagation, the dead `Copy` instructions themselves are cleaned up
by DCE. This pass appears twice in each iteration: once early (after CFG
simplification) and once late (after GVN, LICM, and if-conversion), to
propagate copies generated by intermediate passes.

### div_by_const -- Division by Constant Strength Reduction

Replaces integer division and modulo by compile-time constants with equivalent
multiply-and-shift sequences. On x86, `div`/`idiv` instructions cost 20-90
cycles, while the replacement sequence costs 3-5 cycles, making this one of the
most impactful single optimizations for integer-heavy code.

Supported transformations:

- **Unsigned division** (`x /u C`): replaced with a widened multiply by a magic
  number followed by a right shift, using the algorithm from Hacker's Delight
  by Henry S. Warren Jr. When the magic number exceeds 32 bits, an
  add-and-shift fixup is used.
- **Signed division by power-of-2** (`x /s 2^k`): replaced with a biased
  arithmetic right shift that handles negative rounding correctly.
- **Signed division by constant** (`x /s C`): replaced with a magic-number
  multiply, shift, and sign correction.
- **Signed division by negative constant** (`x /s -C`): delegates to the
  positive-constant case and negates the result.
- **Modulo** (`x % C`): rewritten as `x - (x / C) * C` using the optimized
  division from above. Power-of-2 unsigned modulo is handled separately by
  the simplify pass as `x & (C - 1)`.
- **Small dividends**: an I64 dividend widened from an 8- or 16-bit integer,
  or masked to 16 bits, uses the smallest multiplier that is exact over its
  whole range (checked value by value), when that is below 2^16: one move
  instead of two (`x * 43 >> 8` for a `signed char` divided by 6).

**Only 32-bit operand values are currently optimized.** For native I32/U32
types, the pass applies directly. For I64/U64 operations, the pass performs
value-range analysis to detect operands that are provably 32-bit (e.g., values
produced by zero-extending or sign-extending from a 32-bit type, or loaded from
a 32-bit memory location). When both the dividend and divisor fit in 32 bits,
the pass generates the 32-bit magic-number sequence operating within the 64-bit
type. Genuine 64-bit divisions (where the dividend may exceed 32-bit range) fall
through to native `div`/`idiv` instructions.

The pass is disabled on 32-bit targets (i686) because the generated 64-bit
multiply-and-shift sequences cannot be executed correctly by the 32-bit backend.
It runs only during the first iteration of the main loop; subsequent iterations
clean up the expanded instruction sequences.

### narrow -- Integer Narrowing

Eliminates unnecessary widening introduced by C's integer promotion rules. The
compiler's frontend promotes sub-64-bit operands to I64 before performing
arithmetic, then narrows the result back. This creates a
widen-operate-narrow pattern that generates redundant sign/zero-extension
instructions.

The pass operates in three transformation phases:

**Phase 1: Narrow binary operations with explicit narrowing cast.** Detects the
canonical widen-operate-narrow pattern:

```
%w = Cast %x, I32 -> I64       (widen)
%r = BinOp add %w, %y, I64    (operate at I64)
%n = Cast %r, I64 -> I32       (narrow)
```

And rewrites to operate directly at the narrow type:

```
%r = BinOp add %x, narrow(%y), I32
```

This is safe for operations where the low bits are width-independent when
truncated: Add, Sub, Mul, And, Or, Xor, and Shl. Right shifts are safe when
the extension type matches the shift type (arithmetic shift with sign extension,
logical shift with zero extension). The BinOp result must have exactly one use
(the narrowing cast). For a 32-bit result, an operand that is the one use of a
widening cast from a smaller type also qualifies: the cast then widens to the
32-bit type instead (`(ush)v << n` with an int shift becomes a 32-bit shift
of the 16-bit value zero-extended to 32 bits).

An I64 Add/Sub/Mul/And/Or/Xor whose uses all read only its low 32 bits (32-bit
operations of those kinds, and truncations to 32 bits or fewer) is done at 32
bits, whatever the number of uses; a truncation then reads the 32-bit result
(zlib's `dist - base_dist[code]`, read as an unsigned and as a 16-bit value).
For uses that are all truncations, the signedness is the one the operands
narrow to.

A value truncated from 32 or 64 bits and extended back is the value itself to
uses that read only the bits the truncation kept (truncations and stores at
most that wide, through Add/Sub/Mul/And/Or/Xor and the left of Shl, and right
shifts by a constant k whose uses read k fewer): `s->bi_buf
|= (ush)v << s->bi_valid` truncates the shift to 16 bits, extends it for the
or and truncates the or to 16 bits, so the extension goes (a `sxtw` or an
`and` each send).

**Phase 2: Narrow binary operations without explicit cast.** On 64-bit targets,
narrows I64 bitwise operations (And, Or, Xor) to I32 when all operands are
provably 32-bit values, even without an explicit narrowing cast. This handles
cases where the result is consumed by another operation rather than an explicit
truncation. Uses both the widening cast map and a load-type map to identify
narrow operands. Disabled on 32-bit targets and restricted to I32/U32 output
to avoid sub-int register complications.

**Phase 3: Narrow comparisons.** When both operands of a comparison were
widened from the same type, narrows the comparison to operate at the original
type. Signed comparisons (Slt/Sle/Sgt/Sge) require the source type to be
signed; unsigned comparisons (Ult/Ule/Ugt/Uge) require unsigned; Eq/Ne are
safe with either sign. The RHS operand can be a constant if it round-trips
safely through extension to the comparison type.

### simplify -- Algebraic Simplification

Applies algebraic identities, strength reductions, and peephole optimizations
to individual instructions. This is one of the broadest passes in the pipeline.

**Identity simplifications** (integer-only, to preserve IEEE 754 float
semantics):

```
x + 0 => x          x - 0 => x          x - x => 0
x * 0 => 0          x * 1 => x          x / 1 => x
x / x => 1          x % 1 => 0          x % x => 0
x & 0 => 0          x & ~0 => x         x & x => x
x | 0 => x          x | ~0 => ~0        x | x => x
x ^ 0 => x          x ^ x => 0
x << 0 => x          x >> 0 => x
0 << x => 0          0 >> x => 0
```

Float identities are restricted to cases that respect IEEE 754: `x * 1.0` and
`x / 1.0` are safe, but `x + 0.0`, `x * 0.0`, `x - x`, and `x / x` are not
(due to signed zeros, NaN, and infinity).

**Strength reductions** (integer-only):

```
x * 2     => x + x
x * 2^k   => x << k
x * (-1)  => neg(x)
x /u 2^k  => x >>l k    (unsigned only)
x %u 2^k  => x & (2^k - 1)   (unsigned only)
```

**Constant reassociation.** When a binary operation's operand is itself a binary
operation with a constant, the two constants are combined. For example:
`(x + 3) + 5 => x + 8`, `(x * 4) * 8 => x * 32`, `(x & 0xFF) & 0x0F => x & 0x0F`,
`(x << 2) << 3 => x << 5`. Handles all commutative variants and cross-operator
interactions (e.g., `(x + C1) - C2 => x + (C1 - C2)`). When the combined
constant is an identity element, the entire operation is eliminated. A mask
that keeps every bit of the inner one is the inner value (`(x & 63) & 255 => x & 63`);
otherwise bitwise operations are not reassociated when the inner result has
other uses, since reading `x` instead would keep both live.

**Negation elimination.** Rewrites `x - (neg y)` as `x + y` by tracking
`UnaryOp::Neg` definitions.

**Cast chain optimization:**

```
Cast(x, T -> T)                       => Copy(x)       (identity cast)
Cast(const, A -> B)                   => Copy(folded)   (constant cast)
Cast(Cast(x:A, A->B), B->A) where A<=B => Copy(x)     (widen then narrow back)
Cast(Cast(x, A->B), B->C) where A<B<C  => Cast(x, A->C)  (double widen, same sign)
Cast(Cast(x, A->B), B->C) where A>B>C  => Cast(x, A->C)  (double narrow)
```

**Value ranges.** On 64-bit targets each value whose producer leaves it in
canonical form (32-bit signed values sign-extended in their register, unsigned
ones zero-extended, on every backend) gets bounds from its producer: loads and
extensions of narrow types, masks, shifts, comparisons, selects, and add, sub,
mul and shl of bounded values (exact modulo the width, so the interval holds
when it fits the type). Then:

```
Cast between I32/U32/I64/U64 that leaves the register unchanged => Copy(x)
    (narrowing a value in range; widening one in [0, 2^31), which every
    target extends alike however a later pass computes it)
    (sext(trunc(m & 63)), int arithmetic on a sign-extended byte)
BinOp with a single possible result => that constant
    (the sign fix-up x >> 63 of a non-negative x in x / 2)
```

These run in a sweep of their own before the other rewrites, which may leave
a value out of canonical form (`x ^ 0 => x`). A compare of type T also reads an
operand widened from T as the value before widening.

**Comparison simplifications:**

- Self-comparison: `Cmp(Eq, x, x)` => 1, `Cmp(Slt, x, x)` => 0, etc.
  (integer-only, float self-comparison is not safe due to NaN).
- Boolean test elimination: `Cmp(Ne, bool, 0)` => `bool`,
  `Cmp(Eq, bool, 0)` => inverted comparison (when the boolean comes from a Cmp).
  Boolean-ness is tracked transitively through And, Or, Xor of boolean values.
- Unsigned-zero: `Ult x, 0` => 0 (always false), `Uge x, 0` => 1 (always true),
  `Ule x, 0` => `Eq x, 0`, `Ugt x, 0` => `Ne x, 0`.

**Operand canonicalization.** Commutative BinOps and Cmp instructions with a
constant on the left are swapped to place the constant on the right, normalizing
the representation for downstream passes.

**Select simplification:**

```
select cond, x, x          => x
select Const(0), a, b      => b
select Const(nonzero), a, b => a
```

**GEP simplification:**

```
GEP(base, 0)                  => Copy(base)
GEP(GEP(base, C1), C2)        => GEP(base, C1 + C2)
```

**Math library call lowering.** Recognized calls are replaced with IR
intrinsics: `sqrt`/`sqrtf` => `SqrtF64`/`SqrtF32`, `fabs`/`fabsf` =>
`FabsF64`/`FabsF32`, `pow(x, 0.0)` => `1.0`, `pow(x, 1.0)` => `x`,
`pow(x, 2.0)` => `x * x`, `pow(x, -1.0)` => `1.0 / x`,
`pow(x, 0.5)` => `sqrt(x)`.

### constant_fold -- Constant Folding

Evaluates operations whose operands are all compile-time constants, replacing
the instruction with the computed result. Runs to a fixpoint within each
function, rebuilding the constant map each iteration so that folded results
chain into subsequent folds.

**Folded operation types:**

- **BinOp**: all integer arithmetic, bitwise, and shift operations. Float
  (F32/F64) arithmetic via native IEEE 754. F128/long double via platform-
  specific x87 (x86/i686) or software f128 (ARM/RISC-V) arithmetic. I128 via
  native Rust i128.
- **UnaryOp**: Neg, Not, Clz, Ctz, Popcount (width-sensitive), Bswap
  (16/32/64-bit), and IsConstant.
- **Cmp**: all 10 comparison operators for integer, float, F128 (with correct
  NaN unordered handling), and I128.
- **Cast**: integer-to-integer (with proper width truncation), float-to-float,
  float-to-int (with NaN/Inf/out-of-range safety), int-to-float, and I128
  casts with proper sign/zero extension.
- **Select**: both-arms-same folding, constant-condition folding, and
  both-arms-same-constant folding (even across different integer widths).
- **GetElementPtr**: constant base + constant offset.

**Width sensitivity.** Sub-32-bit types (I8, U8, I16, U16) require careful
promotion: the constant map tracks the target type of Cast instructions so that
I8 values are sign-extended while U8 values are zero-extended, implementing C11
integer promotion semantics.

**Division by zero.** Integer division/remainder by zero is not folded
(preserving undefined behavior). Float division by zero is folded (producing
Inf or NaN per IEEE 754).

**IsConstant handling.** The pass folds `IsConstant` to 1 when the operand is a
compile-time constant. Non-constant operands are left for the Phase 0.5
resolution step (see above).

### range_fold -- Range-Decided Comparisons

Runs right after constant_fold, on the same skip condition. Each integer
comparison is decided from the ranges of its operands where they do not
overlap. A value's range comes from its definition: constants, casts,
add/sub/mul whose exact result stays in the type, masks, remainders,
divisions and shifts by constants, selects, and phis (to a small depth). The
dominating branch conditions on the value narrow it: an edge into a block with
one predecessor says `v op k` held, including each comparison under an
`&&` taken or an `||` not taken. In
`if (var <= 0 || (unsigned)(var - 1) < UINT_MAX)` the second comparison runs
only when `var` is in [1, INT_MAX], so it is always true (gcc-torture
20041114-1). Under `v != k`, `v == k` is false. A comparison of two values
is also decided by a dominating comparison of the same two: under `x == y`,
`x != y` is false and `x <= y` true; under `x < y`, `y < x` is false
(gcc-torture compare-3). A signed order says nothing of the unsigned one.

Ranges are i128 intervals in the interpretation (width and signedness) of a
value's own type, for integer types up to 64 bits. A range is read at another
interpretation only when that reads no bits above the value's type and every
number in it means the same there; a fact from a comparison at the value's
width but the other signedness is mapped across the sign boundary. Signed
overflow is never assumed away: arithmetic whose exact result leaves the type
gives no range. Folded comparisons become constants, which constant_fold,
copy_prop and cfg_simplify carry on to the branches they feed.

A signed `x > 0` or `x <= 0` that the ranges leave open, but whose x is never
negative there (a u8 or u16 field promoted to int, a mask, a value under
`x >= 0`), becomes `x != 0` or `x == 0`: a `cbnz` or `cbz` on AArch64
instead of `cmp` and `b.gt`/`b.le`. This saves 20M of SQLite speedtest's
instructions, most of them in sqlite3BtreeIndexMoveto and moveToRoot
(`pRoot->nCell > 0`).

Before that, a block that only tests one of its phis against a constant
loses the predecessors whose edge decides the test: a constant incoming
value, or one the predecessor's own branch just compared (`x >= 262` taken
decides `x >= 3`). Such an edge goes straight to the successor the test
picks, the successor's phis taking the predecessor's values. The phis may
then be read only in the block and under its other successor, when that one
has the block as its only predecessor (anywhere else, the new edge would
reach the read without the phi being set). This also runs right after
load_phi, whose phis of merged loads are what such a block tests: deflate_fast
tests `lookahead >= MIN_MATCH` where the `lookahead >= MIN_LOOKAHEAD` path
meets the one after fill_window, and `hash_head != NIL` after setting it to
NIL on the other path.

### loop_unroll -- Constant-Trip Loops Unrolled

Runs in the first iteration, after constant folding and range_fold, once
constants have reached loop bounds. A loop of a header (phis, then pure
instructions ending in the exit compare) and one straight-line body block
(no calls, atomics, inline asm or volatile accesses) that branches back is
unrolled in full when running it on its constants decides the exit at
every check, within 16 iterations and 600 instructions in all (copies,
casts and symbol addresses not counted). The trip count comes from
evaluating the header and body on the constants the phis start with, so an
int counter compared and stepped at 64 bits through casts is followed too.
The header then sets each phi to its starting value and branches into the
body block, which holds one copy of the header's instructions and the body
per iteration, each reading what the previous copy gave the phis (through
copies), and branches to the exit; uses after the loop read the values after
the last copy. Copy propagation, constant folding and GVN clean up.

zlib's braided CRC steps through the bytes of each word with
`for (k = 1; k < W; k++)`: a variable shift and table row per byte and a
counter. Unrolled, the shift and mask fold into `ubfx` and the row into the
address (see gep_reassoc). zbench's crc32 kernel retires 1.90G -> 1.31G
instructions (clang 1.24G).

### gvn -- Global Value Numbering

A dominator-based common subexpression elimination (CSE) pass with redundant
load elimination and store-to-load forwarding. Walks the dominator tree in
depth-first order, maintaining scoped hash tables that map expression keys to
previously computed values.

**Value-numbered expression types:**

- **BinOp** -- with commutative operand canonicalization (smaller value number
  first), so `a + b` and `b + a` receive the same value number.
- **UnaryOp** -- unary operations.
- **Cmp** -- comparisons (without commutative canonicalization).
- **Cast** -- type conversions keyed by source and destination type. Excluded
  between 128-bit and float types (library calls); integer extensions to and
  truncations from 128 bits are numbered, so the multiply-high that
  div_by_const writes out for both `x % 100` and `x / 100` is computed once.
- **GetElementPtr** -- base + offset address computations.
- **Load** -- redundant load elimination (see below). Excluded for float,
  long double, and 128-bit types.

**Redundant load elimination.** Two loads from the same pointer with the same
type produce the same value if no intervening memory modification occurs. A
pointer is keyed as its root (before a chain of constant-offset GEPs) and the
byte offset from it, so differently typed GEPs of one field are one location.
Load value numbers use a generation counter: any memory-clobbering instruction
(Store, Call, CallIndirect, Memcpy, atomic operations, InlineAsm, fences, etc.)
bumps the generation, instantly invalidating all cached load entries, except
that a store to a constant offset from a root carries over the loads and
forwardable stores at that root whose bytes it cannot overlap (another field
of the same struct: `s->bi_valid` read around a store to `s->bi_buf`). With
strict aliasing (see above), a store of a known alias class also carries over
the latest entries of the other known classes (at most 16 of each, found
through a per-class list kept like the per-root ones); an entry carried over
that way then serves only loads of its own class, since a byte access, say,
may read what the store wrote. At block merge points (blocks with >1
predecessor), the generation is also bumped to conservatively invalidate
inherited load entries.

**Store-to-load forwarding.** When a Store writes value V to pointer P, a
subsequent Load from P (matching value number and type, same generation) is
replaced with `Copy(V)`. This is tracked with the same generation scheme as
load CSE. Forwarding is disabled for escaped parameter allocas to preserve
correctness of the backend's ParamRef optimization.

**Scoped hash tables.** The pass maintains five rollback logs (pure expressions,
load entries, store-forwarding entries, value number assignments, and the
per-root lists of load and store entries a store to another field keeps, each
logged once per scope that changes it, so a sibling subtree sees what its
parent recorded). On
entering a dominator subtree, the current log positions are saved. On
backtracking, all entries beyond the saved position are undone, giving scoped
hash-table semantics without cloning. This is the same scoping pattern used by
mem2reg's SSA rename phase.

### licm -- Loop-Invariant Code Motion

Identifies natural loops in the CFG and hoists loop-invariant instructions to
preheader blocks that execute before the loop. An instruction is loop-invariant
if all its operands are constants, defined outside the loop, or themselves
defined by loop-invariant instructions (computed to a fixpoint).

Loops are processed innermost-first (sorted by body size). Each loop must have a
single-entry preheader block; loops with multiple outside predecessors are
skipped. Hoisted instructions are topologically sorted (Kahn's algorithm) before
insertion into the preheader to ensure def-before-use ordering.

**Safety rules:**

- **Pure instructions** (arithmetic, casts, GEP, Copy, GlobalAddr, Select, pure
  Intrinsics) are always hoistable. Division and remainder are excluded because
  they can trap (SIGFPE on divide-by-zero) and must not be speculatively
  executed.
- **Loads from non-address-taken allocas** are hoistable when the alloca is not
  stored to inside the loop body.
- **Loads from GlobalAddr pointers** are hoistable when the loop contains no
  calls, no stores to any global-derived pointer, and no direct stores to that
  specific global.
- **Loads from address-taken allocas** are never hoistable because stores
  through derived pointers may not be tracked.
- **All other loads** (runtime-computed pointers) are conservatively rejected
  since there is no alias analysis.

**Constant locals.** A local whose only writes are constant stores in one loop
block, and whose address is only loaded from, read by vector ops or copied
from (a vector literal such as `(double2){ 0.5, 1.5 }` or a constant table in
a loop body), holds the same bytes every iteration: its alloca, the addresses
into it and the stores move to the end of the entry block. A local whose writes
are all in the entry block is invariant in every loop, so in later rounds a
whole copy of one, or a single vector op reading only such locals, constants
and entry values (e.g. `-lim` of a hoisted literal), moves there too.

**Split addresses.** An address `gep p, (inv + var)` in a loop, where `p` and
`inv` come from outside it (`p[j + i]` in a loop over `i`), becomes `gep (gep
p, inv), var` when the add has no other use, so `p + j` is hoisted and each
access indexes it by `i` (`ldrb w0, [x1, x2]`).

**Rare global addresses.** A global's address hoisted out of a loop that
makes calls lives through them, in a callee-saved register or, once those
run out, in a stack slot stored on every entry to the loop. In a loop
computing the addresses of more than three distinct globals, one whose
computing blocks' estimated frequencies (`liveness::compute_block_freq`) add
up to less than 0.3 of the header's stays where it is used.
`sqlite3_str_vappendf`'s format loop had stored 23 string literals and
tables, most used by one conversion each, on every call; SQLite's speedtest
ran 0.5% fewer instructions once they stayed. A loop computing at most
three stays with less than 0.125: a few addresses usually fit in registers
(`deflate_slow`'s length and distance code tables, an estimated 0.17, ran
6% more instructions left in its loop), but a loop with none to spare
spilled even one, as `sqlite3VdbeRecordCompareWithSkip` did with
`sqlite3SmallTypeSizes` (0.06); speedtest ran 0.2% fewer instructions.

### iv_strength_reduce -- Induction Variable Strength Reduction

Transforms expensive per-iteration index computations in loops into cheaper
pointer increment operations. For a typical array access pattern:

```c
for (int i = 0; i < n; i++) sum += arr[i];
```

The IR computes `base + i * sizeof(int)` every iteration via a cast, shift (or
multiply), and GEP. After strength reduction, the loop maintains a running
pointer that is incremented by `sizeof(int)` each iteration, eliminating the
per-iteration multiply and cast.

**Finding induction variables.** The pass scans phi nodes in the loop header for
basic induction variables: `%iv = phi(init, %iv_next)` where
`%iv_next = %iv + constant_step`. Supports looking through Cast and Copy chains
to find the back-edge increment. Only single-latch loops (exactly one back-edge
block) are supported.

**Finding derived expressions.** Once basic IVs are identified, the pass looks
for multiplications or shifts by constants where one operand derives from a
basic IV (following Cast and Copy chains for up to 3 propagation passes). These
derived expressions that feed into GEP instructions become candidates for
strength reduction.

**Applying the transformation.** For each eligible GEP, the pass creates a new
pointer induction variable (a phi in the loop header) that starts at the initial
pointer and increments by `iv.step * stride` bytes each iteration. The original
GEP is replaced with a copy of the new pointer IV. The dead original
computations are removed by subsequent DCE.

**Limits:**

| Limit | Value | Purpose |
|---|---|---|
| Max stride | 1024 bytes | Avoids unusual access patterns |
| Max cast chain depth | 10 | Prevents pathological chain following |
| Shift amount range | 0..64 | Validates shift amounts before computing stride |

Runs only during the first iteration of the main loop. Uses shared CFG analysis
with GVN and LICM.

### mem_inline -- Small Memory Calls In Place

Runs once after the main loop. A `memcpy` call with a constant length of at
most 128 bytes becomes the IR's Memcpy (loads and stores in 32-, 16-, 8- and
smaller chunks), and a `memset` one becomes 8-byte stores of the byte
repeated, then 4-, 2- and 1-byte ones (AArch64 and x86-64 only, where
unaligned stores are fine). Lowering already does this for the fortified
`__builtin___memcpy_chk` calls the system headers make of every memcpy,
when the check cannot fail (the object size is unknown, or a constant length
fits) and the length is constant; this catches lengths that became constant
during optimization (a size computed in an inlined helper).

On the same targets, a copy of 1, 2, 4 or 8 bytes, as a call or as a Memcpy
lowering made (a small struct assigned), becomes one load and one store of
that width. To the register allocator a Memcpy is a call point (its
codegen uses scratch registers), so every value live across one kept out of
the caller-saved registers: inflate_table copies its 4-byte `code` entries
into the table in its fill loop, and most of its values lived on the stack.
It retired 47M instructions in zbench's inflate run, now 36M (clang 35M).

Before the first mem2reg (on the same targets), a copy of exactly a scalar
local's size into or out of it, as a Memcpy or a plain `memcpy` call with a
constant length whose result is unused, becomes a load and a store of the
local's own type (`memcpy(&value, p, 4)`, an unaligned read). The casts and
zero-offset GEPs lowering puts between the call and `&value` are looked
through and removed when nothing else reads them, so mem2reg then promotes
the local: zlib's hash insertion had written each word to the stack and read
it back. A `double` read this way stays a double.

### load_phi -- Loads Joined Across Merges

Runs once after mem_inline. GVN reuses a load or a stored value only along
dominator-tree paths and forgets everything at a merge, so a field both arms
of an if store (zlib's send_bits stores `s->bi_valid` on either path) is
loaded again after it. Here each load's value is looked for walking backward:
in its block, a store to the same location (root pointer, constant byte
offset and type) gives the stored value, and a load of it its result; at the
top of a block with one predecessor the walk continues at that block's end,
and a block with several gets a phi of the value each holds at its end, when
all do (no phi when they all hold the same value). Anything that may write
the location stops the walk: a store through another root or overlapping the
bytes (unless strict aliasing says a store of its class cannot write what the
load reads), a call mod_ref cannot show writes nothing, a memcpy, atomics,
inline asm. A loop header's own back edge can take part (a field never written in
the loop is the value from before it); a walk that comes back to a block it is
still answering for gives up. At most 4096 blocks are entered per function and
256 instructions looked at in each. compress_block keeps `bi_valid` in a
register from one send to the next instead of loading it in each.

A phi of an 8- or 16-bit value would need extending at each use, which its
load does for free, so such loads are taken only when every use is an
extension to one 32- or 64-bit type whose uses read only the loaded bits
(narrow's low-bits check, which also follows right shifts by a constant).
Then any wider value with those low bits will do: the walk looks for stores of
truncations (or constants) and joins the values truncated, and the extensions
read them, high bits unmasked. A truncation from the other width is cast to it
right after it. zlib's 16-bit `bi_buf` is or-ed into, truncated and stored, or
shifted down 8 for its high byte, so it too stays in a register between sends,
as clang keeps it.

### Unread Local Stores (Phase 9d)

A local whose address only reached a memcpy can be left with stores and no
loads: mem2reg ran while a copy out of the middle of it (SQLite's
OP_MakeRecord, `memcpy(zPayload, (u8*)&v + 8 - len, len)` on a branch dead
from the start) still took its address, mem_inline turned the copy into it
(`memcpy(&v, &pRec->u.r, 8)`) into a store, and load_phi forwarded every
load. `dce::eliminate_unread_local_stores` removes the stores to allocas used
only as plain stores' addresses (not parameter allocas). Disabled with
`CCC_DISABLE_PASSES=unread_locals`.

### bound_hoist -- Loop Bound Hoisting

Runs once after the main loop, before iv_widen. iv_widen and lftr need a
loop's bound computed outside the loop, but a bound read from memory every
iteration (`for (i = 0; i < l.n; i++)`, with `&l` passed to a call before the
loop) stays in the header when LICM cannot show nothing writes it, and
loop_hoist moves such loads only after rotation. Here the instructions in the
header that compute an operand of its exit test (casts, constant GEPs and
loads, at most four) move to the preheader when each load reads memory nothing
in the loop may write, as mod_ref and its alias model answer for calls and
stores, and follows only instructions sure to continue. The preheader must
end in a plain branch to the header: a test that branches straight to the
loop (`if (p) for (i = 0; i < p->n; i++)`, the counter's start being a phi
constant) is the loop's only way in, but loads moved there would also run
when it skips the loop, reading through a null `p` (SQLite's
heightOfExprList). The header runs on every entry to the loop, so they then
run on the same paths as before, once.

### iv_widen -- Induction Variable Widening

Runs once after the main loop, on 64-bit targets. A signed `int` loop counter
stepped by a constant is kept as a 64-bit value equal to its sign extension
(signed overflow being undefined), so the step needs no `sxtw` and indexing
needs no extension. Compares against constants or values available before
the loop are widened; other uses read an explicit truncation, one per block
before its first such use (for a phi, at the end of the predecessor), so no
narrow copy stays live through the loop. A counter carried from one loop to
the next, as a partition's `i` is across its swaps, is truncated where the
first scan is left and extended where the next starts: the extension of a
truncation is the wide value itself. Extensions go at the end of a preheader
that only enters the loop, else right after the value's definition.
Disabled by `-fwrapv`.

An `unsigned` counter, whose wrapping is defined, is widened (to its zero
extension) only when it cannot wrap: it starts at a constant below 2^32 - 1,
steps by 1, and the loop's one latch goes back only while `next <u B` for a
32-bit B, so the counter stays below 2^32 - 1 and `next` at most 2^32 - 1.
B may change in the loop (it is zero-extended where it is defined:
compress_block's `sx < s->sym_next` reloads the bound). Unsigned and
equality compares widen; signed ones read a truncation. It is done only when
it removes more zero extensions than it adds truncations.

### lftr -- Linear Function Test Replacement

Runs after iv_widen, under the same conditions. When a loop's counter is only
stepped and tested (strength reduction having moved the addressing to a
pointer), the exit test compares that pointer against its end address instead
(computed in the preheader) and the counter goes away. When the counter is also
shifted to index memory, the shifted value becomes the induction variable,
pointers stepping by the same amount from offset 0 become `base + offset`,
and one register indexes every array (`[base, offset]` on AArch64). The bound
and start must be small constants or sign-extended 32-bit values, so the
scaled bound cannot overflow; pointer tests are signed, or unsigned when
constant bounds show the loop runs.

### loop_vectorize -- In-Place Loop Vectorization

Runs once after lftr, for AArch64. A single-block loop counted down to zero
(`do ... while (--n)`) that loads one element through a pointer stepping by
the element size, stores one back to the same place, and does nothing else
but compute the stored value as one lane operation of the element and a
loop-invariant value gets a vector loop in front of it: while 16 bytes'
worth of iterations remain, one `VecOp` reads, operates on and writes 16
bytes, and the original loop does what is left. The operations are add,
subtract, multiply (lanes below 8 bytes), and, or and xor, computed in any
type at least as wide as the element (only the low bits reach memory), the
invariant on either side, and `x >= w ? x - w : 0` on the zero-extended
element, a saturating subtract of w clamped to the element's maximum. That
is zlib's slide_hash, which shifts the hash chains each time the window
slides (`uqsub v0.8h`, eight entries per step instead of one).

In front of that, while four vectors' worth remain, a loop does four
`VecOp`s per step (64 bytes), sharing one counter update and branch; then
the single-vector loop, then the original one if anything is left (a block
tests for that, since the original loop runs at least once). With the
backend's broadcast hoisting and offset folding, slide_hash's step is
`ldur`, `uqsub`, `stur` per 16 bytes: deflate at level 1 retires 62M (2%)
fewer instructions, though it is no faster, the slide being memory-bound.

### pressure_hoist -- Pressure Hoisting

Runs once after lftr. A pure, non-trapping instruction that is the only use of
two or more values moves to the end of the block defining its last operand
(before a final compare the branch tests), when its own block post-dominates
that one and the two are not in a loop together, so it still runs once per
run of that block. Its operands then die there instead of staying live down to
it (`promo ? promo : type` computed in a move's first block instead of its
last).

### gep_linear -- GEP Offset Cancellation

Runs once after lftr. The offsets of a GEP chain are summed as a linear
combination of values, expanding I64 adds, subtracts, and multiplies and
left shifts by constants (exact modulo 2^64; casts and everything else are
opaque values). When fewer values are left than the chain reads, and at
most one, the GEP is rebuilt from the chain's root as `gep root, v * k` plus
a constant. `p->pieces[pc / 6][pc % 6]` over rows of six 8-byte elements is
`48 q + 8 (pc - 6 q)` once the division is expanded, which is `8 pc`: the
remainder and its multiply go away.

### gep_reassoc -- GEP Offset Reassociation

Runs once after pressure_hoist. `gep(gep(p, C), i)` whose only uses are load and
store addresses becomes `gep(gep(p, i), C)`: the backend folds the constant
into each access (`[t, #120]`) instead of keeping the field address `p + 120`
in a register for every `p->board[sq]`. GVN has already run, so a GEP equal to
one in a dominating block (the `rays + sq` of each ray) then becomes a copy of it.

Before that, `gep(p, (x + C) << k)` or `gep(p, x + C)`, whose index is read
only there and whose address only by loads, stores and constant GEPs that
are, becomes `gep(gep(p, x << k), C << k)`, and those constant GEPs re-base
onto the inner one: `tree[code + 257].Len` loads `[t, #1030]` after `add t,
p, x, lsl #2` instead of adding 257 to the index first.

On AArch64 an access takes a constant offset or a scaled index, not both, so
the first rewrite trades the constant GEP for an add at each access; that
pays only when the constant GEP would otherwise sit in a register across
code that does not use it. Two or more variable GEPs in one block through
one constant GEP keep it as their base, rebuilt in that block when it is
defined elsewhere (LICM hoists it): the braided CRC's table rows
(`crc_braid_table[k] + 4 * byte`, five bytes a row) take one add per row
instead of one per byte.

Last of the pipeline on AArch64 (after remat, which may recompute such
GEPs in a loop), a constant GEP whose offset no add immediate holds (12
bits, optionally shifted by 12) takes its base from an earlier constant GEP
of the same base in its block whose offset is within one: the CRC's rows 5
to 7, at 5120 to 7168 bytes, were a movz and an add each every iteration,
and are each the row before plus 1024. (Row 4, at 4096, is one
`add x22, x12, #1, lsl #12`.)

### load_group -- Next Words' Loads Next to Each Other

Runs after addr_sink, on AArch64. A load of the 4- or 8-byte word after one
an earlier load in its block read (`[p, #k + size]` after `[p, #k]`, the
address the base itself or a constant GEP of it) moves up next to it, with
its address, past at most four instructions none of which may write memory
(loads, arithmetic, compares, casts, copies, GEPs, selects). The peephole
then pairs the two into an `ldp`. Each load joins one pair, as an ldp takes
two. The braided CRC loaded its five words each next to the xor of the one
before, all into one register, so none could pair; now they are two `ldp`s
and an `ldr`, and its loop runs 133 instructions per 40 bytes (clang 135).
Moved further, loads held registers longer and SQLite's speedtest ran 25M
more instructions.

### exit_addr -- Loop Exit Addresses

Runs once after gep_reassoc, on AArch64. The backend folds an address into a
load or store (`ldr w0, [x1, x2, lsl #2]`) only at its last use. An address a
loop computes for its own accesses and also uses after the loop (a
partition's scan `do i++; while (a[i] < p)` and then the swap of `a[i]`) is
recomputed, with a single-use scaled index, in each block after the loop that
uses it, so inside the loop it folds and the loop loses an `add` per
iteration. Only addresses whose every use in their innermost loop is an
access the peephole folds (index scaled by the access size, or not at all),
and that no phi reads.

### edge_addr -- Edge Addresses

Runs once after exit_addr, on 64-bit targets. An address `base + k` that its
block only loads or stores through, and that a phi also reads on an edge
from that block, is recomputed on the edge instead: in a new block on a
critical edge (where phi elimination would have put the copy anyway), or at
the end of a block with one successor. The block's access then folds the
address (`ldrb w0, [x1, #3]`). A match loop that stops at the first
differing byte (`while (*++scan == *++match && ...)`, zlib's longest_match)
leaves with the address it reached, so each step's address had a register
of its own: now each byte compared is two loads, a compare and a branch.
Chains of constant offsets are first rebased on their first base (`(s + 2)
+ 4` is `s + 6`). Pointer increments are lowered as integer adds; one used
only as access addresses becomes a GEP, which the backend folds the same way.

### remat -- Leaf Rematerialization

Runs once after exit_addr, on AArch64 leaf functions (no calls, nothing
that shrinks the backend's leaf register pool). Where a block outside any
loop has more values live than the caller-saved registers a leaf allocates
(x10-x15 and x3-x8 above the arguments), a value live through the peak only
to be read later in the block, and computed by at most four cheap
instructions from values still live at that read, is recomputed right before
it (`p + us * 48` in make_move's castling block, merged by GVN with the
same address in an earlier inlined helper). The pressure count follows the
allocator: a constant-offset GEP used only as an address holds its base's
register, operands dying at an instruction still hold theirs, except that a
simple instruction's result may take one. All or nothing: the changes stay
only if every block then fits, since otherwise the function saves a
callee-saved register (and builds a frame) anyway.

### ret_dup -- Return Tail Duplication

Runs once after remat. A return block with phis, at most two pure
instructions and at most eight predecessors (an `a || b || c` returned from
a function: a phi of 1 from each early exit and the last test's result,
cast to int) gets a copy for each predecessor that passes constants into
every phi, with the phis replaced by them. Constant folding then turns each
copy into `ret 1`, so an exit no longer sets a register, jumps, and extends
it; when one predecessor is left, CFG simplification merges the block into
it. Predecessors through a switch, or on both edges of a conditional branch,
keep the shared block.

### phi_cmp -- Flags Compared Where Paths Meet

Runs after ret_dup. A phi that only its own block's conditional branch
reads, each of whose incoming values is a compare of one kind and type
(integer) that only the phi reads, becomes that compare of two new phis of
the compares' operands (an operand all the paths share, the same value or
constant, needs no phi). deflate_fast's `bflush`, set by `_tr_tally_lit` on
the literal path and by `_tr_tally_dist` on the match path and tested once
both meet, was materialized with `cset` on each path and tested with
`cbz`; compared at the merge, the compare feeds the branch: about 6M fewer
instructions at deflate level 1.

### step_sink -- Counter Steps After Their Last Use

Runs after ext_elide. `p[i++]` steps the counter before the old
value is read, so the two are live at once: phi elimination's copy on the
back edge needs a register of its own and, when the latch branches
conditionally, a block of its own (a move and a jump every iteration). A
phi's incoming value that is an add or subtract of the phi and a constant in
the phi's block moves as late as it can: before its first read in the block,
or to the end (before a compare the branch tests), which must be after the
phi's other uses, all in that block. It then takes the counter's register.
(Right after the counter's last use, an address computed from it would no
longer fold into its access.) compress_block's loop goes straight back to its
header.

The step may also be in the latch, the block the incoming edge comes from,
when the counter is not read after the latch before the header defines it
again: in no block reached from the latch without passing the header, not
by the latch's terminator, and not by a phi taking it from the latch or
such a block. Its uses in the latch are then the ones the step moves below.
zbench's `data[i++] = (unsigned char)*p`, tested at the head of the
unrotated loop and stored in its latch, kept the old counter in a register
of its own each character. ext_elide runs first, with copy propagation
after it, so the counter's elided extension for the index is the counter
itself.

### loop_rotate -- Loop Rotation

Runs after phi elimination, just before codegen. A latch that branches back
unconditionally to a small header (at most 6 simple instructions ending in a
conditional branch) gets a copy of the header, so each iteration ends with one
conditional branch and the original header is left as the entry guard. Header
values used outside the header become variables assigned by a Copy in both
places, as lowered phis are. A one-block loop rotates into its back edge's
trampoline, which phi elimination laid out before the header; that block,
now a loop of its own, moves right after the header, so entering the loop
falls into it.

loop_hoist then moves loads every iteration makes to the guard's edge into
the loop. GVN has run by then, so a load the guard already makes (with
nothing since that may write memory) is reused by a copy instead:
longest_match loaded `s->window` before its loop for the scan pointer and
again for the hoisted `s->window + cur_match`. With strict aliasing, stores in
the loop of another alias class do not hold a load back: deflate's hash
insertion loop stores to the `ush` arrays `s->head` and `s->prev`, and
`s->window`, `s->hash_mask`, `s->w_mask`, `s->head` and `s->prev` are loaded
once before it.

loop_promote (loop_promote.rs) then keeps in a register a field of a local
whose address escapes, which the loop both reads and writes: SQLite's
balance_nonroot gathers cells with `b.apCell[b.nCell] = ...; b.nCell++`, and
`&b` is passed to other functions, so `b.nCell` was loaded and stored on
every iteration. When the loop makes no calls and no memory access other
than plain loads and stores, and none of those that may reach the field (by
the alias model of mod_ref: another local, a global or a parameter's object
cannot; another byte range of the same local cannot) is of an alias class
that may alias its own, the field is loaded once on the guard's edge into
the loop, each load of it in the loop becomes a copy of that value and each
store a copy into it (a variable of several definitions, as lowered phis
are), and it is stored back on every edge leaving the loop (in a new block
when the edge's target has other predecessors). Every access to the field
must have its type and every stored value that type, so that a copy changes
nothing. Speedtest ran 0.16% fewer instructions; the loop's index is then
sign-extended each iteration rather than loaded with `ldrsw`.

### ext_elide -- Extensions Loads Already Made

Runs after phi_cmp, on AArch64, followed by copy propagation and dead code
elimination where it changed anything. `ldrb`/`ldrh` zero-extend and
`ldrsb`/`ldrsh` sign-extend to 64 bits, so an 8- or 16-bit value loaded is
already extended its own way in its register. The backend sees that when the
extension follows the load, not when the value went through a phi:
longest_match's `scan_end` bytes, loaded before the loop and again when a
longer match is found, were masked with `and #0xff` at each candidate. A value
counts as extended when it is such a load, a copy of one, or a phi all of
whose incoming values are (or constants in its type's range), and its
widening by its own type becomes a copy. It runs late because earlier passes
may replace a load with another value of its type (GVN forwards stored
values, which an 8- or 16-bit operation may leave with unspecified high
bits). With the other longest_match changes, deflate at levels 6 and 9,
where the candidate loop dominates, retires about 10% fewer instructions.

32-bit loads count too (`ldrsw` for int, `ldr w` for unsigned), and so does
a U32 operation, which a 32-bit instruction computes with the high half
cleared. compress_block's `bi_valid`, loaded as int and widened for
`bi_valid += len - 16`, and `dist - 1`, widened as a table index, each got a
register and a `mov` of their own. (The register allocator puts such a copy
and its source in one register even where both are live: each is defined
once, so they hold the same bits.) So does a narrowing cast, which extends its
result its own way (`and #0xffff`, `sxth`, `mov w0, w0`, `sxtw`): deflate's
`(Pos)strstart`, then widened as an index into the window, was moved to a
second register at each hash insertion. A 64-bit value shifted right by 32
or more, or masked with a constant below 2^32, is zero-extended from the
bits left (zbench's xorshift `(unsigned)(rng >> 32)`, widened again for
`% nw`). A zero extension from fewer bits than the widening's unsigned
source type has (or than a signed one's sign bit) serves it too, and a mask
by 0xff, 0xffff or 0xffffffff of a value zero-extended within it becomes a
copy (division by a constant masks its 32-bit dividend that way).

Then an int add, subtract, multiply, logical operation or left shift whose
uses, through copies and phis, read only its low 31 bits (narrower casts and
stores, and operations that are themselves such) becomes U32: the backend
keeps an int sign-extended with a `sxtw` after each operation, which no such
use reads. compress_block's `s->bi_buf |= (ush)value << s->bi_valid`, an int
shift and or for a 16-bit store and its two bytes, had one at each send.

Last, a narrowing integer cast becomes a copy when every use reads only the
bits it keeps: narrower stores and casts, widenings that extend from at most
those bits (and so mask again), and through copies, phis and adds,
subtracts, multiplies, logical operations and left shifts (whose low bits
depend only on their operands' low bits, whatever width they are computed
in). deflate_fast's `(ush)(strstart - match_start)`, stored as 16 bits and
decremented in 16 bits for d_code's widening, was masked with
`and #0xffff` for neither.

### addr_sink -- Addresses Down to Their Access

Runs after step_sink, on AArch64. `p[i] = expr` computes the address before
the value, so `p` and `i` die at the GEP and the value may take one of their
registers; the backend then can't fold the add into the store as `[p, i]`. A
GEP with a variable offset whose only use is a load or store at most 16
instructions later in its block (with no call between) moves to just before
it, and a doubled or shifted index read only by it (`x + x`, `x << k`) moves
to just before it in turn, so the peephole folds the scaling into the access
(`[x14, x13, lsl #1]`) instead of the scaled index taking a register of its
own across unrelated code. compress_block's second byte of each flush,
`s->pending_buf[s->pending++] = (Byte)(s->bi_buf >> 8)`, was an `add` and a
store. With the ext_elide changes, compress_block at deflate level 1 went
from 21M instructions more than clang's to 3M fewer.

A shift by a constant whose only use is an add, subtract (as its second
operand) or logical operation of its width, at most 16 instructions later
with no call between, moves to just before that use as well. Next to it, the
shift's source is still live there and the peephole shifts the operand
(`and x12, x14, x12, lsr #16`, deflate's hash update); apart, the source's
register may be reused in between, and the shift needs its own instruction.

### latch_dup -- Latch Copies

Runs after loop_layout (before step_to_load). A latch of at most four simple
instructions, one of them a load, that branches back conditionally (to the
header or the block holding its phi copies), and whose values are used only
in it, is copied into each loop block that jumps to it (at most eight; the
block laid out just before it keeps falling into it), so each path ends with
the loop's test instead of a jump to it. compress_block's literal and match
paths each jumped to the shared `sx < s->sym_next` test. A latch that only
compares values the path just assigned is left alone: its copies read the
value through an extra move, and SQLite's searches ran more instructions.

### loop_layout -- Loop Tail Placement

Runs last, after loop_rotate and loop_hoist. A bottom-tested loop laid out
header first ends with the latch's branch back to the header, taken every
iteration. When the run of loop blocks ending at the latch (each falling into
the next) begins with a block nothing falls into, as for a tail reached by
`continue` or an `x ? a : b` arm the other side jumps past, that run moves in
front of the header: the latch falls into the header and exits by a jump.
Each path through the loop takes one jump less per iteration; the loop's entry
and exit take one more each. This is the layout clang gives chess-perft's move
loop, and made it 0.5% faster.

### step_to_load -- Pointer Step After Its Load

Runs after loop_layout. A pointer stepped by a constant in the latch
(`%q = gep %p, c` or a constant add or sub, then `%p = copy %q`) and read in
the loop only by one load in the header has its step moved to right after
that load. The header may be the latch itself, if the load comes first. The AArch64 peephole then
folds the load and the add into one post-indexed access
(`ldr w1, [x20], #4`), so chess-perft's move loop takes three instructions per
iteration, as clang's does. It applies only when nothing else reads %p or %q
once the loop has started, except reads of %p after the copy in the latch,
and when nothing else in the loop defines either. A one-block `while (p < e)
s = s * 3 + *p++;` loop drops from eight instructions to six. Measured no
speedup on chess-perft.

### const_hoist -- Loop Constants Built Once

Runs last, after step_to_load, where no copy propagation folds constants
back in. A 64-bit constant used in a loop that takes three or more
instructions to build (three or more 16-bit pieces that are neither 0 nor
0xffff; on AArch64 a movz and two or three movks) is defined once by a copy
at the end of the entry block, and its loop uses read that value. Operands
of BinOps, Cmps, Stores and Copies qualify, except shift amounts and masks
that AArch64 encodes in and/orr/eor. On AArch64 a nonzero constant on the left
of a subtract or shift also qualifies (`16 - len`, `1 << n`): it has no
reversed forms taking an immediate, so the constant takes a `mov` at each use.
On AArch64 a constant in a 32-bit operation also qualifies
when both its 16-bit halves differ from 0 and 0xffff (a `movz` and a `movk`)
and the instruction cannot encode it (no add or subtract immediate, no
logical bitmask): deflate's hash multiplies by 66521 and adds it, rebuilt at
every string inserted. So does a constant multiplied or divided by that
takes a movz and a movk or more (AArch64 multiplies and divides take no
immediate): zbench's word choice `r % nw` multiplies by 0x38e38e39 for each
word. (With every nonzero constant, which a single `mov` builds, SQLite's
speedtest ran 4% more instructions.) At most four constants per
function, most loop uses first, a use counted by the instructions building
the constant there takes (three for a costly one, otherwise a movz or movn
and a movk per further piece). A cheap constant needs two loop uses: read once, it saves one
`mov` but holds a register through the function, and longest_match lost
more to the allocation around `MAX_MATCH - len` than it saved. The splitmix64 steps in scalar-bench's hash kernel
drop sixteen instructions per operation, about 20% faster. On AArch64 the
magic multiplier of a division by a 64-bit constant qualifies too, as its
64 bits, where div_by_const::mul_high_chains finds the multiply-high the
backend computes with one umulh or smulh reading it from a register (the
generic 128-bit multiply never sees the 64-bit value), in functions of at
most 500 instructions: in sqlite3_str_vappendf (1721) the magics went to
the stack at entry, eight instructions on every call for divisions in loops
it seldom runs.

### if_convert -- If-Conversion

Converts simple diamond and triangle branch-and-phi patterns into `Select`
instructions, which lower to conditional moves (`cmov` on x86, `csel` on
AArch64).

**Diamond pattern:**

```
pred:
    condbranch %cond, true_block, false_block
true_block:                          false_block:
    (0-8 side-effect-free instrs)       (0-8 side-effect-free instrs)
    branch merge                        branch merge
merge:
    %result = phi [true_val, true_block], [false_val, false_block]
```

**Triangle pattern** (one arm falls through to merge):

```
pred:
    condbranch %cond, arm, merge
arm:
    (0-8 side-effect-free instructions)
    branch merge
merge:
    %result = phi [arm_val, arm], [pred_val, pred]
```

Both patterns are rewritten to:

```
pred:
    (hoisted arm instructions)
    %result = select %cond, true_val, false_val
    branch merge
```

Each arm may contain up to 8 instructions (to accommodate C type system
patterns like Load + Cast chains for parameter loads and sign extensions that
inflate instruction count). Both arms must be side-effect-free: no stores,
calls, loads, atomics, division/remainder (can trap), or inline assembly.
Branches with already-constant conditions are skipped (deferred to
cfg_simplify). F128 and I128 phi types are rejected. The pass iterates to a
fixpoint within each invocation, since converting one diamond may expose
another. Overlapping diamonds within a single iteration are detected and skipped
to avoid conflicts.

Arms that each end by loading the merged value (`n = k < n->key ? n->left :
n->right`), and converting it the same way if at all (`c ? p->s : q->s` of a
`short` field), are first rewritten to merge the two addresses and load once
in the merge block. One load still runs on either path, and the arms, now
load-free, convert: the field address is selected (`csel`) and loaded, with
no branch to mispredict on random keys. When the two addresses are offsets
from one base defined before the arms (or the addresses of one global, which
each arm may still compute itself), the offsets merge instead and the merge
block adds them to the base: `d < 256 ? tab[d] : tab[256 + (d >> 7)]` selects
an index and loads `[base, index]`.

`if (c) return a; return b;`, a conditional branch to two blocks that only
return a value each (and that only it reaches), returns a select instead:
longest_match's `best_len <= s->lookahead ? best_len : s->lookahead` becomes a
`csel` rather than a branch to two copies of the epilogue. Two constants, and
a branch on a phi of its block (a step of an `a && b` test), are left to
ret_dup and branch threading.

One shape is left as branches: a step of an `a && b` or `a || b` test, whose
merge block holds only the boolean phi (with a constant from the
short-circuiting edge) and branches on it, or also phis that only its
successors' phis read on the edges from it (the counter of a loop condition
`m > limit && --n != 0`). cfg_simplify threads the constant's
edge to the branch target, giving one branch per operand instead of computing
the boolean with `cset`/`csel` and then branching on it. Dead code is removed
first, so copies and phi cycles that mem2reg leaves do not count as uses.

Likewise a diamond or triangle whose own branch tests such a phi (its
block's only instruction, its only use, with a constant incoming value):
pqdownheap's `if (j < s->heap_len && smaller(...)) j++` became a `cset` of
the test, two `cmp #0; cset ne` normalizations of it, and a `csel` of j;
threaded, each test branches to the increment or past it.

### ipcp -- Interprocedural Constant Propagation

An interprocedural pass that performs three optimizations across function
boundaries:

1. **Constant return propagation.** Identifies defined, non-weak, non-variadic,
   side-effect-free functions that return the same constant on every return path.
   Replaces all `Call` instructions to these functions with the constant value.
   Side-effect-free means no stores, calls, loads, atomics, inline asm, memcpy,
   or any va_arg operations.

2. **Dead call elimination.** Identifies defined, non-weak, side-effect-free,
   void-returning functions with no `Unreachable` terminators (traps are
   observable side effects). Removes entire `Call` instructions to these
   functions from all callers. This eliminates references to symbols that would
   otherwise cause linker errors.

3. **Constant argument propagation.** For static, defined, non-weak,
   non-variadic functions: when all call sites pass the same constant for a
   given parameter, replaces the `ParamRef` instruction in the function body
   with a copy of that constant. Functions whose address is taken (via
   `GlobalAddr` or global initializer references) have all parameters marked as
   varying since they may be called indirectly with unknown arguments.

IPCP runs at the end of every iteration (not just the first), because earlier
passes within the same iteration may simplify call arguments to constants (e.g.,
phi nodes collapsed after CFG simplification resolves dead branches). When IPCP
makes changes, the pipeline marks all functions dirty and always runs at least
one more iteration to allow cleanup passes to process the newly exposed
constants.

### dce -- Dead Code Elimination

Removes instructions whose results are never used by any other instruction or
terminator. Uses a use-count-based worklist algorithm with O(n) complexity:

1. Build use counts for all values in a single forward pass. Self-referencing
   phi edges (`v = phi [..., v, ...]`) are excluded from the count to prevent
   dead self-referencing phis from surviving.
2. Build a definition map from value IDs to their defining instruction
   locations, excluding side-effecting instructions.
3. Seed the worklist with non-side-effecting instructions that have zero use
   count.
4. Process the worklist: for each dead instruction, decrement its operands' use
   counts (saturating); if any operand's count drops to zero, add its defining
   instruction to the worklist.
5. Sweep all dead instructions in a single pass.

**Side-effecting instructions** are never removed regardless of whether their
result is used: Store, Call, CallIndirect, Alloca, DynAlloca, Memcpy, all atomic
operations (including Fence), InlineAsm, StackRestore, VaStart/VaEnd/VaCopy/
VaArg/VaArgStruct, multi-return helpers (GetReturn*Second, SetReturn*Second),
and Intrinsics that are non-pure or have a destination pointer (the store
through the destination pointer is a side effect even if the intrinsic
computation itself is pure). Alloca is conservatively kept because the backend
uses positional indexing for parameter mapping.

### dead_statics -- Dead Static Function and Global Elimination

Removes internal-linkage (`static`) functions and `static const` globals that
are unreachable from any externally visible symbol. Uses a BFS reachability
analysis:

**Roots** (seeds for BFS):
- All non-static defined functions and non-static defined globals
- Functions and globals with the `is_used` attribute
- All aliases (both the alias name and its target)
- All constructors and destructors
- Address-taken `static always_inline` functions
- Static symbols whose names appear in `toplevel_asm` (conservative check)
- Common globals

**References** are scanned from function bodies (`Call`, `GlobalAddr`,
`InlineAsm.input_symbols`) and global initializer trees. Any static symbol not
reached during BFS is removed from the module. After removal, the pass also
cleans up `module.symbol_attrs` to remove entries for unreferenced symbols.

### resolve_asm -- Inline Assembly Symbol Resolution

A post-inlining fixup pass that resolves symbolic references in inline assembly
operands. After inlining a function like `_static_cpu_has(u16 bit)` with a
constant argument, the IR may contain a `GlobalAddr` + `GEP` chain that
represents a specific offset into a global variable. This pass builds a
lightweight definition map (`DefInfo` enum tracking GlobalAddr, GEP, Add, and
Cast/Copy instructions) and traces those chains to produce symbol strings like
`"boot_cpu_data+74"` for the backend.

Only immediate-constraint ("i") operands with unresolved `input_symbols` entries
are processed. Non-constant GEP offsets cause resolution to fail gracefully.

### loop_analysis -- Shared Loop Analysis Utilities

A utility module (not a standalone pass) providing natural loop detection and
loop body computation used by both LICM and IVSR.

- **Back edge detection.** For each edge `tail -> header` in the CFG, checks
  if `header` dominates `tail` by walking the immediate dominator chain.
- **Loop body computation.** Seeds the body with the header. For each back edge,
  adds the tail and performs reverse BFS: adds all predecessors not already in
  the body, collecting all blocks that can reach the tail without going through
  the header.
- **Loop merging.** Multiple back edges targeting the same header produce
  separate loop entries; these are merged by taking the union of all bodies.
- **Preheader detection.** Finds the single predecessor of the header that is
  not in the loop body. Returns `None` if there is not exactly one such
  predecessor.

---

## Pass Dependency Graph

The following diagram shows the actual `should_run!` macro dependencies encoded
in `mod.rs`. An arrow from A to B means "B checks whether A made changes; if A
(or B itself) made no changes in the previous iteration, B is skipped."

```
                     +-> narrow -------> simplify -> constant_fold
                     |                      |    \       |
cfg_simplify1 -> copy_prop1 --------> gvn --+     +-----+
     ^               |                  |          |     |
     |               +--------> licm ---+     if_convert |
     |               |                         |   |     |
     |          constant_fold <------ copy_prop2 <-+     |
     |               |                  |                |
     |               v                  v                |
     +------------ cfg_simplify2 <---- dce <-------------+
```

**Upstream dependencies (encoded in `should_run!` macro):**

| Pass | Upstream dependencies |
|---|---|
| cfg_simplify (1st) | constant_fold, dce |
| copy_prop (1st) | cfg_simplify, gvn, licm, if_convert |
| narrow | copy_prop |
| simplify | copy_prop, narrow |
| constant_fold | copy_prop, narrow, simplify, if_convert, copy_prop (2nd) |
| gvn | cfg_simplify, copy_prop, simplify |
| licm | cfg_simplify, copy_prop, gvn |
| if_convert | cfg_simplify, constant_fold |
| copy_prop (2nd) | gvn, licm, if_convert + current-iter simplify, constant_fold |
| dce | gvn, licm, if_convert, copy_prop (2nd) |
| cfg_simplify (2nd) | constant_fold, if_convert, dce |

**IPCP** is not part of the `should_run!` system. When IPCP makes changes, it
marks all functions dirty, which forces all passes to re-run in the next
iteration via the dirty tracking mechanism rather than the per-pass skip logic.

---

## Disabling Individual Passes

Individual passes can be disabled at runtime for debugging by setting the
`CCC_DISABLE_PASSES` environment variable to a comma-separated list of pass
names:

```
CCC_DISABLE_PASSES=gvn,licm ./ccc input.c -o output.o
```

Recognized names: `all`, `inline`, `cfg`, `copyprop`, `narrow`, `simplify`,
`constfold`, `gvn`, `licm`, `ifconv`, `dce`, `ipcp`, `divconst`, `ivsr`,
`load_phi`, `unread_locals`, `bound_hoist`, `iv_widen`, `lftr`, `gep_linear`, `pressure_hoist`, `gep_reassoc`, `remat`, `ret_dup`, `loop_rotate`, `loop_hoist`, `loop_promote`, `loop_layout`, `step_to_load`, `range_fold`, `const_hoist`, `addr_arg_promote`, `edge_addr`, `loop_vectorize`, `step_sink`, `latch_dup`, `ext_elide`, `addr_sink`, `loop_unroll`, and `tbaa` (strict aliasing; see above).

Setting the variable to `all` skips the entire optimization pipeline.

Pass timing information can be enabled with:

```
CCC_TIME_PASSES=1 ./ccc input.c -o output.o
```

This prints per-pass, per-function timing and change counts to stderr, which is
useful for identifying performance bottlenecks and understanding convergence
behavior across iterations.

---

## Files

| File                     | Description                                            |
|--------------------------|--------------------------------------------------------|
| `mod.rs`                 | Pipeline orchestration, dirty tracking, shared analysis |
| `addr_arg_promote.rs`    | Locals passed by address to calls kept in registers    |
| `bound_hoist.rs`         | Invariant loads of loop exit-test bounds moved out     |
| `cfg_simplify.rs`        | CFG simplification (branch folding, jump threading)    |
| `const_hoist.rs`         | Large loop constants built once at function entry      |
| `constant_fold.rs`       | Constant expression evaluation at compile time         |
| `copy_prop.rs`           | Copy propagation with path compression                 |
| `dce.rs`                 | Dead code elimination (use-count worklist)              |
| `dead_statics.rs`        | Dead static function/global elimination (BFS)          |
| `div_by_const.rs`        | Division by constant strength reduction                |
| `ext_elide.rs`           | Extensions loads made, and unread sign extensions, dropped (AArch64) |
| `edge_addr.rs`           | Addresses a phi reads recomputed on the edge into it   |
| `exit_addr.rs`           | Loop addresses recomputed after the loop (AArch64)     |
| `gep_linear.rs`          | GEP chain offsets summed and cancelled (`a[x / n][x % n]`) |
| `gep_reassoc.rs`         | Constant GEP offsets moved into loads and stores       |
| `global_promote.rs`      | Static variables kept in registers through loops       |
| `gvn.rs`                 | Dominator-based GVN/CSE with load elimination          |
| `if_convert.rs`          | Diamond/triangle to Select conversion                  |
| `inline.rs`              | Function inlining with tiered size heuristics          |
| `ipcp.rs`                | Interprocedural constant propagation                   |
| `iv_strength_reduce.rs`  | Loop induction variable strength reduction             |
| `iv_widen.rs`            | Signed 32-bit loop counters kept as 64-bit values      |
| `lftr.rs`                | Exit tests moved off counters (one IV per loop)        |
| `load_phi.rs`            | Loads given the values their location holds at merges |
| `loop_unroll.rs`         | Small constant-trip loops unrolled in full             |
| `loop_vectorize.rs`      | In-place element loops done 16 bytes at a time (AArch64) |
| `mem_inline.rs`          | Small constant-length memcpy/memset done in place      |
| `licm.rs`                | Loop-invariant code motion                             |
| `loop_analysis.rs`       | Shared loop detection and body computation utilities   |
| `loop_layout.rs`         | Loop tails moved before the header (latch falls through) |
| `loop_promote.rs`        | Locals' fields kept in registers through call-free loops |
| `loop_rotate.rs`         | Header duplication into latches (bottom-tested loops)  |
| `narrow.rs`              | Integer narrowing (3-phase C promotion elimination)    |
| `pressure_hoist.rs`      | Instructions hoisted to end several live ranges early  |
| `range_fold.rs`          | Comparisons decided by value ranges and dominating branches |
| `remat.rs`               | Cheap values recomputed where a leaf runs out of registers |
| `ret_dup.rs`             | Return blocks copied into predecessors passing constants |
| `resolve_asm.rs`         | Post-inline assembly symbol resolution                 |
| `simplify.rs`            | Algebraic simplification, strength reduction, and peephole |
| `step_sink.rs`           | Counter steps after the counter's last use; addresses down to their access |
| `step_to_load.rs`        | Loop pointer steps moved after their load (post-index) |
