//! AArch64 peephole optimizer for assembly text.
//!
//! Operates on generated assembly text to eliminate redundant patterns from the
//! stack-based codegen. Lines are pre-parsed into `LineKind` enums so hot-path
//! pattern matching uses integer/enum comparisons instead of string parsing.
//!
//! ## Pass structure
//!
//! **Local passes** (iterative, up to 8 rounds): store/load elimination,
//! redundant branch removal, self-move elimination, move chain optimization,
//! branch-over-branch fusion, and move-immediate chain optimization.
//!
//! ## Optimizations
//!
//! 1. **Adjacent store/load elimination**: `str xN, [sp, #off]` followed by
//!    `ldr xN, [sp, #off]` — the load is redundant since the value is
//!    already in the register.
//!
//! 2. **Redundant branch elimination**: `b .LBBN` where `.LBBN:` is the
//!    immediately next non-empty line — falls through naturally.
//!
//! 3. **Self-move elimination**: `mov xN, xN` (64-bit) is a no-op.
//!    Note: `mov wN, wN` (32-bit) zeros upper 32 bits and is NOT eliminated.
//!
//! 4. **Move chain optimization**: `mov A, B; mov C, A` → `mov C, B`,
//!    enabling the first mov to become dead if A is unused.
//!
//! 5. **Branch-over-branch fusion**: `b.cc .Lskip; b .target; .Lskip:`
//!    → `b.!cc .target` (invert condition, eliminate skip label).
//!
//! 6. **Move-immediate chain**: `mov xN, #imm; mov xM, xN` where xN is a
//!    scratch register (x0-x15) → `mov xM, #imm` when safe.

// ── Line classification types ────────────────────────────────────────────────

/// Compact classification of an assembly line.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
enum LineKind {
    /// Deleted / blank
    Nop,
    /// `str xN/wN, [sp, #off]` — store to stack (via sp)
    StoreSp { reg: u8, offset: i32, is_word: bool },
    /// `ldr xN/wN, [sp, #off]` — load from stack (via sp)
    LoadSp { reg: u8, offset: i32, is_word: bool },
    /// `ldrsw xN, [sp, #off]` — load signed word from stack
    LoadswSp { reg: u8, offset: i32 },
    /// `ldrsb xN, [xM]` — load signed byte (general)
    LoadsbReg,
    /// `stp xN, xM, [sp, #off]` — store pair to stack
    StorePairSp,
    /// `ldp xN, xM, [sp, #off]` — load pair from stack
    LoadPairSp,
    /// `mov xN, xM` — register-to-register move.
    /// `is_32bit` indicates whether this is a w-register (32-bit) move.
    /// On AArch64, `mov wN, wM` zeros the upper 32 bits of the destination,
    /// so it is NOT equivalent to `mov xN, xM`.
    Move { dst: u8, src: u8, is_32bit: bool },
    /// `mov xN, #imm` — move immediate to register
    MoveImm { dst: u8 },
    /// `movz/movn xN, #imm` — move wide immediate
    MoveWide { dst: u8 },
    /// `sxtw xN, wM` — sign-extend word to doubleword
    Sxtw { dst: u8, src: u8 },
    /// `b .label` — unconditional branch
    Branch,
    /// `b.cc .label` — conditional branch
    CondBranch,
    /// `cbz/cbnz xN, .label` — compare and branch on (non-)zero
    CmpBranch,
    /// Label (`.LBBx:` etc.)
    Label,
    /// `ret`
    Ret,
    /// `bl func` — branch with link (function call)
    Call,
    /// `cmp` or `cmn` instruction
    Compare,
    /// `add`, `sub`, or other ALU instruction
    Alu,
    /// `str`/`ldr` to non-sp addresses (e.g., `str w1, [x9]`)
    MemOther,
    /// Assembler directive (`.section`, `.globl`, etc.)
    Directive,
    /// Any other instruction
    Other,
}

/// AArch64 register IDs for pattern matching.
/// We map x0-x30, sp, and w0-w30 to the same set (register number).
const REG_NONE: u8 = 255;

/// Parse an AArch64 register name to an internal ID (0-30, or special).
/// x0/w0 → 0, x1/w1 → 1, ..., x30/w30 → 30, sp → 31, xzr/wzr → 32.
fn parse_reg(name: &str) -> u8 {
    let name = name.trim();
    if let Some(n) = name.strip_prefix('x').or_else(|| name.strip_prefix('w')) {
        if let Ok(num) = n.parse::<u8>() {
            if num <= 30 {
                return num;
            }
        }
        if n == "zr" {
            return 32; // zero register
        }
    }
    if name == "sp" {
        return 31;
    }
    REG_NONE
}

/// Return the x-register name for a given ID.
fn xreg_name(id: u8) -> &'static str {
    match id {
        0 => "x0", 1 => "x1", 2 => "x2", 3 => "x3",
        4 => "x4", 5 => "x5", 6 => "x6", 7 => "x7",
        8 => "x8", 9 => "x9", 10 => "x10", 11 => "x11",
        12 => "x12", 13 => "x13", 14 => "x14", 15 => "x15",
        16 => "x16", 17 => "x17", 18 => "x18", 19 => "x19",
        20 => "x20", 21 => "x21", 22 => "x22", 23 => "x23",
        24 => "x24", 25 => "x25", 26 => "x26", 27 => "x27",
        28 => "x28", 29 => "x29", 30 => "x30",
        31 => "sp", 32 => "xzr",
        _ => "??",
    }
}

/// Return the w-register name for a given ID.
fn wreg_name(id: u8) -> &'static str {
    match id {
        0 => "w0", 1 => "w1", 2 => "w2", 3 => "w3",
        4 => "w4", 5 => "w5", 6 => "w6", 7 => "w7",
        8 => "w8", 9 => "w9", 10 => "w10", 11 => "w11",
        12 => "w12", 13 => "w13", 14 => "w14", 15 => "w15",
        16 => "w16", 17 => "w17", 18 => "w18", 19 => "w19",
        20 => "w20", 21 => "w21", 22 => "w22", 23 => "w23",
        24 => "w24", 25 => "w25", 26 => "w26", 27 => "w27",
        28 => "w28", 29 => "w29", 30 => "w30",
        32 => "wzr",
        _ => "??",
    }
}

// ── Line classification ──────────────────────────────────────────────────────

/// Classify a single assembly line into a LineKind.
fn classify_line(line: &str) -> LineKind {
    let trimmed = line.trim();
    if trimmed.is_empty() {
        return LineKind::Nop;
    }

    // Labels end with ':'
    if trimmed.ends_with(':') {
        return LineKind::Label;
    }

    // Directives start with '.'
    if trimmed.starts_with('.') {
        return LineKind::Directive;
    }

    // str xN/wN, [sp, #off]
    if let Some(rest) = trimmed.strip_prefix("str ") {
        if let Some(info) = parse_sp_mem_op(rest) {
            return LineKind::StoreSp { reg: info.0, offset: info.1, is_word: info.2 };
        }
        return LineKind::MemOther;
    }

    // strb wN, [xM]  — byte store
    if trimmed.starts_with("strb ") || trimmed.starts_with("strh ") {
        return LineKind::MemOther;
    }

    // ldr xN/wN, [sp, #off]
    if let Some(rest) = trimmed.strip_prefix("ldr ") {
        if let Some(info) = parse_sp_mem_op(rest) {
            return LineKind::LoadSp { reg: info.0, offset: info.1, is_word: info.2 };
        }
        return LineKind::MemOther;
    }

    // ldrsw xN, [sp, #off]
    if let Some(rest) = trimmed.strip_prefix("ldrsw ") {
        if let Some((reg_str, addr)) = rest.split_once(", ") {
            let reg = parse_reg(reg_str.trim());
            if reg != REG_NONE {
                if let Some(offset) = parse_sp_offset(addr.trim()) {
                    return LineKind::LoadswSp { reg, offset };
                }
            }
        }
        return LineKind::MemOther;
    }

    // ldrsb
    if trimmed.starts_with("ldrsb ") || trimmed.starts_with("ldrsh ") ||
       trimmed.starts_with("ldrb ") || trimmed.starts_with("ldrh ") {
        return LineKind::LoadsbReg;
    }

    // stp (store pair)
    if trimmed.starts_with("stp ") {
        if trimmed.contains("[sp") {
            return LineKind::StorePairSp;
        }
        return LineKind::MemOther;
    }

    // ldp (load pair)
    if trimmed.starts_with("ldp ") {
        if trimmed.contains("[sp") {
            return LineKind::LoadPairSp;
        }
        return LineKind::MemOther;
    }

    // mov xN, xM  or  mov xN, #imm  or  mov xN, :lo12:sym etc.
    if let Some(rest) = trimmed.strip_prefix("mov ") {
        // Avoid matching movz, movn, movk
        if !trimmed.starts_with("movz") && !trimmed.starts_with("movn") && !trimmed.starts_with("movk") {
            if let Some((dst_str, src_str)) = rest.split_once(", ") {
                let dst_trimmed = dst_str.trim();
                let dst = parse_reg(dst_trimmed);
                if dst != REG_NONE {
                    let src_trimmed = src_str.trim();
                    if src_trimmed.starts_with('#') || src_trimmed.starts_with('-') {
                        return LineKind::MoveImm { dst };
                    }
                    let src = parse_reg(src_trimmed);
                    if src != REG_NONE {
                        let is_32bit = dst_trimmed.starts_with('w');
                        return LineKind::Move { dst, src, is_32bit };
                    }
                    // mov xN, :lo12:symbol etc.
                    return LineKind::MoveImm { dst };
                }
            }
        }
    }

    // movz / movn / movk
    if trimmed.starts_with("movz ") || trimmed.starts_with("movn ") {
        if let Some((_, rest)) = trimmed.split_once(' ') {
            if let Some((dst_str, _)) = rest.split_once(", ") {
                let dst = parse_reg(dst_str.trim());
                if dst != REG_NONE {
                    return LineKind::MoveWide { dst };
                }
            }
        }
        return LineKind::Other;
    }

    // movk is an update, not a fresh definition
    if trimmed.starts_with("movk ") {
        return LineKind::Other;
    }

    // sxtw xN, wM
    if let Some(rest) = trimmed.strip_prefix("sxtw ") {
        if let Some((dst_str, src_str)) = rest.split_once(", ") {
            let dst = parse_reg(dst_str.trim());
            let src = parse_reg(src_str.trim());
            if dst != REG_NONE && src != REG_NONE {
                return LineKind::Sxtw { dst, src };
            }
        }
    }

    // Unconditional branch: b .label (but not bl, b.cc)
    if trimmed.starts_with("b ") && !trimmed.starts_with("bl ") && !trimmed.starts_with("b.") {
        return LineKind::Branch;
    }

    // Conditional branch: b.eq, b.ne, b.lt, b.ge, b.gt, b.le, b.hi, b.ls, b.cs, b.cc, etc.
    if trimmed.starts_with("b.") {
        return LineKind::CondBranch;
    }

    // cbz/cbnz/tbz/tbnz
    if trimmed.starts_with("cbz ") || trimmed.starts_with("cbnz ") ||
       trimmed.starts_with("tbz ") || trimmed.starts_with("tbnz ") {
        return LineKind::CmpBranch;
    }

    // ret
    if trimmed == "ret" {
        return LineKind::Ret;
    }

    // bl (branch and link = call)
    if trimmed.starts_with("bl ") || trimmed.starts_with("blr ") {
        return LineKind::Call;
    }

    // br xN (indirect branch = control flow barrier)
    if trimmed.starts_with("br ") {
        return LineKind::Branch;
    }

    // cmp/cmn
    if trimmed.starts_with("cmp ") || trimmed.starts_with("cmn ") {
        return LineKind::Compare;
    }

    // ALU: add, sub, and, orr, eor, lsl, lsr, asr, mul, etc.
    if trimmed.starts_with("add ") || trimmed.starts_with("sub ") ||
       trimmed.starts_with("and ") || trimmed.starts_with("orr ") ||
       trimmed.starts_with("eor ") || trimmed.starts_with("mul ") ||
       trimmed.starts_with("neg ") || trimmed.starts_with("mvn ") ||
       trimmed.starts_with("lsl ") || trimmed.starts_with("lsr ") ||
       trimmed.starts_with("asr ") || trimmed.starts_with("madd ") ||
       trimmed.starts_with("msub ") || trimmed.starts_with("sdiv ") ||
       trimmed.starts_with("udiv ") || trimmed.starts_with("adds ") ||
       trimmed.starts_with("subs ") {
        return LineKind::Alu;
    }

    LineKind::Other
}

/// Parse `xN/wN, [sp, #off]` and return (reg_id, offset, is_word).
fn parse_sp_mem_op(rest: &str) -> Option<(u8, i32, bool)> {
    let (reg_str, addr) = rest.split_once(", ")?;
    let reg_str = reg_str.trim();
    let is_word = reg_str.starts_with('w');
    let reg = parse_reg(reg_str);
    if reg == REG_NONE {
        return None;
    }
    let offset = parse_sp_offset(addr.trim())?;
    Some((reg, offset, is_word))
}

/// Parse `[sp, #off]` or `[sp]` and return the offset.
fn parse_sp_offset(addr: &str) -> Option<i32> {
    // [sp] — zero offset
    if addr == "[sp]" {
        return Some(0);
    }
    // [sp, #N] or [sp, #-N]
    if addr.starts_with("[sp, #") && addr.ends_with(']') {
        let inner = &addr[6..addr.len() - 1]; // strip "[sp, #" and "]"
        return inner.parse::<i32>().ok();
    }
    // [sp, #N]! (pre-index) — not a simple stack slot access
    None
}

/// Extract branch target from `b .label` or `b label`.
fn branch_target(line: &str) -> Option<&str> {
    let trimmed = line.trim();
    // Match "b .label" but not "br xN" (indirect branch)
    if let Some(rest) = trimmed.strip_prefix("b ") {
        let target = rest.trim();
        // Must start with '.' (a label), not a register
        if target.starts_with('.') {
            return Some(target);
        }
    }
    None
}

/// Extract the condition code and target from a conditional branch.
/// `b.eq .label` → Some(("eq", ".label"))
fn cond_branch_parts(line: &str) -> Option<(&str, &str)> {
    let trimmed = line.trim();
    if let Some(rest) = trimmed.strip_prefix("b.") {
        if let Some((cc, target)) = rest.split_once(' ') {
            return Some((cc, target.trim()));
        }
    }
    None
}

/// Invert a condition code.
fn invert_condition(cc: &str) -> Option<&'static str> {
    match cc {
        "eq" => Some("ne"),
        "ne" => Some("eq"),
        "lt" => Some("ge"),
        "ge" => Some("lt"),
        "gt" => Some("le"),
        "le" => Some("gt"),
        "hi" => Some("ls"),
        "ls" => Some("hi"),
        "hs" | "cs" => Some("lo"),
        "lo" | "cc" => Some("hs"),
        "mi" => Some("pl"),
        "pl" => Some("mi"),
        "vs" => Some("vc"),
        "vc" => Some("vs"),
        _ => None,
    }
}

/// Extract label name from a label line (strip trailing `:`)
fn label_name(line: &str) -> Option<&str> {
    let trimmed = line.trim();
    trimmed.strip_suffix(':')
}


// ── Main entry point ─────────────────────────────────────────────────────────

/// Run peephole optimization on AArch64 assembly text.
/// Returns the optimized assembly string.
pub fn peephole_optimize(asm: String) -> String {
    let mut lines: Vec<String> = asm.lines().map(String::from).collect();
    let mut kinds: Vec<LineKind> = lines.iter().map(|l| classify_line(l)).collect();
    let n = lines.len();

    if n == 0 {
        return asm;
    }

    // Phase 1: Iterative local passes (up to 8 rounds)
    let mut changed = true;
    let mut rounds = 0;
    while changed && rounds < 8 {
        changed = false;
        changed |= eliminate_adjacent_store_load(&mut lines, &mut kinds, n);
        changed |= eliminate_redundant_branches(&lines, &mut kinds, n);
        changed |= eliminate_self_moves(&mut kinds, n);
        changed |= eliminate_move_chains(&mut lines, &mut kinds, n);
        changed |= fuse_branch_over_branch(&mut lines, &mut kinds, n);
        changed |= eliminate_unreferenced_labels(&lines, &mut kinds, n);
        rounds += 1;
    }

    // Phase 2: Global passes
    //
    // Global store forwarding is disabled: same-register NOP elimination has a
    // remaining correctness bug in complex float-array code (test 0036_0041).
    // The root cause is not yet identified — it appears correct within a basic
    // block but produces wrong output. Until this is fixed, GSF is skipped.
    //
    // Copy propagation and dead store elimination are independent and safe.
    propagate_register_copies(&mut lines, &mut kinds, n);
    global_dead_store_elimination(&lines, &mut kinds, n);

    // Phase 3: Local cleanup after global passes (up to 4 rounds)
    // CCC_PEEPHOLE_SKIP=name,name skips the listed passes (for bisecting).
    let skip = std::env::var("CCC_PEEPHOLE_SKIP").unwrap_or_default();
    let skip: Vec<&str> = skip.split(',').filter(|s| !s.is_empty()).collect();
    macro_rules! pass {
        ($name:ident($($arg:expr),*)) => {
            !skip.contains(&stringify!($name)) && $name($($arg),*)
        };
    }
    {
        let mut changed2 = true;
        let mut rounds2 = 0;
        while changed2 && rounds2 < 4 {
            changed2 = false;
            changed2 |= pass!(eliminate_adjacent_store_load(&mut lines, &mut kinds, n));
            changed2 |= pass!(eliminate_redundant_branches(&lines, &mut kinds, n));
            changed2 |= pass!(eliminate_self_moves(&mut kinds, n));
            changed2 |= pass!(eliminate_move_chains(&mut lines, &mut kinds, n));
            changed2 |= pass!(eliminate_redundant_sxtw(&mut lines, &mut kinds, n));
            changed2 |= pass!(eliminate_redundant_narrow_extends(&mut lines, &mut kinds, n));
            changed2 |= pass!(propagate_w_copies(&mut lines, &mut kinds, n));
            changed2 |= pass!(eliminate_low_half_only_extends(&lines, &mut kinds, n));
            changed2 |= pass!(eliminate_dead_upper_extends(&lines, &mut kinds, n));
            changed2 |= pass!(fold_shift_into_operand(&mut lines, &mut kinds, n));
            changed2 |= pass!(fold_index_into_mem(&mut lines, &mut kinds, n));
            changed2 |= pass!(fold_offset_into_global_addr(&mut lines, &mut kinds, n));
            changed2 |= pass!(fuse_cset_into_csel(&mut lines, &mut kinds, n));
            changed2 |= pass!(forward_copy_propagation(&mut lines, &mut kinds, n));
            changed2 |= pass!(retarget_results(&mut lines, &mut kinds, n));
            changed2 |= pass!(fuse_and_into_tst(&mut lines, &mut kinds, n));
            changed2 |= pass!(fuse_mul_into_madd(&mut lines, &mut kinds, n));
            changed2 |= pass!(fuse_shift_mask_into_ubfx(&mut lines, &mut kinds, n));
            changed2 |= pass!(fold_extend_into_narrow_store(&mut lines, &mut kinds, n));
            changed2 |= pass!(fold_sxtw_into_users(&mut lines, &mut kinds, n));
            changed2 |= pass!(eliminate_dead_writes(&lines, &mut kinds, n));
            changed2 |= pass!(eliminate_dead_writes_global(&lines, &mut kinds, n));
            changed2 |= pass!(fuse_branch_over_branch(&mut lines, &mut kinds, n));
            changed2 |= pass!(eliminate_unreferenced_labels(&lines, &mut kinds, n));
            rounds2 += 1;
        }
    }

    // Phase 4: drop frame records that leaf functions no longer need.
    omit_leaf_frames(&lines, &mut kinds, n);

    for i in 0..n {
        if lines[i] == RET_NONE_MARKER || lines[i] == RET_X0_MARKER {
            kinds[i] = LineKind::Nop;
        }
    }

    // Build result, filtering out Nop lines
    let align = small_loop_alignment(&lines, &kinds, n);
    let mut result = String::with_capacity(asm.len());
    for i in 0..n {
        if let Some(p) = align[i] {
            result.push_str("    .p2align ");
            result.push_str(if p == 5 { "5" } else { "6" });
            result.push('\n');
        }
        if kinds[i] != LineKind::Nop {
            result.push_str(&lines[i]);
            result.push('\n');
        }
    }
    result
}

// ── Loop alignment ───────────────────────────────────────────────────────────
//
// A loop that straddles a 64-byte boundary needs two instruction fetches per
// iteration; a short loop body that fits in one line runs up to twice as fast
// (measured on Apple M-series). The head of each loop of at most 16
// instructions without calls (counting from its label to the last branch back
// to it) is aligned to 32 bytes if the loop fits in 32, else to 64.

/// For each line, the alignment (log2) to emit before it, if any.
fn small_loop_alignment(lines: &[String], kinds: &[LineKind], n: usize) -> Vec<Option<u8>> {
    let mut align = vec![None; n];
    for (start, end) in function_ranges(lines, n) {
        let mut labels: std::collections::HashMap<&str, usize> = std::collections::HashMap::new();
        for k in start..end {
            if kinds[k] == LineKind::Label {
                if let Some(name) = label_name(&lines[k]) {
                    labels.insert(name, k);
                }
            }
        }
        // The last backward branch to each label.
        let mut back: std::collections::HashMap<usize, usize> = std::collections::HashMap::new();
        for j in start..end {
            if !matches!(kinds[j], LineKind::Branch | LineKind::CondBranch | LineKind::CmpBranch) {
                continue;
            }
            let Some((_, ops)) = split_insn(&lines[j]) else { continue };
            if let Some(&i) = labels.get(last_operand(ops)) {
                if i < j {
                    back.insert(i, j);
                }
            }
        }
        for (head, last) in back {
            let mut insns = 0;
            let mut call = false;
            for k in head..=last {
                match kinds[k] {
                    LineKind::Nop | LineKind::Label | LineKind::Directive => {}
                    LineKind::Call => call = true,
                    _ if lines[k].trim_start().starts_with('.') => {}
                    _ => insns += 1,
                }
            }
            if !call && insns <= 16 {
                align[head] = Some(if insns <= 8 { 5 } else { 6 });
            }
        }
    }
    align
}

// ── Pass 1: Adjacent store/load elimination ──────────────────────────────────
//
// Pattern: str xN, [sp, #off]  →  ldr xN, [sp, #off]  (same reg, same offset)
// The load is redundant since the value is already in the register.
// Also: str xN, [sp, #off]  →  ldr xM, [sp, #off]  → replace load with mov xM, xN
// Also handles: str wN, [sp, #off]  →  ldrsw xN, [sp, #off]

fn eliminate_adjacent_store_load(lines: &mut [String], kinds: &mut [LineKind], n: usize) -> bool {
    let mut changed = false;
    let mut i = 0;
    while i + 1 < n {
        if let LineKind::StoreSp { reg: store_reg, offset: store_off, is_word: store_word } = kinds[i] {
            // Look ahead for the matching load (skip Nops)
            let mut j = i + 1;
            while j < n && kinds[j] == LineKind::Nop {
                j += 1;
            }
            if j < n {
                match kinds[j] {
                    LineKind::LoadSp { reg: load_reg, offset: load_off, is_word: load_word }
                        if store_off == load_off && store_word == load_word =>
                    {
                        if store_reg == load_reg {
                            // Same register: eliminate the load entirely
                            kinds[j] = LineKind::Nop;
                            changed = true;
                        } else {
                            // Different register: replace load with mov
                            let reg_fmt = if store_word { wreg_name } else { xreg_name };
                            lines[j] = format!("    mov {}, {}", reg_fmt(load_reg), reg_fmt(store_reg));
                            kinds[j] = LineKind::Move { dst: load_reg, src: store_reg, is_32bit: store_word };
                            changed = true;
                        }
                    }
                    // str wN, [sp, #off] followed by ldrsw xM, [sp, #off]
                    // The value was just stored as a word; sign-extending load can be replaced
                    // with sxtw xM, wN (or eliminated if same reg).
                    LineKind::LoadswSp { reg: load_reg, offset: load_off }
                        if store_off == load_off && store_word =>
                    {
                        lines[j] = format!("    sxtw {}, {}", xreg_name(load_reg), wreg_name(store_reg));
                        kinds[j] = LineKind::Sxtw { dst: load_reg, src: store_reg };
                        changed = true;
                    }
                    _ => {}
                }
            }
        }
        i += 1;
    }
    changed
}

// ── Pass 2: Redundant branch elimination ─────────────────────────────────────
//
// Pattern: b .LBBN  ;  .LBBN:  (branch to immediately next label)
// Falls through naturally, so the branch is redundant.

fn eliminate_redundant_branches(lines: &[String], kinds: &mut [LineKind], n: usize) -> bool {
    let mut changed = false;
    for i in 0..n {
        if kinds[i] == LineKind::Branch {
            if let Some(target) = branch_target(&lines[i]) {
                // Find next non-Nop line
                // Any of the labels directly after the branch.
                let mut j = i + 1;
                while j < n && matches!(kinds[j], LineKind::Nop | LineKind::Label) {
                    if kinds[j] == LineKind::Label && label_name(&lines[j]) == Some(target) {
                        kinds[i] = LineKind::Nop;
                        changed = true;
                        break;
                    }
                    j += 1;
                }
            }
        }
    }
    changed
}

/// Delete `.L` local labels that nothing references, so branches separated by
/// them (e.g. a leftover `.Lskip_N:`) become adjacent for the other passes.
fn eliminate_unreferenced_labels(lines: &[String], kinds: &mut [LineKind], n: usize) -> bool {
    let is_sym_char = |c: char| c.is_ascii_alphanumeric() || c == '_' || c == '.' || c == '$';
    let mut referenced: std::collections::HashSet<&str> = std::collections::HashSet::new();
    for i in 0..n {
        if !matches!(kinds[i], LineKind::Nop | LineKind::Label) {
            referenced.extend(lines[i].split(|c: char| !is_sym_char(c)).filter(|t| t.starts_with(".L")));
        }
    }
    let mut changed = false;
    for i in 0..n {
        if kinds[i] == LineKind::Label {
            if let Some(name) = label_name(&lines[i]) {
                if name.starts_with(".L") && name.chars().all(is_sym_char) && !referenced.contains(name) {
                    kinds[i] = LineKind::Nop;
                    changed = true;
                }
            }
        }
    }
    changed
}

// ── Pass 3: Self-move elimination ────────────────────────────────────────────
//
// Pattern: mov xN, xN — no-op

fn eliminate_self_moves(kinds: &mut [LineKind], n: usize) -> bool {
    let mut changed = false;
    for i in 0..n {
        if let LineKind::Move { dst, src, is_32bit } = kinds[i] {
            if dst == src && !is_32bit {
                // Only eliminate 64-bit self-moves (mov xN, xN).
                // On AArch64, `mov wN, wN` zeros the upper 32 bits of xN,
                // so it is NOT a true no-op and must be preserved.
                kinds[i] = LineKind::Nop;
                changed = true;
            }
        }
    }
    changed
}

// ── Pass 4: Move chain optimization ──────────────────────────────────────────
//
// Pattern: mov A, B  ;  mov C, A → mov C, B
// This allows the first mov to potentially be dead-eliminated later.

fn eliminate_move_chains(lines: &mut [String], kinds: &mut [LineKind], n: usize) -> bool {
    let mut changed = false;
    let mut i = 0;
    while i + 1 < n {
        match kinds[i] {
            LineKind::Move { dst: dst1, src: src1, is_32bit: is_32bit1 } => {
                // Find next non-Nop instruction
                let mut j = i + 1;
                while j < n && kinds[j] == LineKind::Nop {
                    j += 1;
                }
                if j < n {
                    if let LineKind::Move { dst: dst2, src: src2, is_32bit: is_32bit2 } = kinds[j] {
                        // mov dst1, src1 ; mov dst2, dst1 → mov dst2, src1
                        // Only safe when both moves use the same width.
                        if src2 == dst1 && dst2 != src1 && is_32bit1 == is_32bit2 {
                            let reg_fmt = if is_32bit2 { wreg_name } else { xreg_name };
                            lines[j] = format!("    mov {}, {}", reg_fmt(dst2), reg_fmt(src1));
                            kinds[j] = LineKind::Move { dst: dst2, src: src1, is_32bit: is_32bit2 };
                            changed = true;
                        }
                    }
                }
            }
            LineKind::MoveImm { dst: dst1 } | LineKind::MoveWide { dst: dst1 } => {
                // mov xN, #imm ; mov xM, xN → mov xM, #imm (copy the immediate)
                // Only when dst1 is a scratch register (x0-x15) not callee-saved
                if dst1 <= 15 {
                    let mut j = i + 1;
                    while j < n && kinds[j] == LineKind::Nop {
                        j += 1;
                    }
                    if j < n {
                        if let LineKind::Move { dst: dst2, src: src2, is_32bit: _ } = kinds[j] {
                            if src2 == dst1 {
                                // Copy the immediate instruction, retargeted to dst2
                                let old_line = lines[i].trim();
                                // Replace the register in the first instruction
                                if let Some(new_line) = retarget_move_imm(old_line, dst2) {
                                    lines[j] = format!("    {}", new_line);
                                    kinds[j] = LineKind::MoveImm { dst: dst2 };
                                    changed = true;
                                }
                            }
                        }
                    }
                }
            }
            _ => {}
        }
        i += 1;
    }
    changed
}

/// Retarget a move-immediate instruction to a different destination register.
/// E.g., `mov x0, #5` with new dest x14 → `mov x14, #5`
fn retarget_move_imm(line: &str, new_dst: u8) -> Option<String> {
    // Handle: mov xN, #imm  /  mov xN, :lo12:sym  /  movz xN, #imm  /  movn xN, #imm
    for prefix in &["mov ", "movz ", "movn "] {
        if let Some(rest) = line.strip_prefix(prefix) {
            if let Some((_old_reg, imm_part)) = rest.split_once(", ") {
                let new_reg = if line.contains('w') && !imm_part.starts_with('w') {
                    // If original used w-register (e.g., mov w0, #5)
                    // check if the source had 'w' prefix
                    let old_first = rest.chars().next()?;
                    if old_first == 'w' {
                        wreg_name(new_dst)
                    } else {
                        xreg_name(new_dst)
                    }
                } else {
                    xreg_name(new_dst)
                };
                return Some(format!("{}{}, {}", prefix, new_reg, imm_part));
            }
        }
    }
    None
}

// ── Pass 5: Branch-over-branch fusion ────────────────────────────────────────
//
// Pattern:
//   b.cc .Lskip_N
//   b .target
//   .Lskip_N:
//
// Transform to:
//   b.!cc .target
//
// This is a very common pattern from the codegen: it emits a conditional branch
// to skip over an unconditional branch.

/// Estimate the distance (in instructions) from position `from` to the label
/// `target` in the assembly. Returns `None` if the label is not found.
fn estimate_branch_distance(lines: &[String], kinds: &[LineKind], from: usize, target: &str) -> Option<usize> {
    // Search both forward and backward for the target label
    let target_with_colon = format!("{}:", target);
    for idx in 0..lines.len() {
        if kinds[idx] == LineKind::Label && lines[idx].trim() == target_with_colon {
            // Count non-Nop lines between from and idx (each is one 4-byte instruction)
            let (lo, hi) = if from < idx { (from, idx) } else { (idx, from) };
            let count = (lo..hi).filter(|&p| kinds[p] != LineKind::Nop && kinds[p] != LineKind::Label).count();
            return Some(count);
        }
    }
    None
}

/// Maximum number of instructions a b.cond can reach (±1MB = ±262144 instructions).
/// Use a conservative threshold of 200,000 to leave margin.
const COND_BRANCH_SAFE_DISTANCE: usize = 200_000;

/// The inverse of a conditional branch, retargeted: `b.cc` becomes `b.!cc`,
/// `cbz` becomes `cbnz` and vice versa. None for `tbz`/`tbnz`, whose
/// +/-32KB range is too short to retarget safely.
fn invert_branch(line: &str, target: &str) -> Option<String> {
    if let Some((cc, _)) = cond_branch_parts(line) {
        return invert_condition(cc).map(|inv| format!("    b.{} {}", inv, target));
    }
    let (mnemonic, ops) = split_insn(line)?;
    let inv = match mnemonic {
        "cbz" => "cbnz",
        "cbnz" => "cbz",
        _ => return None,
    };
    Some(format!("    {} {}, {}", inv, first_and_rest(ops).0, target))
}

/// Target label of `b.cc`, `cbz`, or `cbnz`.
fn cond_target(line: &str) -> Option<&str> {
    if let Some((_, t)) = cond_branch_parts(line) {
        return Some(t);
    }
    let (mnemonic, ops) = split_insn(line)?;
    matches!(mnemonic, "cbz" | "cbnz").then(|| last_operand(ops))
}

fn fuse_branch_over_branch(lines: &mut [String], kinds: &mut [LineKind], n: usize) -> bool {
    let mut changed = false;
    let mut i = 0;
    while i + 2 < n {
        if matches!(kinds[i], LineKind::CondBranch | LineKind::CmpBranch) {
            // Find the next two non-Nop instructions
            let mut j = i + 1;
            while j < n && kinds[j] == LineKind::Nop {
                j += 1;
            }
            if j >= n {
                i += 1;
                continue;
            }
            let mut k = j + 1;
            while k < n && kinds[k] == LineKind::Nop {
                k += 1;
            }
            if k >= n {
                i += 1;
                continue;
            }

            // Check pattern: b.cc .skip ; b .target ; .skip:
            if kinds[j] == LineKind::Branch && kinds[k] == LineKind::Label {
                if let (Some(skip_target), Some(real_target), Some(lbl)) = (
                    cond_target(&lines[i]),
                    branch_target(&lines[j]),
                    label_name(&lines[k]),
                ) {
                    if skip_target == lbl {
                        // Check if the real target is within safe b.cond range.
                        // If the label isn't found in this snippet (external target),
                        // assume it's in range since the unconditional branch already
                        // reached it, and b.cond has ±1MB range which is generous.
                        let in_range = estimate_branch_distance(lines, kinds, i, real_target)
                            .is_none_or(|d| d < COND_BRANCH_SAFE_DISTANCE);
                        if in_range {
                            if let Some(inverted) = invert_branch(&lines[i], real_target) {
                                // Replace conditional branch with inverted condition to real target
                                lines[i] = inverted;
                                // kinds[i] stays as CondBranch/CmpBranch
                                // Remove the unconditional branch
                                kinds[j] = LineKind::Nop;
                                // Keep the label (might be targeted by other branches)
                                changed = true;
                            }
                        }
                    }
                }
            }
        }
        i += 1;
    }
    changed
}

// ── Global register copy propagation ─────────────────────────────────────────
//
// After store forwarding converts loads into register moves, propagate those
// copies into subsequent instructions. For `mov xDST, xSRC`, replace
// references to xDST with xSRC in the immediately following instruction
// (within the same basic block).

fn propagate_register_copies(lines: &mut [String], kinds: &mut [LineKind], n: usize) -> bool {
    let mut changed = false;

    for i in 0..n {
        // Only process 64-bit register-to-register moves
        let (dst, src) = match kinds[i] {
            LineKind::Move { dst, src, is_32bit: false } => {
                // Don't propagate sp/fp moves
                if dst >= 29 || src >= 29 {
                    continue;
                }
                (dst, src)
            }
            _ => continue,
        };

        // Find the next non-Nop instruction
        let mut j = i + 1;
        while j < n && kinds[j] == LineKind::Nop {
            j += 1;
        }
        if j >= n {
            continue;
        }

        // Don't propagate across control flow boundaries or into instructions
        // with multiple destination registers (like ldp which has two dest regs
        // after the mnemonic, but our replace_source_reg_in_instruction only
        // treats the first operand as dest).
        match kinds[j] {
            LineKind::Label | LineKind::Branch | LineKind::Ret | LineKind::Directive
            | LineKind::LoadPairSp | LineKind::Call => continue,
            _ => {}
        }
        // Also skip ldp/ldaxr/ldxr/stxr by checking instruction text,
        // since these have multiple dest registers or complex operand semantics
        let trimmed_j = lines[j].trim();
        if trimmed_j.starts_with("ldp ")
            || trimmed_j.starts_with("ldaxr ")
            || trimmed_j.starts_with("ldxr ")
            || trimmed_j.starts_with("stxr ")
            || trimmed_j.starts_with("ldaxp ")
            || trimmed_j.starts_with("ldxp ")
            || trimmed_j.starts_with("cas ")
        {
            continue;
        }

        // Try to replace references to dst with src in line j. Skip lines with
        // relocations or literals, whose symbol names may look like registers.
        let old_line = &lines[j];
        let dst_name = xreg_name(dst);
        if !old_line.contains(dst_name) || old_line.contains(':') || old_line.contains('@') || old_line.contains('=') {
            continue;
        }

        // Don't propagate into instructions that write to the same register
        // as the source (would create a self-reference)
        let src_name = xreg_name(src);

        // For move instructions, replace the source operand
        match kinds[j] {
            LineKind::Move { dst: dst2, src: src2, is_32bit: false } if src2 == dst => {
                // mov X, dst -> mov X, src
                if dst2 != src {
                    lines[j] = format!("    mov {}, {}", xreg_name(dst2), src_name);
                    kinds[j] = LineKind::Move { dst: dst2, src, is_32bit: false };
                    changed = true;
                }
            }
            _ => {
                // General case: try to replace the register in the instruction text.
                // Only replace in source operand positions (not destination).
                if let Some(new_line) = replace_source_reg_in_instruction(old_line, dst_name, src_name) {
                    lines[j] = new_line;
                    kinds[j] = classify_line(&lines[j]);
                    changed = true;
                }
            }
        }
    }
    changed
}

// ── Straight-line dataflow passes ────────────────────────────────────────────
//
// The accumulator codegen computes most results in x0 and then copies them,
// and stages operands through x1/x2. These passes clean that up within
// straight-line code. They only reason about instructions on explicit
// allowlists, whose register reads and writes are all explicit operands; any
// other line (label, branch, call, ret, unknown instruction, raw `.inst`)
// ends the scan with every register treated as live.

/// How a line uses general-purpose registers.
#[derive(Clone, Copy, PartialEq)]
enum Access {
    /// Writes only its first operand (when that is a GPR) and reads the rest.
    Write { deletable: bool },
    /// Reads all its operands and writes no GPR.
    ReadOnly,
    /// Assembler directive that cannot touch registers (`.loc`, `.cfi_*`).
    Transparent,
    /// Anything else: control flow, calls, or unknown semantics.
    Barrier,
}

fn split_insn(line: &str) -> Option<(&str, &str)> {
    let t = line.trim();
    match t.find(char::is_whitespace) {
        Some(p) => Some((&t[..p], t[p..].trim())),
        None => Some((t, "")),
    }
}

fn classify_access(line: &str, kind: LineKind) -> Access {
    if matches!(kind, LineKind::Label | LineKind::Branch | LineKind::CondBranch
        | LineKind::CmpBranch | LineKind::Ret | LineKind::Call)
    {
        return Access::Barrier;
    }
    let Some((mnemonic, ops)) = split_insn(line) else { return Access::Barrier };
    if mnemonic.starts_with('.') {
        return if mnemonic == ".loc" || mnemonic.starts_with(".cfi_") || mnemonic == ".ccc_ret_regs" {
            Access::Transparent
        } else {
            Access::Barrier
        };
    }
    // Relocations and literals carry symbol names that could look like registers.
    let has_symbol = ops.contains(':') || ops.contains('@') || ops.contains('=');
    // Address materialization (`adrp xD, sym`; `add xD, xS, :lo12:sym`) is an
    // ordinary write when the symbol text contains nothing register-like, so
    // no pass can misread or rewrite it.
    let (first, rest) = first_and_rest(ops);
    if gpr_operand(first).is_some() && !ops.contains('[') {
        if mnemonic == "adrp" && !rest.contains(':') && regs_in(rest) == 0 {
            return Access::Write { deletable: true };
        }
        let sym = last_operand(ops);
        if mnemonic == "add" && sym.starts_with(":lo12:") && regs_in(sym) == 0 && ops.matches(',').count() == 2 {
            return Access::Write { deletable: true };
        }
    }
    match mnemonic {
        "mov" | "movz" | "movn" | "add" | "sub" | "and" | "orr" | "eor" | "bic" | "orn"
        | "eon" | "lsl" | "lsr" | "asr" | "ror" | "mul" | "madd" | "msub" | "smull"
        | "umull" | "smulh" | "umulh" | "sdiv" | "udiv" | "neg" | "mvn" | "sxtw" | "sxth"
        | "sxtb" | "uxtb" | "uxth" | "cset" | "csetm" | "csel" | "csinc" | "csinv"
        | "csneg" | "cinc" | "clz" | "rbit" | "rev" | "rev16" | "rev32" | "ubfx" | "sbfx"
        | "ubfiz" | "sbfiz" | "smaddl" | "smsubl" | "fmov" | "fcvtzs" | "fcvtzu" | "umov" if !has_symbol => {
            Access::Write { deletable: true }
        }
        // Loads write their destination; never delete them (volatile/MMIO).
        // Pre/post-index forms also write the base register.
        "ldr" | "ldrb" | "ldrh" | "ldrsb" | "ldrsh" | "ldrsw" | "ldur" | "ldurb" | "ldurh"
        | "ldursb" | "ldursh" | "ldursw"
            if !has_symbol && !ops.contains("]!") && !ops.contains("],") =>
        {
            Access::Write { deletable: false }
        }
        "str" | "strb" | "strh" | "stur" | "sturb" | "sturh" | "cmp" | "cmn" | "tst"
        | "fcmp" | "fcmpe" | "fadd" | "fsub" | "fmul" | "fdiv" | "fneg" | "fabs" | "fsqrt"
        | "fmadd" | "fmsub" | "fnmadd" | "fnmsub" | "fmin" | "fmax" | "fminnm" | "fmaxnm"
        | "fcsel" | "fcvt" | "scvtf" | "ucvtf"
            if !has_symbol && !ops.contains("]!") && !ops.contains("],") =>
        {
            Access::ReadOnly
        }
        // A NEON lane insert or broadcast reads at most one GPR.
        "dup" | "ins" if first.starts_with('v') && !has_symbol => Access::ReadOnly,
        // Other NEON arithmetic on vector registers (ushr v1.8b, v0.8b, #1)
        // touches no GPR and no memory.
        _ if first.starts_with('v') && first.contains('.') && !ops.contains('[') && !has_symbol
            && regs_in(rest) == 0 => Access::ReadOnly,
        _ => Access::Barrier,
    }
}

/// GPR number of an `xN`/`wN` operand (0-28; not fp, lr, sp, or zr).
fn gpr_operand(op: &str) -> Option<u8> {
    let op = op.trim();
    let num = op.strip_prefix('x').or_else(|| op.strip_prefix('w'))?;
    let n: u8 = num.parse().ok()?;
    (n <= 28 && num.len() <= 2 && !num.starts_with('+')).then_some(n)
}

/// Whether `text` names register `n` as `xN` or `wN`.
fn mentions_reg(text: &str, n: u8) -> bool {
    text.split(|c: char| !c.is_ascii_alphanumeric())
        .any(|tok| gpr_operand(tok) == Some(n))
}

/// Split operands into (first operand, everything after the first comma).
fn first_and_rest(ops: &str) -> (&str, &str) {
    match ops.find(',') {
        Some(p) => (ops[..p].trim(), &ops[p + 1..]),
        None => (ops.trim(), ""),
    }
}

/// Whether register `r` is overwritten before being read, scanning forward
/// from line `start` through straight-line code.
fn reg_dead_after(lines: &[String], kinds: &[LineKind], start: usize, n: usize, r: u8) -> bool {
    for k in start..n {
        if kinds[k] == LineKind::Nop {
            continue;
        }
        let (_, ops) = split_insn(&lines[k]).unwrap_or(("", ""));
        match classify_access(&lines[k], kinds[k]) {
            Access::Transparent => {}
            Access::Barrier => return false,
            Access::ReadOnly => {
                if mentions_reg(ops, r) {
                    return false;
                }
            }
            Access::Write { .. } => {
                let (dest, srcs) = first_and_rest(ops);
                if mentions_reg(srcs, r) {
                    return false;
                }
                if gpr_operand(dest) == Some(r) {
                    return true;
                }
            }
        }
    }
    false
}

/// After `mov xA, xB`, rewrite later reads of A to read B, until A or B is
/// written. This lets the mov itself become dead.
fn forward_copy_propagation(lines: &mut [String], kinds: &mut [LineKind], n: usize) -> bool {
    let mut changed = false;
    for i in 0..n {
        let LineKind::Move { dst: a, src: b, is_32bit: false } = kinds[i] else { continue };
        if a > 28 || b > 28 || a == b {
            continue;
        }
        let (a_x, b_x) = (xreg_name(a), xreg_name(b));
        let (a_w, b_w) = (wreg_name(a), wreg_name(b));
        for k in i + 1..n {
            if kinds[k] == LineKind::Nop {
                continue;
            }
            // `cbz`/`cbnz` read their register operand and end the block.
            if kinds[k] == LineKind::CmpBranch {
                if let Some((mnemonic @ ("cbz" | "cbnz"), ops)) = split_insn(&lines[k]) {
                    let (reg, target) = first_and_rest(ops);
                    let new_reg = if reg == a_x { Some(b_x) } else if reg == a_w { Some(b_w) } else { None };
                    if let Some(new_reg) = new_reg {
                        lines[k] = format!("    {} {},{}", mnemonic, new_reg, target);
                        changed = true;
                    }
                }
                break;
            }
            let access = classify_access(&lines[k], kinds[k]);
            let (prefix, ops) = match access {
                Access::Transparent => continue,
                Access::Barrier => break,
                _ => {
                    let line = lines[k].trim_start();
                    let p = line.find(char::is_whitespace).unwrap_or(line.len());
                    (line[..p].to_string(), line[p..].trim().to_string())
                }
            };
            let (dest, srcs) = match access {
                Access::Write { .. } => {
                    let (d, s) = first_and_rest(&ops);
                    (Some(d.to_string()), s.to_string())
                }
                _ => (None, ops.clone()),
            };
            let new_srcs = replace_whole_word(&replace_whole_word(&srcs, a_x, b_x), a_w, b_w);
            if new_srcs != srcs {
                lines[k] = match &dest {
                    Some(d) => format!("    {} {},{}", prefix, d, new_srcs),
                    None => format!("    {} {}", prefix, new_srcs),
                };
                kinds[k] = classify_line(&lines[k]);
                changed = true;
            }
            if let Some(d) = dest {
                if matches!(gpr_operand(&d), Some(r) if r == a || r == b) {
                    break;
                }
            }
        }
    }
    changed
}

/// `op xS, ...` then `mov xR, xS` with S dead afterwards: compute into R.
fn retarget_results(lines: &mut [String], kinds: &mut [LineKind], n: usize) -> bool {
    let mut changed = false;
    // Only removes a read of a dead register; precomputed liveness stays safe.
    let live = live_out_all(lines, kinds, n);
    for i in 0..n {
        if kinds[i] == LineKind::Nop || !matches!(classify_access(&lines[i], kinds[i]), Access::Write { .. }) {
            continue;
        }
        let Some((mnemonic, ops)) = split_insn(&lines[i]) else { continue };
        let (dest, srcs) = first_and_rest(ops);
        let Some(s) = gpr_operand(dest) else { continue };
        let mut j = i + 1;
        while j < n && kinds[j] == LineKind::Nop {
            j += 1;
        }
        if j >= n {
            continue;
        }
        // (A w write is zero-extended, so a w or x move of it copies the same
        // value; a w move of an x write truncates.)
        let LineKind::Move { dst: r, src, is_32bit } = kinds[j] else { continue };
        if is_32bit && !dest.starts_with('w') {
            continue;
        }
        if src != s || r > 28 || r == s || (live[j] & (1 << s) != 0 && !reg_dead_after(lines, kinds, j + 1, n, s)) {
            continue;
        }
        let new_dest = if dest.starts_with('w') { wreg_name(r) } else { xreg_name(r) };
        lines[i] = format!("    {} {},{}", mnemonic, new_dest, srcs);
        kinds[i] = classify_line(&lines[i]);
        kinds[j] = LineKind::Nop;
        changed = true;
    }
    changed
}

/// Whether the flags may be read starting at line `k` (following labels and
/// straight-line code) before anything sets them again.
fn flags_read_from(lines: &[String], kinds: &[LineKind], mut k: usize, n: usize) -> bool {
    let mut seen = 0;
    while k < n && seen < 8 {
        if matches!(kinds[k], LineKind::Nop | LineKind::Label) || lines[k].trim_start().starts_with('.') {
            k += 1;
            continue;
        }
        let Some((m, _)) = split_insn(&lines[k]) else { return true };
        if m.starts_with("b.") || reads_flags(m) {
            return true;
        }
        if sets_flags(m) || matches!(m, "b" | "ret" | "bl" | "blr" | "br") {
            return false;
        }
        seen += 1;
        k += 1;
    }
    true
}

/// `and D, A, B` then `cmp D, #0` and `b.eq`/`b.ne` (D dead afterwards):
/// `tst A, B`. (cmp #0 sets C and tst clears it, so only zero tests qualify,
/// and neither path may read the flags further.)
fn fuse_and_into_tst(lines: &mut [String], kinds: &mut [LineKind], n: usize) -> bool {
    let mut changed = false;
    let live = live_out_all(lines, kinds, n);
    let mut labels: std::collections::HashMap<String, usize> = std::collections::HashMap::new();
    for k in 0..n {
        if kinds[k] == LineKind::Label {
            if let Some(name) = label_name(&lines[k]) {
                labels.insert(name.to_string(), k);
            }
        }
    }
    let next = |kinds: &[LineKind], mut k: usize| {
        while k < n && kinds[k] == LineKind::Nop {
            k += 1;
        }
        k
    };
    for i in 0..n {
        let Some(("and", ops)) = split_insn(&lines[i]) else { continue };
        let (dest, srcs) = first_and_rest(ops);
        let Some(d) = gpr_operand(dest) else { continue };
        let j = next(kinds, i + 1);
        let b = next(kinds, j + 1);
        if b >= n || lines[j].trim() != format!("cmp {}, #0", dest) {
            continue;
        }
        let Some(("b.eq" | "b.ne", target)) = split_insn(&lines[b]) else { continue };
        let target_line = labels.get(target.trim()).copied();
        if live[j] & (1 << d) != 0 || flags_read_from(lines, kinds, b + 1, n)
            || target_line.is_none_or(|t| flags_read_from(lines, kinds, t + 1, n)) {
            continue;
        }
        lines[i] = format!("    tst {}", srcs.trim());
        kinds[i] = classify_line(&lines[i]);
        kinds[j] = LineKind::Nop;
        changed = true;
    }
    changed
}

/// `mul T, A, B` then `add D, C, T` (either order) or `sub D, C, T`, with T
/// dead afterwards: `madd D, A, B, C` / `msub D, A, B, C`.
fn fuse_mul_into_madd(lines: &mut [String], kinds: &mut [LineKind], n: usize) -> bool {
    let mut changed = false;
    let live = live_out_all(lines, kinds, n);
    for i in 0..n {
        let Some(("mul", ops)) = split_insn(&lines[i]) else { continue };
        let m: Vec<&str> = ops.split(',').map(str::trim).collect();
        let [t, a, b] = m[..] else { continue };
        let Some(tr) = gpr_operand(t) else { continue };
        let mut j = i + 1;
        while j < n && kinds[j] == LineKind::Nop {
            j += 1;
        }
        if j >= n {
            continue;
        }
        let Some((op @ ("add" | "sub"), aops)) = split_insn(&lines[j]) else { continue };
        let s: Vec<&str> = aops.split(',').map(str::trim).collect();
        let [d, x, y] = s[..] else { continue };
        // Same register width throughout, plain registers only.
        let w = t.starts_with('w');
        if [d, x, y, a, b].iter().any(|r| r.starts_with('w') != w || gpr_operand(r).is_none()) {
            continue;
        }
        let c = match (op, x == t, y == t) {
            ("add", true, false) => y,
            ("add", false, true) | ("sub", false, true) => x,
            _ => continue,
        };
        if live[j] & (1 << tr) != 0 && gpr_operand(d) != Some(tr) {
            continue;
        }
        let fused = if op == "add" { "madd" } else { "msub" };
        lines[j] = format!("    {} {}, {}, {}, {}", fused, d, a, b, c);
        kinds[j] = classify_line(&lines[j]);
        kinds[i] = LineKind::Nop;
        changed = true;
    }
    changed
}

/// An immediate operand (`#63`, `#0x3f`).
fn parse_imm(op: &str) -> Option<u64> {
    let v = op.strip_prefix('#')?;
    match v.strip_prefix("0x") {
        Some(hex) => u64::from_str_radix(hex, 16).ok(),
        None => v.parse().ok(),
    }
}

/// `lsr T, S, #k` immediately followed by `and D, T, #(2^w - 1)` (T dead
/// afterwards unless it is D) becomes `ubfx D, S, #k, #w`: a bit-field
/// extraction (`(m >> 6) & 63`) in one instruction.
fn fuse_shift_mask_into_ubfx(lines: &mut [String], kinds: &mut [LineKind], n: usize) -> bool {
    let mut changed = false;
    let live = live_out_all(lines, kinds, n);
    for i in 0..n {
        let Some(("lsr", ops)) = split_insn(&lines[i]) else { continue };
        let p: Vec<&str> = ops.split(',').map(str::trim).collect();
        let [t, s, k] = p[..] else { continue };
        let (Some(tr), Some(sr), Some(k)) = (gpr_operand(t), gpr_operand(s), parse_imm(k)) else { continue };
        let width = if t.starts_with('w') { 32 } else { 64 };
        if s.starts_with('w') != (width == 32) {
            continue;
        }
        let mut j = i + 1;
        while j < n && kinds[j] == LineKind::Nop {
            j += 1;
        }
        if j >= n {
            continue;
        }
        let Some(("and", aops)) = split_insn(&lines[j]) else { continue };
        let a: Vec<&str> = aops.split(',').map(str::trim).collect();
        let [d, x, m] = a[..] else { continue };
        let (Some(dr), Some(m)) = (gpr_operand(d), parse_imm(m)) else { continue };
        if gpr_operand(x) != Some(tr) || m == 0 || m & (m + 1) != 0 {
            continue;
        }
        let w = 64 - m.leading_zeros() as u64;
        // A 32-bit shift brings in zeros above bit 31, as does a w-form
        // result.
        let limit = if d.starts_with('w') { 32 } else { width };
        if k + w > limit.min(width) {
            continue;
        }
        if live[j] & (1 << tr) != 0 && dr != tr {
            continue;
        }
        // A field within the low 32 bits extracts in w form (writing wD
        // clears the upper half), so 32-bit copies of S can feed it.
        let (d, src) = if k + w <= 32 { (wreg_name(dr), wreg_name(sr)) } else { (xreg_name(dr), xreg_name(sr)) };
        lines[j] = format!("    ubfx {}, {}, #{}, #{}", d, src, k, w);
        kinds[j] = classify_line(&lines[j]);
        kinds[i] = LineKind::Nop;
        changed = true;
    }
    changed
}

/// `sxtb|uxtb|sxth|uxth A, wB` immediately followed by a byte or halfword
/// store of wA that the extension does not change (`strb` after a byte or
/// halfword extension, `strh` after a halfword one), with A dead afterwards:
/// the store takes wB and the extension goes.
fn fold_extend_into_narrow_store(lines: &mut [String], kinds: &mut [LineKind], n: usize) -> bool {
    let mut changed = false;
    let live = live_out_all(lines, kinds, n);
    for i in 0..n {
        let Some((ext @ ("sxtb" | "uxtb" | "sxth" | "uxth"), ops)) = split_insn(&lines[i]) else { continue };
        let (a, b) = first_and_rest(ops);
        let b = b.trim();
        let (Some(ar), Some(br)) = (gpr_operand(a), gpr_operand(b)) else { continue };
        let mut j = i + 1;
        while j < n && kinds[j] == LineKind::Nop {
            j += 1;
        }
        if j >= n {
            continue;
        }
        let Some((st, sops)) = split_insn(&lines[j]) else { continue };
        let fits = match st {
            "strb" | "sturb" => true,
            "strh" | "sturh" => ext.ends_with('h'),
            _ => false,
        };
        let (v, addr) = first_and_rest(sops);
        if !fits || gpr_operand(v) != Some(ar) || mentions_reg(addr, ar) || live[j] & (1 << ar) != 0 {
            continue;
        }
        lines[j] = format!("    {} {},{}", st, wreg_name(br), addr);
        kinds[j] = classify_line(&lines[j]);
        kinds[i] = LineKind::Nop;
        changed = true;
    }
    changed
}

/// `sxtw xA, wB` whose every read, until A dies, can take wB with the
/// extension folded into the instruction:
///   add|sub xC, xD, xA [, lsl #s]      -> add|sub xC, xD, wB, sxtw [#s]
///   ldr|str R, [xD, xA [, lsl #s]]     -> [xD, wB, sxtw [#s]]
///   madd|msub xC, xA, xM, xN           -> smaddl|smsubl xC, wB, wM, xN
///     (xM set by the line before from a 32-bit constant)
///   a 32-bit read of wA                -> wB
/// B must keep its value until A's last read. The sxtw then goes.
fn fold_sxtw_into_users(lines: &mut [String], kinds: &mut [LineKind], n: usize) -> bool {
    let mut changed = false;
    let live = live_out_all(lines, kinds, n);
    for i in 0..n {
        let LineKind::Sxtw { dst: a, src: b } = kinds[i] else { continue };
        if a == b || a > 28 || b > 28 || live[i] & (1 << a) == 0 {
            continue;
        }
        let mut rewrites: Vec<(usize, String)> = Vec::new();
        let mut prev = i;
        let mut ok = false;
        for k in i + 1..n {
            if kinds[k] == LineKind::Nop || access_is_transparent(&lines[k], kinds[k]) {
                continue;
            }
            if live[prev] & (1 << a) == 0 {
                ok = true;
                break;
            }
            let Some((mnemonic, ops)) = split_insn(&lines[k]) else { break };
            let access = classify_access(&lines[k], kinds[k]);
            if access == Access::Barrier {
                break;
            }
            let (dest, srcs) = match access {
                Access::Write { .. } if !mnemonic.starts_with("str") => {
                    let (d, s) = first_and_rest(ops);
                    (gpr_operand(d), s)
                }
                _ => (None, ops),
            };
            if mentions_reg(srcs, a) {
                match rewrite_sxtw_use(mnemonic, ops, dest.is_some(), a, b, &lines[prev]) {
                    Some(new) => rewrites.push((k, new)),
                    None => break,
                }
            }
            if dest == Some(a) {
                ok = true;
                break;
            }
            if dest == Some(b) && live[k] & (1 << a) != 0 {
                break;
            }
            prev = k;
        }
        if !ok {
            continue;
        }
        for (k, new) in rewrites {
            lines[k] = new;
            kinds[k] = classify_line(&lines[k]);
        }
        kinds[i] = LineKind::Nop;
        changed = true;
    }
    changed
}

/// One read of xA rewritten to read wB sign-extended (see
/// fold_sxtw_into_users), or None.
fn rewrite_sxtw_use(mnemonic: &str, ops: &str, first_is_dest: bool, a: u8, b: u8, prev_line: &str) -> Option<String> {
    let (xa, wa, wb) = (xreg_name(a), wreg_name(a), wreg_name(b));
    let p: Vec<&str> = ops.split(',').map(str::trim).collect();
    let shift_amount = |s: &str| s.strip_prefix("lsl #").and_then(|k| k.parse::<u8>().ok()).filter(|&k| k <= 4);
    match mnemonic {
        "add" | "sub" if !ops.contains('[') => {
            let (c, d, x) = (p.first()?, p.get(1)?, p.get(2)?);
            let (d, x) = if *x == xa { (*d, *x) } else if mnemonic == "add" && *d == xa && p.len() == 3 { (*x, *d) } else { return None };
            if !c.starts_with('x') || !d.starts_with('x') || gpr_operand(d) == Some(a) || x != xa {
                return None;
            }
            let ext = match p.get(3) {
                None => "sxtw".to_string(),
                Some(s) => format!("sxtw #{}", shift_amount(s)?),
            };
            (p.len() <= 4).then(|| format!("    {} {}, {}, {}, {}", mnemonic, c, d, wb, ext))
        }
        "madd" | "msub" => {
            let [c, m1, m2, acc] = p[..] else { return None };
            let other = if m1 == xa && m2 != xa { m2 } else if m2 == xa && m1 != xa { m1 } else { return None };
            if acc == xa || !c.starts_with('x') {
                return None;
            }
            // The other factor must be a sign-extended 32-bit constant.
            let (pm, pops) = split_insn(prev_line)?;
            let (pd, pv) = first_and_rest(pops);
            let k = parse_imm(pv.trim()).map(|v| v as i64)
                .or_else(|| pv.trim().strip_prefix("#-").and_then(|v| v.parse::<i64>().ok()).map(|v| -v))?;
            if pm != "mov" || pd != other || !(i32::MIN as i64..=i32::MAX as i64).contains(&k) {
                return None;
            }
            let o = gpr_operand(other)?;
            let fused = if mnemonic == "madd" { "smaddl" } else { "smsubl" };
            Some(format!("    {} {}, {}, {}, {}", fused, c, wb, wreg_name(o), acc))
        }
        _ if ops.contains('[') => {
            // Register-offset address: [xD, xA] or [xD, xA, lsl #s].
            let open = ops.find('[')?;
            let close = ops.find(']')?;
            let (before, inner, after) = (&ops[..open], &ops[open + 1..close], &ops[close..]);
            // A stored register may be wA (a 32-bit read), never xA; a
            // loaded one is written, not read.
            if reads_x_form(before, a) || after != "]" {
                return None;
            }
            let before = if first_is_dest { before.to_string() } else { replace_whole_word(before, wa, wb) };
            let q: Vec<&str> = inner.split(',').map(str::trim).collect();
            if q.len() < 2 || q[1] != xa || mentions_reg(q[0], a) || !q[0].starts_with('x') {
                return None;
            }
            let ext = match q.get(2) {
                None => "sxtw".to_string(),
                Some(s) => format!("sxtw #{}", shift_amount(s)?),
            };
            (q.len() <= 3).then(|| format!("    {} {}[{}, {}, {}]", mnemonic, before, q[0], wb, ext))
        }
        // Only 32-bit reads of A otherwise: the low half is the same.
        _ if first_is_dest => {
            let (d, rest) = first_and_rest(ops);
            (!reads_x_form(rest, a)).then(|| format!("    {} {},{}", mnemonic, d, replace_whole_word(rest, wa, wb)))
        }
        _ if !reads_x_form(ops, a) => Some(format!("    {} {}", mnemonic, replace_whole_word(ops, wa, wb))),
        _ => None,
    }
}

/// Delete register writes that are overwritten before any read.
fn eliminate_dead_writes(lines: &[String], kinds: &mut [LineKind], n: usize) -> bool {
    let mut changed = false;
    for i in 0..n {
        if kinds[i] == LineKind::Nop {
            continue;
        }
        if classify_access(&lines[i], kinds[i]) != (Access::Write { deletable: true }) {
            continue;
        }
        let (_, ops) = split_insn(&lines[i]).unwrap_or(("", ""));
        let Some(r) = gpr_operand(first_and_rest(ops).0) else { continue };
        if reg_dead_after(lines, kinds, i + 1, n, r) {
            kinds[i] = LineKind::Nop;
            changed = true;
        }
    }
    changed
}

// ── Redundant sign extension ─────────────────────────────────────────────────
//
// Codegen keeps signed 32-bit values sign-extended in their x register, and
// an IR cast to 64 bits emits another `sxtw`. Track, through straight-line
// code, which registers already hold the sign extension of their low 32 bits;
// `sxtw xD, wS` of such a register is a plain copy (or nothing when D == S).

/// Whether writing `line` (destination register `dest` operand text) leaves
/// the destination equal to the sign extension of its low 32 bits.
fn writes_sign_extended(mnemonic: &str, dest: &str, srcs: &str, sext: &[bool; 29]) -> bool {
    let x_dest = dest.starts_with('x');
    match mnemonic {
        "sxtw" | "ldrsw" | "cset" | "csetm" => x_dest,
        // Byte/halfword sign extensions into an x register, and zero
        // extensions of at most 16 bits into either width.
        "sxtb" | "sxth" | "ldrsb" | "ldrsh" => x_dest,
        "uxtb" | "uxth" | "ldrb" | "ldrh" => true,
        // A count of at most 64.
        "clz" => true,
        // Results below 2^31: masking with a small non-negative immediate,
        // and 32-bit logical right shifts by at least one.
        "and" => {
            let imm = last_operand(srcs).strip_prefix('#').and_then(|v| v.parse::<i64>().ok());
            imm.is_some_and(|v| (0..=i32::MAX as i64).contains(&v))
        }
        "lsr" if !x_dest => {
            let imm = last_operand(srcs).strip_prefix('#').and_then(|v| v.parse::<i64>().ok());
            imm.is_some_and(|v| (1..32).contains(&v))
        }
        // Selects between two sign-extended registers (or zero).
        "csel" if x_dest => {
            let (a, rest) = first_and_rest(srcs);
            let b = first_and_rest(rest).0;
            let ok = |r: &str| r == "xzr" || (r.starts_with('x') && gpr_operand(r).is_some_and(|n| sext[n as usize]));
            ok(a) && ok(b)
        }
        "mov" => {
            let src = srcs.trim();
            if let Some(imm) = src.strip_prefix('#') {
                let imm = imm.trim();
                let value = match imm.strip_prefix("0x").or_else(|| imm.strip_prefix("-0x")) {
                    Some(hex) => i64::from_str_radix(hex, 16).ok().map(|v| if imm.starts_with('-') { -v } else { v }),
                    None => imm.parse::<i64>().ok(),
                };
                return match value {
                    Some(v) if x_dest => (i32::MIN as i64..=i32::MAX as i64).contains(&v),
                    Some(v) => (0..=i32::MAX as i64).contains(&v),
                    None => false,
                };
            }
            x_dest && gpr_operand(src).is_some_and(|s| src.starts_with('x') && sext[s as usize])
        }
        _ => false,
    }
}

/// `sxtb xD, wS` / `sxth xD, wS` where S's last write in this straight-line
/// run already sign-extended a byte (halfword) to 64 bits (`ldrsb xS`,
/// `sxtb xS`; for halfwords also `ldrsh xS`, `sxth xS`): a move, or nothing.
fn eliminate_redundant_narrow_extends(lines: &mut [String], kinds: &mut [LineKind], n: usize) -> bool {
    let mut changed = false;
    // Per register: the width (1 or 2 bytes) its value is known to be
    // sign-extended from, as a 64-bit value.
    let mut ext: [Option<u8>; 29] = [None; 29];
    for i in 0..n {
        if kinds[i] == LineKind::Nop {
            continue;
        }
        if matches!(kinds[i], LineKind::Label) || classify_access(&lines[i], kinds[i]) == Access::Barrier {
            ext = [None; 29];
            continue;
        }
        let Some((m, ops)) = split_insn(&lines[i]) else { continue };
        let (dest, srcs) = first_and_rest(ops);
        let width = match m { "sxtb" | "ldrsb" => Some(1u8), "sxth" | "ldrsh" => Some(2), _ => None };
        if let (Some(w), "sxtb" | "sxth", Some(d), Some(s)) = (width, m, gpr_operand(dest), gpr_operand(srcs.trim())) {
            if dest.starts_with('x') && ext[s as usize].is_some_and(|known| known <= w) {
                if d == s {
                    kinds[i] = LineKind::Nop;
                } else {
                    lines[i] = format!("    mov {}, {}", xreg_name(d), xreg_name(s));
                    kinds[i] = classify_line(&lines[i]);
                }
                changed = true;
                ext[d as usize] = ext[s as usize];
                continue;
            }
        }
        // Record what this line leaves in the registers it writes.
        let info = line_info(&lines[i], kinds[i], &|_| None);
        for r in 0..29 {
            if info.defs & (1 << r) != 0 {
                ext[r] = None;
            }
        }
        if let (Some(w), Some(d)) = (width, gpr_operand(dest)) {
            if dest.starts_with('x') {
                ext[d as usize] = Some(w);
            }
        }
    }
    changed
}

fn eliminate_redundant_sxtw(lines: &mut [String], kinds: &mut [LineKind], n: usize) -> bool {
    let mut changed = false;
    let mut sext = [false; 29];
    // Register -> the immediately preceding 32-bit bitwise op that defined it.
    let mut widenable: [Option<usize>; 29] = [None; 29];
    let mut last_wmov: Option<(u8, u8)> = None;
    for i in 0..n {
        if kinds[i] == LineKind::Nop {
            continue;
        }
        let access = classify_access(&lines[i], kinds[i]);
        if access == Access::Barrier {
            sext = [false; 29];
            widenable = [None; 29];
            last_wmov = None;
            continue;
        }
        let line = lines[i].clone();
        let Some((mnemonic, ops)) = split_insn(&line) else { continue };
        let (dest, srcs) = first_and_rest(ops);
        // `mov wA, wB` then `sxtw xD, wA`: sign-extend wB directly.
        if let (LineKind::Sxtw { dst, src }, Some((a, b))) = (kinds[i], last_wmov) {
            if src == a && b <= 28 {
                lines[i] = format!("    sxtw {}, {}", xreg_name(dst), wreg_name(b));
                kinds[i] = classify_line(&lines[i]);
                changed = true;
            }
        }
        last_wmov = match kinds[i] {
            LineKind::Move { dst, src, is_32bit: true } if dst != src => Some((dst, src)),
            _ => None,
        };
        if let LineKind::Sxtw { dst, src } = kinds[i] {
            // `orr wS, wA, wB` of sign-extended A and B, then `sxtw xD, wS`:
            // the 64-bit bitwise op already gives the sign-extended result.
            // S's own upper half changes, so S must be overwritten or dead.
            let s_dead = dst == src || (src <= 28 && reg_dead_after(lines, kinds, i + 1, n, src));
            if src <= 28 && !sext[src as usize] && s_dead {
                if let Some(j) = widenable[src as usize].take() {
                    let (m, o) = split_insn(&lines[j]).unwrap_or(("", ""));
                    let widened = o.split(',').map(|r| r.trim().replacen('w', "x", 1)).collect::<Vec<_>>().join(", ");
                    lines[j] = format!("    {} {}", m, widened);
                    kinds[j] = classify_line(&lines[j]);
                    sext[src as usize] = true;
                }
            }
            if dst <= 28 && src <= 28 && sext[src as usize] {
                if dst == src {
                    kinds[i] = LineKind::Nop;
                } else {
                    lines[i] = format!("    mov {}, {}", xreg_name(dst), xreg_name(src));
                    kinds[i] = classify_line(&lines[i]);
                    sext[dst as usize] = true;
                }
                changed = true;
                continue;
            }
        }
        widenable = [None; 29];
        if let Access::Write { .. } = access {
            if let Some(d) = gpr_operand(dest) {
                let new_sext = writes_sign_extended(mnemonic, dest, srcs, &sext);
                let regs: Vec<&str> = srcs.split(',').map(str::trim).collect();
                if matches!(mnemonic, "and" | "orr" | "eor") && dest.starts_with('w') && regs.len() == 2
                    && regs.iter().all(|r| r.starts_with('w') && gpr_operand(r).is_some_and(|n| sext[n as usize]))
                {
                    widenable[d as usize] = Some(i);
                }
                sext[d as usize] = new_sext;
            }
        }
        // Writeback addressing also updates the base register.
        if ops.contains("]!") || ops.contains("],") {
            sext = [false; 29];
        }
    }
    changed
}

// ── Leaf frame omission ──────────────────────────────────────────────────────
//
// A function that calls nothing and, after the other passes, never touches
// the stack, fp, or lr does not need its frame record (nor the stack slots
// that come with it in a frame of N > 16 bytes):
//   stp x29, x30, [sp, #-N]!; mov x29, sp; ...; ldp x29, x30, [sp], #N; ret
// Dropping it (and its CFI) leaves the default CFA of sp with the return
// address in x30, which is what clang emits for such leaves.

fn omit_leaf_frames(lines: &[String], kinds: &mut [LineKind], n: usize) {
    let mut i = 0;
    while i < n {
        if !is_function_start(&lines[i]) {
            i += 1;
            continue;
        }
        let start = i + 1;
        let mut end = start;
        while end < n && !lines[end].trim().starts_with(".size ") && !is_function_start(&lines[end]) {
            end += 1;
        }
        omit_leaf_frame_in(lines, kinds, start, end);
        i = end;
    }
}

fn omit_leaf_frame_in(lines: &[String], kinds: &mut [LineKind], start: usize, end: usize) {
    // The frame record may sit in a larger frame (stack slots that nothing
    // uses any more): stp x29, x30, [sp, #-N]! ... ldp x29, x30, [sp], #N.
    let mut size = 0;
    let mut frame_cfi: Vec<String> = Vec::new();
    let mut epilogue = String::new();
    let mut prologue: Vec<usize> = Vec::new();
    let mut epilogues: Vec<usize> = Vec::new();
    let mut saw_stp = false;
    let mut saw_mov_fp = false;
    for k in start..end {
        if kinds[k] == LineKind::Nop {
            continue;
        }
        let t = lines[k].trim();
        if !saw_stp {
            // Everything before the frame setup must be the label/directives.
            if let Some(n) = t.strip_prefix("stp x29, x30, [sp, #-").and_then(|r| r.strip_suffix("]!")) {
                let Ok(n) = n.parse::<i64>() else { return };
                size = n;
                frame_cfi = vec![format!(".cfi_def_cfa_offset {n}"), format!(".cfi_offset x29, -{n}"),
                                 format!(".cfi_offset x30, -{}", n - 8), ".cfi_def_cfa_register x29".to_string()];
                epilogue = format!("ldp x29, x30, [sp], #{n}");
                saw_stp = true;
                prologue.push(k);
                continue;
            }
            if kinds[k] == LineKind::Label || t == ".cfi_startproc" || t.starts_with(".loc ") {
                continue;
            }
            return;
        }
        if !saw_mov_fp {
            if frame_cfi.iter().any(|c| c == t) {
                prologue.push(k);
                continue;
            }
            if t == "mov x29, sp" {
                saw_mov_fp = true;
                prologue.push(k);
                continue;
            }
            return;
        }
        if t == ".cfi_def_cfa_register x29" && prologue.len() < 6 {
            prologue.push(k);
            continue;
        }
        if t == epilogue {
            epilogues.push(k);
            continue;
        }
        if kinds[k] == LineKind::Call || (t.starts_with(".cfi_") && t != ".cfi_endproc") {
            return;
        }
        let touches_frame = t.split(|c: char| !c.is_ascii_alphanumeric())
            .any(|tok| matches!(tok, "sp" | "wsp" | "x29" | "w29" | "x30" | "w30" | "fp" | "lr"));
        if touches_frame {
            return;
        }
    }
    if !saw_mov_fp || epilogues.is_empty() || size < 16 {
        return;
    }
    for k in prologue.into_iter().chain(epilogues) {
        kinds[k] = LineKind::Nop;
    }
}

// ── Condition reuse for selects ──────────────────────────────────────────────
//
// A select on a compare result is emitted as
//   cmp ...; cset xC, cc; <setup>; cmp xC, #0; csel xD, xT, xF, ne
// When nothing in between sets the flags or writes C, the csel can use the
// original condition directly (`csel xD, xT, xF, cc`) and the second compare
// goes away; the cset is left for dead-write elimination.

/// Mnemonics that write the NZCV flags.
fn sets_flags(mnemonic: &str) -> bool {
    matches!(mnemonic, "cmp" | "cmn" | "tst" | "fcmp" | "fcmpe" | "ccmp" | "ccmn" | "fccmp"
        | "fccmpe" | "adds" | "subs" | "ands" | "bics" | "adcs" | "sbcs" | "negs" | "ngcs")
}

/// Mnemonics that read the NZCV flags (besides `b.cc`).
fn reads_flags(mnemonic: &str) -> bool {
    matches!(mnemonic, "csel" | "csinc" | "csinv" | "csneg" | "cset" | "csetm" | "cinc" | "cinv"
        | "cneg" | "fcsel" | "adc" | "adcs" | "sbc" | "sbcs" | "ngc" | "ngcs" | "ccmp" | "ccmn"
        | "fccmp" | "fccmpe")
}

/// Whether the flags are dead after line `start - 1`: overwritten, or the
/// block ends, before anything reads them. Codegen never keeps flags live
/// across labels, branches, or calls.
fn flags_dead_after(lines: &[String], kinds: &[LineKind], start: usize, n: usize) -> bool {
    for k in start..n {
        match kinds[k] {
            LineKind::Nop => continue,
            LineKind::Label | LineKind::Branch | LineKind::Ret | LineKind::Call => return true,
            LineKind::CondBranch => return false,
            _ => {}
        }
        let Some((m, _)) = split_insn(&lines[k]) else { return false };
        if reads_flags(m) {
            return false;
        }
        if sets_flags(m) {
            return true;
        }
        // (movk is a barrier for register tracking, as it also reads its
        // destination, and so are the register pairs of an epilogue, but
        // they leave the flags alone.)
        match classify_access(&lines[k], kinds[k]) {
            Access::Barrier if kinds[k] != LineKind::CmpBranch && !matches!(m, "movk" | "ldp" | "stp") => return false,
            _ => {}
        }
    }
    false
}

fn fuse_cset_into_csel(lines: &mut [String], kinds: &mut [LineKind], n: usize) -> bool {
    let mut changed = false;
    for i in 0..n {
        // `cmp xC, #0` / `cmp wC, #0`
        let Some(("cmp", ops)) = split_insn(&lines[i]) else { continue };
        let (creg, imm) = first_and_rest(ops);
        if imm.trim() != "#0" {
            continue;
        }
        let Some(c) = gpr_operand(creg) else { continue };
        // Next instruction: `csel xD, xT, xF, ne|eq`.
        let mut j = i + 1;
        while j < n && kinds[j] == LineKind::Nop {
            j += 1;
        }
        if j >= n {
            continue;
        }
        let Some(("csel", sel_ops)) = split_insn(&lines[j]) else { continue };
        let sel_cc = last_operand(sel_ops);
        if sel_cc != "ne" && sel_cc != "eq" {
            continue;
        }
        // Walk back to the cset that defined C, through flag-preserving code.
        let mut k = i;
        let mut cc = None;
        while k > 0 {
            k -= 1;
            if kinds[k] == LineKind::Nop {
                continue;
            }
            let access = classify_access(&lines[k], kinds[k]);
            if access == Access::Transparent {
                continue;
            }
            let Some((m, o)) = split_insn(&lines[k]) else { break };
            if m == "cset" {
                let (d, rest) = first_and_rest(o);
                if gpr_operand(d) == Some(c) {
                    cc = Some(rest.trim().to_string());
                }
                break;
            }
            let writes_c = matches!(access, Access::Write { .. }) && gpr_operand(first_and_rest(o).0) == Some(c);
            if access == Access::Barrier || sets_flags(m) || writes_c {
                break;
            }
        }
        let Some(cc) = cc else { continue };
        if !flags_dead_after(lines, kinds, j + 1, n) {
            continue;
        }
        let new_cc = if sel_cc == "ne" { Some(cc.as_str()) } else { invert_condition(&cc) };
        let Some(new_cc) = new_cc else { continue };
        let prefix = &sel_ops[..sel_ops.len() - sel_cc.len()];
        lines[j] = format!("    csel {}{}", prefix, new_cc);
        kinds[j] = classify_line(&lines[j]);
        kinds[i] = LineKind::Nop;
        changed = true;
    }
    changed
}

// ── 32-bit copies and shifted operands ───────────────────────────────────────
//
// `sxtw xA, wB` and `mov wA, wB` leave wA equal to wB. Later 32-bit reads of
// wA can read wB instead (making the copy dead), and `sxtw xA, wA` itself is
// a no-op for code that only reads wA. A shift feeding a single ALU op folds
// into the op's shifted-register operand.

/// Whether `ops` (operand text) reads register `r` in its 64-bit form.
fn reads_x_form(ops: &str, r: u8) -> bool {
    ops.split(|c: char| !c.is_ascii_alphanumeric())
        .any(|tok| tok.starts_with('x') && gpr_operand(tok) == Some(r))
}

/// The register ids a line reads (`srcs`) and writes (`dest`), split like
/// classify_access; None for lines that are not plain Write/ReadOnly.
fn split_rw(line: &str, kind: LineKind) -> Option<(Option<String>, String, String)> {
    let access = classify_access(line, kind);
    let (mnemonic, ops) = split_insn(line)?;
    match access {
        Access::Write { .. } => {
            let (d, s) = first_and_rest(ops);
            Some((Some(d.to_string()), s.to_string(), mnemonic.to_string()))
        }
        Access::ReadOnly => Some((None, ops.to_string(), mnemonic.to_string())),
        _ => None,
    }
}

fn propagate_w_copies(lines: &mut [String], kinds: &mut [LineKind], n: usize) -> bool {
    let mut changed = false;
    for i in 0..n {
        let (a, b) = match kinds[i] {
            LineKind::Sxtw { dst, src } => (dst, src),
            LineKind::Move { dst, src, is_32bit: true } => (dst, src),
            _ => continue,
        };
        if a > 28 || b > 28 || a == b {
            continue;
        }
        let (a_w, b_w) = (wreg_name(a), wreg_name(b));
        for k in i + 1..n {
            if kinds[k] == LineKind::Nop {
                continue;
            }
            if kinds[k] == LineKind::CmpBranch {
                if let Some((m @ ("cbz" | "cbnz"), ops)) = split_insn(&lines[k]) {
                    let (reg, target) = first_and_rest(ops);
                    if reg == a_w {
                        lines[k] = format!("    {} {},{}", m, b_w, target);
                        changed = true;
                    }
                }
                break;
            }
            if access_is_transparent(&lines[k], kinds[k]) {
                continue;
            }
            let Some((dest, srcs, mnemonic)) = split_rw(&lines[k], kinds[k]) else { break };
            // A 64-bit read of A needs A itself (and stops the propagation).
            if reads_x_form(&srcs, a) {
                break;
            }
            let new_srcs = replace_whole_word(&srcs, a_w, b_w);
            if new_srcs != srcs {
                lines[k] = match &dest {
                    Some(d) => format!("    {} {},{}", mnemonic, d, new_srcs),
                    None => format!("    {} {}", mnemonic, new_srcs),
                };
                kinds[k] = classify_line(&lines[k]);
                changed = true;
            }
            if let Some(d) = dest {
                if matches!(gpr_operand(&d), Some(r) if r == a || r == b) {
                    break;
                }
            }
        }
    }
    changed
}

fn access_is_transparent(line: &str, kind: LineKind) -> bool {
    classify_access(line, kind) == Access::Transparent
}

/// `sxtw xA, wA` / `mov wA, wA` where A is only read as wA until it is
/// redefined in the same block: the upper half never matters.
fn eliminate_low_half_only_extends(lines: &[String], kinds: &mut [LineKind], n: usize) -> bool {
    let mut changed = false;
    for i in 0..n {
        let a = match kinds[i] {
            LineKind::Sxtw { dst, src } if dst == src => dst,
            LineKind::Move { dst, src, is_32bit: true } if dst == src => dst,
            _ => continue,
        };
        if a > 28 {
            continue;
        }
        let mut safe = false;
        for k in i + 1..n {
            if kinds[k] == LineKind::Nop {
                continue;
            }
            if access_is_transparent(&lines[k], kinds[k]) {
                continue;
            }
            let Some((dest, srcs, mnemonic)) = split_rw(&lines[k], kinds[k]) else { break };
            // A register shift amount uses only its low 6 bits.
            let srcs = match (mnemonic.as_str(), srcs.rsplit_once(',')) {
                ("lsl" | "lsr" | "asr" | "ror", Some((value, amount))) if gpr_operand(amount.trim()).is_some() =>
                    value.to_string(),
                _ => srcs,
            };
            if reads_x_form(&srcs, a) {
                break;
            }
            if dest.as_deref().and_then(gpr_operand) == Some(a) {
                safe = true;
                break;
            }
        }
        if safe {
            kinds[i] = LineKind::Nop;
            changed = true;
        }
    }
    changed
}

/// `lsl/lsr/asr T, S, #k` immediately followed by `add|sub|orr|and|eor D, X, T`
/// (T dead afterwards, same width) becomes `op D, X, S, <shift> #k`.
fn fold_shift_into_operand(lines: &mut [String], kinds: &mut [LineKind], n: usize) -> bool {
    let mut changed = false;
    // Rewrites here only remove reads of dead registers, so liveness computed
    // up front stays a safe over-approximation.
    let live = live_out_all(lines, kinds, n);
    for i in 0..n {
        let Some((shift @ ("lsl" | "lsr" | "asr"), ops)) = split_insn(&lines[i]) else { continue };
        let parts: Vec<&str> = ops.split(',').map(str::trim).collect();
        if parts.len() != 3 || !parts[2].starts_with('#') {
            continue;
        }
        let (t_name, s_name, amount) = (parts[0], parts[1], parts[2]);
        let (Some(t), Some(_)) = (gpr_operand(t_name), gpr_operand(s_name)) else { continue };
        let mut j = i + 1;
        while j < n && kinds[j] == LineKind::Nop {
            j += 1;
        }
        if j >= n {
            continue;
        }
        let Some((op @ ("add" | "sub" | "orr" | "and" | "eor"), op_ops)) = split_insn(&lines[j]) else { continue };
        let p: Vec<&str> = op_ops.split(',').map(str::trim).collect();
        if p.len() != 3 || p.iter().any(|r| gpr_operand(r).is_none() || r.starts_with('x') != t_name.starts_with('x')) {
            continue;
        }
        // The shifted operand must be the second source; swap for commutative ops.
        let x = if p[2] == t_name && p[1] != t_name {
            p[1]
        } else if p[1] == t_name && p[2] != t_name && op != "sub" {
            p[2]
        } else {
            continue;
        };
        let d = gpr_operand(p[0]).unwrap_or(255);
        if d != t && live[j] & (1 << t) != 0 {
            continue;
        }
        let new_line = format!("    {} {}, {}, {}, {} {}", op, p[0], x, s_name, shift, amount);
        lines[j] = new_line;
        kinds[j] = classify_line(&lines[j]);
        kinds[i] = LineKind::Nop;
        changed = true;
    }
    changed
}

/// `adrp xT, sym; add xT, xT, :lo12:sym; add xD, xT, #K` (T dead afterwards
/// unless it is D) becomes `adrp xD, sym+K; add xD, xD, :lo12:sym+K`: the
/// constant offset of a GEP from a global goes into the relocation addend.
fn fold_offset_into_global_addr(lines: &mut [String], kinds: &mut [LineKind], n: usize) -> bool {
    let mut changed = false;
    // Rewrites here only remove writes of dead registers, so liveness computed
    // up front stays a safe over-approximation.
    let live = live_out_all(lines, kinds, n);
    let next = |kinds: &[LineKind], mut j: usize| {
        j += 1;
        while j < n && kinds[j] == LineKind::Nop {
            j += 1;
        }
        j
    };
    for i in 0..n {
        if kinds[i] == LineKind::Nop {
            continue;
        }
        let Some(("adrp", ops)) = split_insn(&lines[i]) else { continue };
        let (t_name, sym) = first_and_rest(ops);
        let (t_name, sym) = (t_name.to_string(), sym.trim().to_string());
        let Some(t) = gpr_operand(&t_name) else { continue };
        let plain_symbol = !sym.is_empty() && regs_in(&sym) == 0
            && sym.chars().all(|c| c.is_ascii_alphanumeric() || matches!(c, '_' | '.' | '$'));
        if !t_name.starts_with('x') || !plain_symbol {
            continue;
        }
        let j = next(kinds, i);
        let k = next(kinds, j);
        if k >= n {
            continue;
        }
        let lo12 = format!(":lo12:{}", sym);
        let Some(("add", add_ops)) = split_insn(&lines[j]) else { continue };
        let p: Vec<&str> = add_ops.split(',').map(str::trim).collect();
        if p.len() != 3 || p[0] != t_name || p[1] != t_name || p[2] != lo12 {
            continue;
        }
        let Some(("add", off_ops)) = split_insn(&lines[k]) else { continue };
        let q: Vec<&str> = off_ops.split(',').map(str::trim).collect();
        if q.len() != 3 || q[1] != t_name || !q[0].starts_with('x') {
            continue;
        }
        let Some(d) = gpr_operand(q[0]) else { continue };
        let Some(offset) = q[2].strip_prefix('#').and_then(|v| v.parse::<u32>().ok()) else { continue };
        if offset == 0 || offset > 4095 || (d != t && live[k] & (1 << t) != 0) {
            continue;
        }
        let dst = q[0].to_string();
        lines[i] = format!("    adrp {}, {}+{}", dst, sym, offset);
        lines[j] = format!("    add {}, {}, :lo12:{}+{}", dst, dst, sym, offset);
        kinds[i] = classify_line(&lines[i]);
        kinds[j] = classify_line(&lines[j]);
        kinds[k] = LineKind::Nop;
        changed = true;
    }
    changed
}

/// `add xA, xB, xI[, lsl #k]` immediately followed by a load/store through
/// `[xA]` (A dead afterwards, k the access size's log2 or 0) becomes the
/// register-offset form `[xB, xI, lsl #k]`.
fn fold_index_into_mem(lines: &mut [String], kinds: &mut [LineKind], n: usize) -> bool {
    let mut changed = false;
    let live = live_out_all(lines, kinds, n);
    for i in 0..n {
        let Some(("add", ops)) = split_insn(&lines[i]) else { continue };
        let p: Vec<&str> = ops.split(',').map(str::trim).collect();
        let shift = match p.len() {
            3 => 0,
            4 => match p[3].strip_prefix("lsl #").and_then(|k| k.parse::<u32>().ok()) {
                Some(k) => k,
                None => continue,
            },
            _ => continue,
        };
        if !p[..3].iter().all(|r| r.starts_with('x') && gpr_operand(r).is_some()) {
            continue;
        }
        let a = gpr_operand(p[0]).unwrap_or(255);
        // The access may come a few lines later, past straight-line code that
        // neither reads A nor writes A or the registers it was computed from.
        let written = (1 << a) | p[1..3].iter().filter_map(|r| gpr_operand(r)).fold(0, |m, r| m | (1 << r));
        let mut j = i + 1;
        let mut skipped = 0;
        while j < n {
            if kinds[j] == LineKind::Nop {
                j += 1;
                continue;
            }
            if split_insn(&lines[j]).is_some_and(|(_, o)| first_and_rest(o).1.trim() == format!("[{}]", p[0])) {
                break;
            }
            let info = line_info(&lines[j], kinds[j], &|_| None);
            if skipped == 6 || !matches!(info.flow, Flow::Next) || matches!(kinds[j], LineKind::Label | LineKind::Call)
                || info.uses & (1 << a) != 0 || info.defs & written != 0 {
                j = n;
                break;
            }
            skipped += 1;
            j += 1;
        }
        if j >= n {
            continue;
        }
        let Some((m, mops)) = split_insn(&lines[j]) else { continue };
        let (reg, addr) = first_and_rest(mops);
        if addr.trim() != format!("[{}]", p[0]) {
            continue;
        }
        let is_load = m.starts_with("ld");
        let size = match (m, reg.chars().next()) {
            ("ldrb" | "ldrsb" | "strb", _) => 1,
            ("ldrh" | "ldrsh" | "strh", _) => 2,
            ("ldrsw", _) => 4,
            ("ldr" | "str", Some('x' | 'd')) => 8,
            ("ldr" | "str", Some('w' | 's')) => 4,
            ("ldr" | "str", Some('q')) => 16,
            _ => continue,
        };
        if shift != 0 && 1u32 << shift != size {
            continue;
        }
        let value_reg = gpr_operand(reg);
        // A store must not store A itself; A must be dead unless a load overwrites it.
        if !is_load && value_reg == Some(a) {
            continue;
        }
        if !(is_load && value_reg == Some(a)) && live[j] & (1 << a) != 0 {
            continue;
        }
        let mem = if shift == 0 {
            format!("[{}, {}]", p[1], p[2])
        } else {
            format!("[{}, {}, lsl #{}]", p[1], p[2], shift)
        };
        lines[j] = format!("    {} {}, {}", m, reg, mem);
        kinds[j] = classify_line(&lines[j]);
        kinds[i] = LineKind::Nop;
        changed = true;
    }
    changed
}

// ── Function-wide dead register write elimination ────────────────────────────
//
// `eliminate_dead_writes` only sees straight-line code. This pass computes
// GPR liveness over each function's control-flow graph (labels and branches)
// and deletes deletable writes whose destination is dead on every path.
//
// The model is conservative: any line it does not understand reads every
// register and writes none; `ret` reads the return and callee-saved
// registers; calls read the argument registers; indirect branches, branches
// to labels outside the function, and anything after them make every
// register live. Functions that call setjmp-like routines are skipped.

/// Emitted by the prologue when x0/x1 do not carry the return value (void
/// or FP results): neither is live at `ret`. Always removed from the output.
pub(super) const RET_NONE_MARKER: &str = "    .ccc_ret_regs 0";
/// Emitted by the prologue when only x0 carries the return value.
pub(super) const RET_X0_MARKER: &str = "    .ccc_ret_regs 1";

/// Bitmask over x0-x28.
type RegSet = u32;
const ALL_REGS: RegSet = (1 << 29) - 1;
/// x0-x8: integer arguments and the indirect-result register.
const ARG_REGS: RegSet = 0x1ff;
/// x0-x1 (return value) and x19-x28 (callee-saved).
const RET_REGS: RegSet = 0b11 | (0x3ff << 19);

/// Register set named by `text` (`xN`/`wN` tokens, N ≤ 28).
fn regs_in(text: &str) -> RegSet {
    text.split(|c: char| !c.is_ascii_alphanumeric())
        .filter_map(gpr_operand)
        .fold(0, |s, r| s | 1 << r)
}

#[derive(Clone, Copy)]
enum Flow {
    /// Falls through to the next line.
    Next,
    /// Jumps to a label (its line index) and maybe falls through.
    Jump { target: Option<usize>, falls_through: bool },
    /// Leaves the function (ret).
    Exit,
}

struct LineInfo {
    uses: RegSet,
    defs: RegSet,
    flow: Flow,
    /// Destination GPR if this line may be deleted when it is dead.
    deletable_dest: Option<u8>,
}

fn last_operand(ops: &str) -> &str {
    ops.rsplit(',').next().unwrap_or("").trim()
}

fn line_info(line: &str, kind: LineKind, label_index: &dyn Fn(&str) -> Option<usize>) -> LineInfo {
    let simple = |uses, defs| LineInfo { uses, defs, flow: Flow::Next, deletable_dest: None };
    let (mnemonic, ops) = split_insn(line).unwrap_or(("", ""));
    match kind {
        LineKind::Nop | LineKind::Label => return simple(0, 0),
        LineKind::Ret => return LineInfo { uses: RET_REGS, defs: 0, flow: Flow::Exit, deletable_dest: None },
        LineKind::Call => {
            let uses = if mnemonic == "blr" { ARG_REGS | regs_in(ops) } else { ARG_REGS };
            return simple(uses, 0);
        }
        LineKind::Branch => {
            let target = if mnemonic == "b" { label_index(ops.trim()) } else { None };
            // An unknown target (indirect, tail call, outside label) needs everything.
            let uses = if target.is_some() { 0 } else { ALL_REGS };
            return LineInfo { uses, defs: 0, flow: Flow::Jump { target, falls_through: false }, deletable_dest: None };
        }
        LineKind::CondBranch | LineKind::CmpBranch => {
            let target = label_index(last_operand(ops));
            let reg_use = if kind == LineKind::CmpBranch { regs_in(first_and_rest(ops).0) } else { 0 };
            let uses = if target.is_some() { reg_use } else { ALL_REGS };
            return LineInfo { uses, defs: 0, flow: Flow::Jump { target, falls_through: true }, deletable_dest: None };
        }
        _ => {}
    }
    let (dest, srcs) = first_and_rest(ops);
    // Address materialization: `adrp xN, sym` and `add xN, xM, :lo12:sym`.
    if mnemonic == "adrp" {
        if let Some(d) = gpr_operand(dest) {
            return LineInfo { uses: 0, defs: 1 << d, flow: Flow::Next, deletable_dest: Some(d) };
        }
    }
    if mnemonic == "add" && last_operand(ops).starts_with(":lo12:") {
        let base = first_and_rest(srcs).0;
        if let (Some(d), Some(b)) = (gpr_operand(dest), gpr_operand(base)) {
            return LineInfo { uses: 1 << b, defs: 1 << d, flow: Flow::Next, deletable_dest: Some(d) };
        }
    }
    // `movk xD, #imm, lsl #s` replaces 16 bits of D: it reads and writes D
    // only (a barrier for the rewriting passes, but not for liveness).
    if mnemonic == "movk" && !srcs.contains(':') && regs_in(srcs) == 0 {
        if let Some(d) = gpr_operand(dest) {
            return LineInfo { uses: 1 << d, defs: 1 << d, flow: Flow::Next, deletable_dest: Some(d) };
        }
    }
    // Pair loads/stores and writeback forms (callee-saved register saves and
    // restores, memcpy loops). The writeback base stays live; it is never
    // treated as killed.
    let has_symbol = ops.contains(':') || ops.contains('@') || ops.contains('=');
    if !has_symbol {
        if let Some(bracket) = ops.find('[') {
            let (regs, addr) = (&ops[..bracket], &ops[bracket..]);
            let writeback = addr.contains("]!") || addr.contains("],");
            match mnemonic {
                "ldp" | "ldpsw" => {
                    let (a, rest) = first_and_rest(regs);
                    let b = first_and_rest(rest).0;
                    let defs = regs_in(a) | regs_in(b);
                    return simple(regs_in(addr), defs & !regs_in(addr));
                }
                "stp" => return simple(regs_in(ops), 0),
                "ldr" | "ldrb" | "ldrh" | "ldrsb" | "ldrsh" | "ldrsw" if writeback => {
                    return simple(regs_in(addr), regs_in(dest) & !regs_in(addr));
                }
                "str" | "strb" | "strh" if writeback => return simple(regs_in(ops), 0),
                _ => {}
            }
        }
    }
    match classify_access(line, kind) {
        Access::Transparent => simple(0, 0),
        Access::Barrier => simple(ALL_REGS, 0),
        Access::ReadOnly => simple(regs_in(ops), 0),
        Access::Write { deletable } => match gpr_operand(dest) {
            Some(d) => LineInfo {
                uses: regs_in(srcs),
                defs: 1 << d,
                flow: Flow::Next,
                deletable_dest: deletable.then_some(d),
            },
            // Non-GPR destination (e.g. `fmov d0, x1`): only reads.
            None => simple(regs_in(srcs), 0),
        },
    }
}

fn is_function_start(line: &str) -> bool {
    let t = line.trim();
    t.starts_with(".type ") && t.ends_with("%function")
}

fn eliminate_dead_writes_global(lines: &[String], kinds: &mut [LineKind], n: usize) -> bool {
    let mut changed = false;
    for (start, end) in function_ranges(lines, n) {
        changed |= dead_writes_in_function(lines, kinds, start, end);
    }
    changed
}

/// GPRs live after each line of the function `start..end`, and each line's
/// deletable destination. None for functions the model does not cover
/// (setjmp-like calls).
fn function_liveness(lines: &[String], kinds: &[LineKind], start: usize, end: usize) -> Option<(Vec<RegSet>, Vec<Option<u8>>)> {
    function_liveness_with(lines, kinds, start, end, false)
}

/// function_liveness, or with `upper` the liveness of the registers' upper
/// 32 bits: ordinary instructions then use only the registers they read in
/// x form (a w read, or a register shift amount, sees only the low half);
/// calls, returns and unknown lines still read theirs whole.
fn function_liveness_with(lines: &[String], kinds: &[LineKind], start: usize, end: usize, upper: bool)
                          -> Option<(Vec<RegSet>, Vec<Option<u8>>)> {
    let body = start..end;
    if body.clone().any(|k| {
        kinds[k] == LineKind::Call
            && ["setjmp", "vfork", "getcontext", "savectx"].iter().any(|s| lines[k].contains(s))
    }) {
        return None;
    }
    // Label name -> line index (local to this function).
    let mut labels: std::collections::HashMap<&str, usize> = std::collections::HashMap::new();
    for k in body.clone() {
        if kinds[k] == LineKind::Label {
            if let Some(name) = label_name(&lines[k]) {
                labels.insert(name, k);
            }
        }
    }
    let label_index = |name: &str| labels.get(name).copied();
    let ret_mask: RegSet = if body.clone().any(|k| lines[k] == RET_NONE_MARKER) {
        !0b11
    } else if body.clone().any(|k| lines[k] == RET_X0_MARKER) {
        !0b10
    } else {
        !0
    };

    let mut infos: Vec<LineInfo> = body.clone().map(|k| line_info(&lines[k], kinds[k], &label_index)).collect();
    if upper {
        for (info, k) in infos.iter_mut().zip(body.clone()) {
            if let Some(x) = x_form_reads(&lines[k], kinds[k]) {
                info.uses &= x;
            }
        }
    }
    for info in infos.iter_mut().filter(|i| matches!(i.flow, Flow::Exit)) {
        info.uses &= ret_mask;
    }
    let len = infos.len();
    let succ_live = |live_in: &[RegSet], idx: usize| -> RegSet {
        let next = if idx + 1 < len { live_in[idx + 1] } else { ALL_REGS };
        match infos[idx].flow {
            Flow::Next => next,
            Flow::Exit => 0,
            Flow::Jump { target, falls_through } => {
                let t = target.map_or(ALL_REGS, |t| live_in[t - start]);
                if falls_through { t | next } else { t }
            }
        }
    };
    let mut live_in: Vec<RegSet> = vec![0; len];
    let mut iterate = true;
    while iterate {
        iterate = false;
        for idx in (0..len).rev() {
            let out = succ_live(&live_in, idx);
            let new_in = (out & !infos[idx].defs) | infos[idx].uses;
            if new_in != live_in[idx] {
                live_in[idx] = new_in;
                iterate = true;
            }
        }
    }
    let live_out = (0..len).map(|idx| succ_live(&live_in, idx)).collect();
    Some((live_out, infos.iter().map(|i| i.deletable_dest).collect()))
}

/// The registers an ordinary instruction reads in x form (all 64 bits), or
/// None for lines that keep their full uses (calls, branches other than
/// cbz/cbnz, returns, and anything classify_access does not understand).
fn x_form_reads(line: &str, kind: LineKind) -> Option<RegSet> {
    let (mnemonic, ops) = split_insn(line)?;
    let reads = match kind {
        LineKind::CmpBranch if matches!(mnemonic, "cbz" | "cbnz") => first_and_rest(ops).0.to_string(),
        LineKind::Nop | LineKind::Label => return Some(0),
        LineKind::Call | LineKind::Ret | LineKind::Branch | LineKind::CondBranch | LineKind::CmpBranch => return None,
        _ => match classify_access(line, kind) {
            Access::Transparent => return Some(0),
            Access::Barrier => return None,
            Access::ReadOnly => ops.to_string(),
            // Pair loads and stores, and writeback forms: their register
            // lists are reads (stores) or writes (loads) of whole registers.
            Access::Write { .. } if ops.contains('[') && matches!(mnemonic, "ldp" | "ldpsw") => {
                ops[ops.find('[')?..].to_string()
            }
            Access::Write { .. } => first_and_rest(ops).1.to_string(),
        },
    };
    // A register shift amount uses only its low 6 bits.
    let reads = match (mnemonic, reads.rsplit_once(',')) {
        ("lsl" | "lsr" | "asr" | "ror", Some((value, amount))) if gpr_operand(amount.trim()).is_some() =>
            value.to_string(),
        _ => reads,
    };
    Some(reads.split(|c: char| !c.is_ascii_alphanumeric())
        .filter(|tok| tok.starts_with('x'))
        .filter_map(gpr_operand)
        .fold(0, |m, r| m | (1 << r)))
}

/// `sxtw xA, wA` (or `mov wA, wA`) whose upper half nothing reads before A
/// is written again, on any path: the low half is all that is used.
fn eliminate_dead_upper_extends(lines: &[String], kinds: &mut [LineKind], n: usize) -> bool {
    let mut changed = false;
    for (start, end) in function_ranges(lines, n) {
        let Some((upper_live, _)) = function_liveness_with(lines, kinds, start, end, true) else { continue };
        for k in start..end {
            let a = match kinds[k] {
                LineKind::Sxtw { dst, src } if dst == src => dst,
                LineKind::Move { dst, src, is_32bit: true } if dst == src => dst,
                _ => continue,
            };
            if a <= 28 && upper_live[k - start] & (1 << a) == 0 {
                kinds[k] = LineKind::Nop;
                changed = true;
            }
        }
    }
    changed
}

/// Function boundaries: (start, end) line ranges of each function body.
fn function_ranges(lines: &[String], n: usize) -> Vec<(usize, usize)> {
    let mut ranges = Vec::new();
    let mut i = 0;
    while i < n {
        if !is_function_start(&lines[i]) {
            i += 1;
            continue;
        }
        let start = i + 1;
        let mut end = start;
        while end < n && !lines[end].trim().starts_with(".size ") && !is_function_start(&lines[end]) {
            end += 1;
        }
        ranges.push((start, end));
        i = end;
    }
    ranges
}

/// GPRs live after every line (all registers outside analyzed functions).
fn live_out_all(lines: &[String], kinds: &[LineKind], n: usize) -> Vec<RegSet> {
    let mut live = vec![ALL_REGS; n];
    for (start, end) in function_ranges(lines, n) {
        if let Some((out, _)) = function_liveness(lines, kinds, start, end) {
            live[start..end].copy_from_slice(&out);
        }
    }
    live
}

fn dead_writes_in_function(lines: &[String], kinds: &mut [LineKind], start: usize, end: usize) -> bool {
    let mut changed = false;
    loop {
        let Some((live_out, deletable)) = function_liveness(lines, kinds, start, end) else { return changed };
        let mut removed = false;
        for idx in 0..live_out.len() {
            if let Some(d) = deletable[idx] {
                if live_out[idx] & (1 << d) == 0 {
                    kinds[start + idx] = LineKind::Nop;
                    removed = true;
                }
            }
        }
        if !removed {
            return changed;
        }
        changed = true;
    }
}

// Shared peephole string utilities -- see backend/peephole_common.rs
use crate::backend::peephole_common::{replace_source_reg_in_instruction, replace_whole_word};

// ── Global dead store elimination ────────────────────────────────────────────
//
// Scans the entire function to find stack slot offsets that are never loaded.
// Stores to such slots are dead and can be eliminated.
// This runs after global store forwarding, which may have converted many loads
// to register moves, leaving the original stores dead.

fn global_dead_store_elimination(lines: &[String], kinds: &mut [LineKind], n: usize) -> bool {
    // Safety check: if any instruction takes the address of sp (e.g., `add xN, sp, #off`),
    // stack slots could be accessed through pointers, so we must not eliminate any stores.
    // This is conservative but sound — it prevents miscompilation when arrays or structs
    // are allocated on the stack and passed by pointer to callees.
    for i in 0..n {
        if kinds[i] == LineKind::Nop {
            continue;
        }
        let trimmed = lines[i].trim();
        // Check for address-of-sp patterns:
        // - `add xN, sp, #offset` (common for array/struct address)
        // - `mov xN, sp` (copying stack pointer)
        // - `sub xN, sp, #N` (stack address computation)
        if (trimmed.starts_with("add ") || trimmed.starts_with("sub ")) && trimmed.contains(", sp,") {
            return false;
        }
        if trimmed.starts_with("mov ") && trimmed.contains(", sp") {
            // Check it's actually "mov xN, sp" not "mov sp, xN"
            if let Some(rest) = trimmed.strip_prefix("mov ") {
                if let Some((_, src)) = rest.split_once(", ") {
                    if src.trim() == "sp" {
                        return false;
                    }
                }
            }
        }
    }

    // Phase 1: Collect all (offset, size) byte ranges that are loaded from.
    // We must use byte-range overlap (not exact offset match) because a wide
    // store (e.g. `str x` at offset 16, 8 bytes) can be partially read by a
    // narrower load at a different offset (e.g. `ldr w` at offset 20, 4 bytes).
    let mut loaded_ranges: Vec<(i32, i32)> = Vec::new(); // (offset, size)
    for i in 0..n {
        match kinds[i] {
            LineKind::LoadSp { offset, is_word, .. } => {
                let size = if is_word { 4 } else { 8 };
                loaded_ranges.push((offset, size));
            }
            LineKind::LoadswSp { offset, .. } => {
                loaded_ranges.push((offset, 4));
            }
            _ => {
                // Check for loads in Other instructions (e.g., ldp, ldrb, ldrh, etc.)
                let trimmed = lines[i].trim();
                let load_size = if trimmed.starts_with("ldp ") {
                    Some(16) // ldp loads two 8-byte registers
                } else if trimmed.starts_with("ldr x") || trimmed.starts_with("ldur x") {
                    Some(8)
                } else if trimmed.starts_with("ldr w") || trimmed.starts_with("ldur w") ||
                          trimmed.starts_with("ldrsw ") {
                    Some(4)
                } else if trimmed.starts_with("ldrh ") || trimmed.starts_with("ldrsh ") {
                    Some(2)
                } else if trimmed.starts_with("ldrb ") || trimmed.starts_with("ldrsb ") {
                    Some(1)
                } else if trimmed.starts_with("ldr ") || trimmed.starts_with("ldur ") {
                    Some(8) // default to 8 for unqualified ldr
                } else {
                    None
                };
                if let Some(sz) = load_size {
                    if trimmed.contains("[sp") {
                        if let Some(off) = extract_sp_offset(trimmed) {
                            loaded_ranges.push((off, sz));
                        }
                    }
                }
            }
        }
    }

    // Phase 2: Remove stores whose byte range does not overlap any load range
    let mut changed = false;
    for i in 0..n {
        if let LineKind::StoreSp { offset, is_word, .. } = kinds[i] {
            let store_size = if is_word { 4 } else { 8 };
            let overlaps_any_load = loaded_ranges.iter().any(|&(load_off, load_sz)| {
                // Two ranges [a, a+as) and [b, b+bs) overlap iff a < b+bs && b < a+as
                offset < load_off + load_sz && load_off < offset + store_size
            });
            if !overlaps_any_load {
                kinds[i] = LineKind::Nop;
                changed = true;
            }
        }
    }
    changed
}

/// Extract the numeric offset from an instruction containing `[sp, #N]` or `[sp]`.
fn extract_sp_offset(line: &str) -> Option<i32> {
    if let Some(start) = line.find("[sp") {
        let rest = &line[start..];
        if rest.starts_with("[sp]") {
            return Some(0);
        }
        if rest.starts_with("[sp, #") {
            let num_start = start + 6; // skip "[sp, #"
            let after = &line[num_start..];
            if let Some(end) = after.find(']') {
                return after[..end].parse::<i32>().ok();
            }
        }
    }
    None
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_classify_store() {
        assert!(matches!(
            classify_line("    str x0, [sp, #16]"),
            LineKind::StoreSp { reg: 0, offset: 16, is_word: false }
        ));
        assert!(matches!(
            classify_line("    str w1, [sp, #24]"),
            LineKind::StoreSp { reg: 1, offset: 24, is_word: true }
        ));
    }

    #[test]
    fn test_classify_load() {
        assert!(matches!(
            classify_line("    ldr x0, [sp, #16]"),
            LineKind::LoadSp { reg: 0, offset: 16, is_word: false }
        ));
    }

    #[test]
    fn test_classify_loadsw() {
        assert!(matches!(
            classify_line("    ldrsw x0, [sp, #24]"),
            LineKind::LoadswSp { reg: 0, offset: 24 }
        ));
    }

    #[test]
    fn test_classify_move() {
        assert!(matches!(
            classify_line("    mov x14, x0"),
            LineKind::Move { dst: 14, src: 0, is_32bit: false }
        ));
    }

    #[test]
    fn test_classify_move_imm() {
        assert!(matches!(
            classify_line("    mov x0, #0"),
            LineKind::MoveImm { dst: 0 }
        ));
        assert!(matches!(
            classify_line("    mov x0, #-1"),
            LineKind::MoveImm { dst: 0 }
        ));
    }

    #[test]
    fn test_classify_branch() {
        assert_eq!(classify_line("    b .LBB1"), LineKind::Branch);
    }

    #[test]
    fn test_classify_cond_branch() {
        assert_eq!(classify_line("    b.ge .Lskip_0"), LineKind::CondBranch);
        assert_eq!(classify_line("    b.eq .LBB3"), LineKind::CondBranch);
    }

    #[test]
    fn test_classify_label() {
        assert_eq!(classify_line(".LBB1:"), LineKind::Label);
        assert_eq!(classify_line("sum_array:"), LineKind::Label);
    }

    #[test]
    fn test_classify_ret() {
        assert_eq!(classify_line("    ret"), LineKind::Ret);
    }

    #[test]
    fn test_classify_sxtw() {
        assert!(matches!(
            classify_line("    sxtw x0, w0"),
            LineKind::Sxtw { dst: 0, src: 0 }
        ));
    }

    #[test]
    fn test_adjacent_store_load_same_reg() {
        let input = "    str x0, [sp, #16]\n    ldr x0, [sp, #16]\n    ret\n";
        let result = peephole_optimize(input.to_string());
        assert!(result.contains("str x0, [sp, #16]"));
        assert!(!result.contains("ldr x0, [sp, #16]"));
    }

    #[test]
    fn test_adjacent_store_load_diff_reg() {
        let input = "    str x0, [sp, #16]\n    ldr x1, [sp, #16]\n    ret\n";
        let result = peephole_optimize(input.to_string());
        // The load is replaced with mov, and then DSE removes the now-dead store
        // (no remaining loads from offset 16), leaving just the mov and ret.
        assert!(!result.contains("ldr x1, [sp, #16]"));
        assert!(result.contains("mov x1, x0"));
    }

    #[test]
    fn test_redundant_branch() {
        let input = "    cbz x0, .LBB1\n    b .LBB1\n.LBB1:\n    ret\n";
        let result = peephole_optimize(input.to_string());
        assert!(!result.contains("b .LBB1"));
        assert!(result.contains(".LBB1:"));
    }

    #[test]
    fn test_repeated_sxtw_becomes_copy() {
        let input = "    sxtw x10, w1\n    sxtw x11, w10\n    sxtw x10, w10\n    str x11, [x9]\n    str x10, [x8]\n";
        let result = peephole_optimize(input.to_string());
        assert!(!result.contains("sxtw x11, w10"), "{}", result);
        assert!(!result.contains("sxtw x10, w10"), "{}", result);
        assert!(result.contains("sxtw x10, w1"), "{}", result);
    }

    #[test]
    fn test_sxtw_kept_after_32bit_arithmetic_and_labels() {
        let input = "    cbz x0, .LBB1\n    sxtw x10, w1\n    add w10, w10, w2\n    sxtw x10, w10\n    sxtw x12, w1\n.LBB1:\n    sxtw x13, w12\n    str x10, [x9]\n    str x13, [x9]\n";
        let result = peephole_optimize(input.to_string());
        assert!(result.contains("sxtw x10, w10"), "32-bit add can overflow: {}", result);
        assert!(result.contains("sxtw x13, w12"), "state resets at labels: {}", result);
    }

    #[test]
    fn test_sxtw_of_clz_and_csel_removed() {
        let input = "    clz x0, x1\n    sxtw x0, w0\n    mov x1, #8\n    mov x2, #-8\n    csel x3, x1, x2, ne\n    sxtw x3, w3\n    str x0, [x9]\n    str x3, [x9]\n";
        let result = peephole_optimize(input.to_string());
        assert!(!result.contains("sxtw"), "{}", result);
    }

    #[test]
    fn test_bitwise_op_widened_instead_of_sxtw() {
        let input = "    sxtw x10, w1\n    sxtw x11, w2\n    orr w12, w10, w11\n    sxtw x12, w12\n    str x12, [x9]\n";
        let result = peephole_optimize(input.to_string());
        assert!(result.contains("orr x12, x10, x11"), "{}", result);
        assert!(!result.contains("sxtw x12"), "{}", result);
    }

    #[test]
    fn test_bitwise_op_not_widened_when_source_stays_live() {
        // x12 is read after the sxtw, and must keep its zero-extended upper half.
        let input = "    sxtw x10, w1\n    sxtw x11, w2\n    orr w12, w10, w11\n    sxtw x13, w12\n    str x12, [x9]\n    str x13, [x9]\n";
        let result = peephole_optimize(input.to_string());
        assert!(!result.contains("orr x12"), "{}", result);
        assert!(result.contains("sxtw x13, w12"), "{}", result);
    }

    #[test]
    fn test_cset_condition_reused_by_csel() {
        let input = "    cmp w1, #2\n    cset x10, le\n    mov x2, #8\n    cmp x10, #0\n    csel x0, x2, x3, ne\n    ret\n";
        let result = peephole_optimize(input.to_string());
        assert!(result.contains("csel x0, x2, x3, le"), "{}", result);
        assert!(!result.contains("cmp x10, #0"), "{}", result);
        // eq on the boolean is the inverted condition.
        let input = "    cmp w1, #2\n    cset x10, le\n    cmp x10, #0\n    csel x0, x2, x3, eq\n    ret\n";
        let result = peephole_optimize(input.to_string());
        assert!(result.contains("csel x0, x2, x3, gt"), "{}", result);
    }

    #[test]
    fn test_cset_condition_not_reused_across_flag_writes() {
        // Another compare between the cset and the select.
        let input = "    cmp w1, #2\n    cset x10, le\n    cmp w1, #7\n    cset x11, eq\n    cmp x10, #0\n    csel x0, x2, x11, ne\n    ret\n";
        let result = peephole_optimize(input.to_string());
        assert!(result.contains("cmp x10, #0"), "{}", result);
        // A later reader of the flags from `cmp x10, #0`.
        let input = "    cmp w1, #2\n    cset x10, le\n    cmp x10, #0\n    csel x0, x2, x3, ne\n    b.ne .LBB1\n    ret\n.LBB1:\n    ret\n";
        let result = peephole_optimize(input.to_string());
        assert!(result.contains("cmp x10, #0"), "{}", result);
    }

    #[test]
    fn test_leaf_frame_omitted() {
        let frame = ".type f, %function\nf:\n.cfi_startproc\n    stp x29, x30, [sp, #-16]!\n    .cfi_def_cfa_offset 16\n    .cfi_offset x29, -16\n    .cfi_offset x30, -8\n    mov x29, sp\n    .cfi_def_cfa_register x29\n";
        let tail = "    ldp x29, x30, [sp], #16\n    ret\n.cfi_endproc\n.size f, .-f\n";
        let leaf = format!("{}    add x0, x0, x1\n{}", frame, tail);
        let result = peephole_optimize(leaf);
        assert!(!result.contains("x29") && !result.contains("cfa"), "{}", result);
        assert!(result.contains("add x0, x0, x1") && result.contains("ret"), "{}", result);
        // Calls and stack accesses keep the frame.
        for body in ["    bl g\n", "    str x0, [sp, #8]\n", "    mov x0, x30\n"] {
            let result = peephole_optimize(format!("{}{}{}", frame, body, tail));
            assert!(result.contains("stp x29, x30") && result.contains("ldp x29, x30"), "{}", result);
        }
        // A larger frame whose slots nothing uses goes too; mismatched sizes stay.
        let big = frame.replace("#-16]!", "#-128]!").replace("offset 16", "offset 128")
            .replace("x29, -16", "x29, -128").replace("x30, -8", "x30, -120");
        let result = peephole_optimize(format!("{}    add x0, x0, x1\n{}", big, tail.replace("#16", "#128")));
        assert!(!result.contains("x29") && !result.contains("cfa"), "{}", result);
        let result = peephole_optimize(format!("{}    add x0, x0, x1\n{}", big, tail));
        assert!(result.contains("stp x29, x30"), "{}", result);
    }

    #[test]
    fn test_w_copy_feeds_32bit_store() {
        // Only the low half is stored: store w13 directly.
        let input = ".type f, %function\nf:\n    sxtw x0, w13\n    mov w0, w0\n    str w0, [x14]\n    ret\n.size f, .-f\n";
        let result = peephole_optimize(input.to_string());
        assert!(result.contains("str w13, [x14]"), "{}", result);
        // x0 itself stays live for the return value, so only the sxtw goes.
        assert!(!result.contains("sxtw x0"), "{}", result);
    }

    #[test]
    fn test_w_copy_stops_at_64bit_read() {
        let input = ".type f, %function\nf:\n    sxtw x0, w13\n    str x0, [x14]\n    str w0, [x15]\n    ret\n.size f, .-f\n";
        let result = peephole_optimize(input.to_string());
        assert!(result.contains("sxtw x0, w13") && result.contains("str x0, [x14]"), "{}", result);
    }

    #[test]
    fn test_shift_folded_into_operand() {
        let input = ".type f, %function\nf:\n    lsl w11, w0, #6\n    orr w13, w12, w11\n    str w13, [x14]\n    ret\n.size f, .-f\n";
        let result = peephole_optimize(input.to_string());
        assert!(result.contains("orr w13, w12, w0, lsl #6"), "{}", result);
        assert!(!result.contains("lsl w11"), "{}", result);
        // Not when the shifted value is used again, or for sub with the shift first.
        let input = ".type f, %function\nf:\n    lsl x11, x0, #3\n    add x13, x12, x11\n    str x13, [x14]\n    str x11, [x15]\n    ret\n.size f, .-f\n";
        assert!(peephole_optimize(input.to_string()).contains("lsl x11, x0, #3"));
        let input = ".type f, %function\nf:\n    lsl x11, x0, #3\n    sub x13, x11, x12\n    str x13, [x14]\n    ret\n.size f, .-f\n";
        assert!(peephole_optimize(input.to_string()).contains("lsl x11, x0, #3"));
    }

    #[test]
    fn test_index_folded_into_load_and_store() {
        let input = ".type f, %function\nf:\n    add x15, x4, x3, lsl #3\n    ldr x10, [x15]\n    add x9, x4, x3, lsl #2\n    str w10, [x9]\n    add x9, x4, x3\n    ldrb w11, [x9]\n    mov x0, x11\n    ret\n.size f, .-f\n";
        let result = peephole_optimize(input.to_string());
        assert!(result.contains("ldr x10, [x4, x3, lsl #3]"), "{}", result);
        assert!(result.contains("str w10, [x4, x3, lsl #2]"), "{}", result);
        assert!(result.contains("ldrb w0, [x4, x3]") || result.contains("ldrb w11, [x4, x3]"), "{}", result);
        assert!(!result.contains("add x"), "{}", result);
    }

    #[test]
    fn test_index_not_folded_with_wrong_scale_or_live_address() {
        // lsl #3 does not match a 4-byte access.
        let input = ".type f, %function\nf:\n    add x15, x4, x3, lsl #3\n    ldr w10, [x15]\n    mov x0, x10\n    ret\n.size f, .-f\n";
        assert!(peephole_optimize(input.to_string()).contains("add x15, x4, x3, lsl #3"));
        // The address is used again afterwards.
        let input = ".type f, %function\nf:\n    add x15, x4, x3\n    ldr x10, [x15]\n    str x10, [x15, #8]\n    ret\n.size f, .-f\n";
        assert!(peephole_optimize(input.to_string()).contains("add x15, x4, x3"));
    }

    #[test]
    fn test_index_folded_across_neon_ops() {
        // NEON arithmetic reads no GPR, so x11 is dead after the load.
        let input = ".type f, %function\nf:\n    add x11, x4, x3\n    ldr d0, [x11]\n    ushr v28.8b, v0.8b, #1\n    add x11, x5, x3\n    ldr d1, [x11]\n    add v28.8b, v28.8b, v1.8b\n    str d28, [x6]\n    ret\n.size f, .-f\n";
        let result = peephole_optimize(input.to_string());
        assert!(result.contains("ldr d0, [x4, x3]") && result.contains("ldr d1, [x5, x3]"), "{}", result);
        assert!(!result.contains("add x11"), "{}", result);
    }

    #[test]
    fn test_sign_extension_with_dead_upper_half_removed() {
        // Only w reads (a compare, a store, a shift amount) on every path.
        let f = |tail: &str| format!(".type f, %function\nf:\n    add w12, w7, w12\n    sxtw x12, w12\n    cmp w12, #5\n    b.ne .LBB1\n    str w12, [x0]\n.LBB1:\n{tail}    ret\n.size f, .-f\n");
        let result = peephole_optimize(f("    lsl x3, x4, x12\n    str x3, [x0]\n"));
        assert!(!result.contains("sxtw"), "{}", result);
        // An x read on one path keeps it: an index, or the value itself.
        for tail in ["    ldr x1, [x2, x12, lsl #3]\n    str x1, [x0]\n", "    str x12, [x0]\n"] {
            assert!(peephole_optimize(f(tail)).contains("sxtw x12, w12"), "{tail}");
        }
        // As does a call argument.
        let input = ".type f, %function\nf:\n    add w1, w7, w1\n    sxtw x1, w1\n    bl g\n    ret\n.size f, .-f\n";
        assert!(peephole_optimize(input.to_string()).contains("sxtw x1, w1"));
    }

    #[test]
    fn test_and_compare_zero_becomes_tst() {
        let body = |br: &str, after: &str| format!(".type f, %function\nf:\n    and x11, x11, x12\n    cmp x11, #0\n    {br} .LBB2\n    ldr x11, [x0]\n{after}    ret\n.LBB2:\n    mov x0, #1\n    ret\n.size f, .-f\n");
        let result = peephole_optimize(body("b.ne", ""));
        assert!(result.contains("tst x11, x12") && !result.contains("cmp x11"), "{}", result);
        // Not for a carry test, nor when the flags are read again.
        assert!(peephole_optimize(body("b.hs", "")).contains("cmp x11, #0"));
        assert!(peephole_optimize(body("b.ne", "    cset x0, hi\n")).contains("cmp x11, #0"));
    }

    #[test]
    fn test_mul_add_becomes_madd() {
        let input = ".type f, %function\nf:\n    mov x0, #48\n    mul x12, x12, x0\n    add x14, x15, x12\n    str x14, [x1]\n    ret\n.size f, .-f\n";
        let result = peephole_optimize(input.to_string());
        assert!(result.contains("madd x14, x12, x0, x15") && !result.contains("mul x12"), "{}", result);
        let input = ".type f, %function\nf:\n    mul w3, w1, w2\n    sub w0, w4, w3\n    ret\n.size f, .-f\n";
        assert!(peephole_optimize(input.to_string()).contains("msub w0, w1, w2, w4"));
        // Not when the product is used again.
        let input = ".type f, %function\nf:\n    mul x3, x1, x2\n    add x0, x4, x3\n    add x0, x0, x3\n    ret\n.size f, .-f\n";
        assert!(peephole_optimize(input.to_string()).contains("mul x3, x1, x2"));
    }

    #[test]
    fn test_w_result_retargeted_into_w_move() {
        // A w write then a w move of it (dead afterwards): compute in place.
        let input = ".type f, %function\nf:\n    and w10, w10, #7\n    mov w0, w10\n    bl g\n    ret\n.size f, .-f\n";
        let result = peephole_optimize(input.to_string());
        assert!(result.contains("and w0, w10, #7") && !result.contains("mov w0"), "{}", result);
        // An x write then a w move truncates: kept.
        let input = ".type f, %function\nf:\n    add x10, x11, x12\n    mov w0, w10\n    bl g\n    ret\n.size f, .-f\n";
        assert!(peephole_optimize(input.to_string()).contains("mov w0, w10"));
    }

    #[test]
    fn test_index_folded_into_later_access() {
        // Other loads and NEON ops may come between the add and its access.
        let input = ".type f, %function\nf:\n    add x11, x4, x3\n    ldr q0, [x5, x3]\n    fmul v28.4s, v0.4s, v26.4s\n    ldr q1, [x11]\n    fmla v28.4s, v27.4s, v1.4s\n    str q28, [x5, x3]\n    ret\n.size f, .-f\n";
        let result = peephole_optimize(input.to_string());
        assert!(result.contains("ldr q1, [x4, x3]") && !result.contains("add x11"), "{}", result);
        // Not past a write of the index, or a read of the address register.
        for between in ["    add x3, x3, #16\n", "    str x11, [x6]\n"] {
            let input = format!(".type f, %function\nf:\n    add x11, x4, x3\n{between}    ldr q1, [x11]\n    str q1, [x6]\n    str x3, [x7]\n    ret\n.size f, .-f\n");
            let result = peephole_optimize(input);
            assert!(result.contains("add x11, x4, x3"), "{between}{result}");
        }
    }

    #[test]
    fn test_small_loops_aligned() {
        let body = |n: usize| "    add x1, x1, x2\n".repeat(n);
        // 5 instructions: 32-byte alignment; 13: 64; 21, or a call: none.
        for (n, call, align) in [(3, false, Some("5")), (11, false, Some("6")), (19, false, None), (3, true, None)] {
            let call_line = if call { "    bl g\n" } else { "" };
            let input = format!(".type f, %function\nf:\n    mov x1, #0\n.LBB1:\n{}{}    subs x2, x2, #1\n    b.ne .LBB1\n    mov x0, x1\n    ret\n.size f, .-f\n", body(n), call_line);
            let result = peephole_optimize(input);
            match align {
                Some(p) => assert!(result.contains(&format!(".p2align {p}\n.LBB1:")), "{n}: {result}"),
                None => assert!(!result.contains(".p2align"), "{n}: {result}"),
            }
        }
    }

    #[test]
    fn test_offset_folded_into_global_addr() {
        let input = ".type f, %function
f:
    adrp x0, rays
    add x0, x0, :lo12:rays
    add x14, x0, #512
    ldr x12, [x14, x10]
    mov x0, x12
    ret
.size f, .-f
";
        let result = peephole_optimize(input.to_string());
        assert!(result.contains("adrp x14, rays+512
    add x14, x14, :lo12:rays+512"), "{}", result);
        assert!(!result.contains("add x14, x0"), "{}", result);
        // Not when the base address is used again, through a GOT entry, or
        // for a symbol named like a register.
        let input = ".type f, %function
f:
    adrp x0, rays
    add x0, x0, :lo12:rays
    add x14, x0, #512
    ldr x12, [x14, x10]
    ldr x13, [x0]
    add x0, x12, x13
    ret
.size f, .-f
";
        assert!(peephole_optimize(input.to_string()).contains("add x14, x0, #512"));
        let input = ".type f, %function
f:
    adrp x0, :got:rays
    ldr x0, [x0, :got_lo12:rays]
    add x14, x0, #512
    ldr x0, [x14]
    ret
.size f, .-f
";
        assert!(peephole_optimize(input.to_string()).contains("add x14, x0, #512"));
        let input = ".type f, %function
f:
    adrp x0, x1
    add x0, x0, :lo12:x1
    add x14, x0, #512
    ldr x0, [x14]
    ret
.size f, .-f
";
        assert!(peephole_optimize(input.to_string()).contains("add x14, x0, #512"));
    }

    #[test]
    fn test_return_register_markers() {
        // x1 is dead at ret when only x0 is returned; nothing is returned in GPRs for void.
        let body = "    mov x1, x5\n    mov x0, x6\n    ret\n.size f, .-f\n";
        let result = peephole_optimize(format!(".type f, %function\nf:\n{}\n{}", RET_X0_MARKER, body));
        assert!(!result.contains("mov x1, x5") && result.contains("mov x0, x6"), "{}", result);
        assert!(!result.contains("ccc_ret_regs"), "{}", result);
        let result = peephole_optimize(format!(".type f, %function\nf:\n{}\n{}", RET_NONE_MARKER, body));
        assert!(!result.contains("mov x1, x5") && !result.contains("mov x0, x6"), "{}", result);
        // Without a marker both stay live.
        let result = peephole_optimize(format!(".type f, %function\nf:\n{}", body));
        assert!(result.contains("mov x1, x5") && result.contains("mov x0, x6"), "{}", result);
    }

    #[test]
    fn test_unreferenced_label_removed() {
        let input = "    b .LBB1\n.LBB1:\n    ret\n";
        let result = peephole_optimize(input.to_string());
        assert!(!result.contains(".LBB1"));
    }

    #[test]
    fn test_cbz_over_branch_fusion() {
        // cbz to a skip label over `b`: invert, and fall through to the true block.
        let input = "    cbz x0, .Lskip_0\n    b .LBB2\n.Lskip_0:\n    b .LBB4\n.LBB2:\n    ret\n.LBB4:\n    ret\n";
        let result = peephole_optimize(input.to_string());
        assert!(result.contains("cbz x0, .LBB4"), "{}", result);
        assert!(!result.contains("b .LBB2"), "{}", result);
    }

    #[test]
    fn test_copy_propagates_into_cbz() {
        let input = ".type f, %function\nf:\n    mov x0, x12\n    cbz x0, .LBB1\n    mov x0, #1\n    ret\n.LBB1:\n    mov x0, #2\n    ret\n.size f, .-f\n";
        let result = peephole_optimize(input.to_string());
        assert!(result.contains("cbz x12, .LBB1"), "{}", result);
        assert!(!result.contains("mov x0, x12"), "{}", result);
    }

    #[test]
    fn test_global_dead_write_across_blocks() {
        // x11 is written before a branch and overwritten on both paths.
        let input = ".type f, %function\nf:\n    mov x11, x1\n    cbz x0, .LBB1\n    mov x11, #1\n    b .LBB2\n.LBB1:\n    mov x11, #2\n.LBB2:\n    mov x0, x11\n    ret\n.size f, .-f\n";
        let result = peephole_optimize(input.to_string());
        assert!(!result.contains("mov x11, x1"), "{}", result);
        assert!(result.contains("mov x11, #1") && result.contains("mov x11, #2"), "{}", result);
    }

    #[test]
    fn test_global_dead_write_kept_when_live_on_one_path() {
        let input = ".type f, %function\nf:\n    mov x11, x1\n    cbz x0, .LBB1\n    mov x11, #1\n.LBB1:\n    mov x0, x11\n    ret\n.size f, .-f\n";
        let result = peephole_optimize(input.to_string());
        assert!(result.contains("mov x11, x1"), "{}", result);
    }

    #[test]
    fn test_global_dead_write_kept_for_return_and_callee_saved() {
        // x1 (second return register) and x20 (callee-saved) are live at ret.
        let input = ".type f, %function\nf:\n    mov x1, x2\n    mov x20, x3\n    mov x9, x4\n    ret\n.size f, .-f\n";
        let result = peephole_optimize(input.to_string());
        assert!(result.contains("mov x1, x2") && result.contains("mov x20, x3"), "{}", result);
        assert!(!result.contains("mov x9, x4"), "{}", result);
    }

    #[test]
    fn test_global_dead_write_kept_before_indirect_branch_and_calls() {
        let input = ".type f, %function\nf:\n    mov x3, x9\n    bl g\n    mov x11, x9\n    br x10\n.size f, .-f\n";
        let result = peephole_optimize(input.to_string());
        assert!(result.contains("mov x3, x9"), "call argument: {}", result);
        assert!(result.contains("mov x11, x9"), "indirect branch: {}", result);
    }

    #[test]
    fn test_global_dead_write_skips_setjmp_functions() {
        let input = ".type f, %function\nf:\n    bl setjmp\n    mov x11, x1\n    cbz x0, .LBB1\n    mov x11, #1\n.LBB1:\n    mov x11, #2\n    mov x0, x11\n    ret\n.size f, .-f\n";
        let result = peephole_optimize(input.to_string());
        assert!(result.contains("mov x11, x1"), "{}", result);
    }

    #[test]
    fn test_self_move() {
        let input = "    mov x0, x0\n    ret\n";
        let result = peephole_optimize(input.to_string());
        assert!(!result.contains("mov x0, x0"));
    }

    #[test]
    fn test_branch_over_branch_fusion() {
        let input = "    b.ge .Lskip_0\n    b .LBB2\n.Lskip_0:\n    b .LBB4\n";
        let result = peephole_optimize(input.to_string());
        // Should become: b.lt .LBB2 (inverted ge → lt)
        assert!(result.contains("b.lt .LBB2"));
        // The unconditional branch to LBB2 should be eliminated
        assert!(!result.contains("    b .LBB2\n"));
    }

    #[test]
    fn test_move_chain() {
        let input = "    mov x0, x14\n    mov x13, x0\n    ret\n";
        let result = peephole_optimize(input.to_string());
        assert!(result.contains("mov x13, x14"));
    }

    #[test]
    fn test_move_imm_chain() {
        let input = "    mov x0, #0\n    mov x14, x0\n    ret\n";
        let result = peephole_optimize(input.to_string());
        assert!(result.contains("mov x14, #0"));
    }

    #[test]
    fn test_store_loadsw_fusion() {
        let input = "    str w1, [sp, #24]\n    ldrsw x0, [sp, #24]\n    ret\n";
        let result = peephole_optimize(input.to_string());
        assert!(!result.contains("ldrsw"));
        assert!(result.contains("sxtw x0, w1"));
    }

    // ── Global store forwarding tests ─────────────────────────────────

    #[test]
    #[ignore] // GSF is disabled due to correctness bug in complex float-array code
    fn test_gsf_same_reg_elimination() {
        // Store x0 then load x0 from same slot (non-adjacent) — load is dead
        let input = "\
    str x0, [sp, #16]\n\
    add x1, x2, x3\n\
    ldr x0, [sp, #16]\n\
    ret\n";
        let result = peephole_optimize(input.to_string());
        assert!(result.contains("str x0, [sp, #16]"));
        assert!(!result.contains("ldr x0, [sp, #16]"));
    }

    #[test]
    #[ignore] // GSF is disabled due to correctness bug in complex float-array code
    fn test_gsf_different_reg_forwarding() {
        // Store x5 then load x10 from same slot — replace load with mov
        let input = "\
    str x5, [sp, #32]\n\
    add x1, x2, x3\n\
    ldr x10, [sp, #32]\n\
    ret\n";
        let result = peephole_optimize(input.to_string());
        assert!(result.contains("str x5, [sp, #32]"));
        assert!(!result.contains("ldr x10, [sp, #32]"));
        assert!(result.contains("mov x10, x5"));
    }

    #[test]
    fn test_gsf_invalidation_on_reg_overwrite() {
        // After x0 is overwritten, the mapping slot 16 → x0 is stale
        let input = "\
    str x0, [sp, #16]\n\
    mov x0, #42\n\
    ldr x1, [sp, #16]\n\
    ret\n";
        let result = peephole_optimize(input.to_string());
        // The load should NOT be forwarded since x0 was overwritten
        assert!(result.contains("ldr x1, [sp, #16]"));
    }

    #[test]
    fn test_gsf_invalidation_at_jump_target() {
        // Mappings are invalidated at jump target labels
        let input = "\
    cbz x0, .LBB1\n\
    str x5, [sp, #16]\n\
    b .LBB1\n\
.LBB1:\n\
    ldr x5, [sp, #16]\n\
    ret\n";
        let result = peephole_optimize(input.to_string());
        // .LBB1 is a jump target, so mappings are invalidated
        assert!(result.contains("ldr x5, [sp, #16]"));
    }

    #[test]
    #[ignore] // GSF is disabled due to correctness bug in complex float-array code
    fn test_gsf_word_forwarding() {
        // Word store forwarded to word load
        let input = "\
    str w3, [sp, #24]\n\
    add x1, x2, x4\n\
    ldr w5, [sp, #24]\n\
    ret\n";
        let result = peephole_optimize(input.to_string());
        assert!(!result.contains("ldr w5, [sp, #24]"));
        assert!(result.contains("mov w5, w3"));
    }

    #[test]
    #[ignore] // GSF is disabled due to correctness bug in complex float-array code
    fn test_gsf_ldrsw_forwarding() {
        // Word store forwarded to sign-extending load
        let input = "\
    str w1, [sp, #24]\n\
    add x2, x3, x4\n\
    ldrsw x5, [sp, #24]\n\
    ret\n";
        let result = peephole_optimize(input.to_string());
        assert!(!result.contains("ldrsw x5, [sp, #24]"));
        assert!(result.contains("sxtw x5, w1"));
    }

    // ── Copy propagation tests ───────────────────────────────────────

    #[test]
    fn test_copy_prop_word_boundary() {
        // Ensure "x1" doesn't match inside "x11"
        assert_eq!(
            replace_whole_word("x11, x1", "x1", "x5"),
            "x11, x5"
        );
    }

    #[test]
    fn test_copy_prop_no_false_match() {
        // x10 should not be affected when replacing x1
        assert_eq!(
            replace_whole_word("x10", "x1", "x5"),
            "x10"
        );
    }

    // ── Dead store elimination tests ─────────────────────────────────

    #[test]
    fn test_dse_with_address_taken() {
        // When sp address is taken, no stores should be eliminated
        let input = "\
    str w0, [sp, #16]\n\
    add x1, sp, #16\n\
    ret\n";
        let result = peephole_optimize(input.to_string());
        // Store must be preserved because address of stack slot is taken
        assert!(result.contains("str w0, [sp, #16]"));
    }

    #[test]
    fn test_retarget_result_into_dest_reg() {
        let input = "    add x0, x21, x13\n    mov x22, x0\n    mov x0, #1\n    ret\n";
        let result = peephole_optimize(input.to_string());
        assert!(result.contains("add x22, x21, x13"), "{}", result);
        assert!(!result.contains("mov x22, x0"), "{}", result);
    }

    #[test]
    fn test_retarget_keeps_live_scratch() {
        // x0 is read after the copy, so the add must still write x0.
        let input = "    add x0, x21, x13\n    mov x22, x0\n    str x0, [x9]\n    ret\n";
        let result = peephole_optimize(input.to_string());
        assert!(result.contains("add x0, x21, x13"), "{}", result);
    }

    #[test]
    fn test_dead_write_removed() {
        let input = "    mov x0, x21\n    mov x0, #5\n    ret\n";
        let result = peephole_optimize(input.to_string());
        assert!(!result.contains("mov x0, x21"), "{}", result);
    }

    #[test]
    fn test_dead_write_kept_before_call() {
        // bl reads x0-x7 implicitly.
        let input = "    mov x0, x21\n    bl foo\n    mov x0, #5\n    ret\n";
        let result = peephole_optimize(input.to_string());
        assert!(result.contains("mov x0, x21"), "{}", result);
    }

    #[test]
    fn test_dead_load_not_removed() {
        // The load may be volatile or MMIO.
        let input = "    ldr x0, [x9]\n    mov x0, #5\n    ret\n";
        let result = peephole_optimize(input.to_string());
        assert!(result.contains("ldr x0, [x9]"), "{}", result);
    }

    #[test]
    fn test_dead_write_kept_before_unknown_instruction() {
        // svc reads x8 (and x0-x5) implicitly.
        let input = "    mov x8, #64\n    svc #0\n    mov x8, #1\n    ret\n";
        let result = peephole_optimize(input.to_string());
        assert!(result.contains("mov x8, #64"), "{}", result);
    }

    #[test]
    fn test_neon_lane_writes_read_one_gpr() {
        // dup/ins read their GPR source and write no GPR, so the dead copy
        // to x10 goes and the source is propagated into the lane writes.
        let input = "    mov w0, w20\n    mov x10, x0\n    dup v27.4s, w0\n    neg x0, x0\n    ins v27.s[1], w0\n    mov x10, #1\n    ret\n";
        let result = peephole_optimize(input.to_string());
        assert!(!result.contains("mov x10, x0"), "{}", result);
        assert!(result.contains("ins v27.s[1], w0"), "{}", result);
    }

    #[test]
    fn test_forward_copy_propagation() {
        let input = "    mov x1, x22\n    add x0, x1, x13\n    str x1, [x9]\n    mov x1, #0\n    ret\n";
        let result = peephole_optimize(input.to_string());
        assert!(result.contains("add x0, x22, x13"), "{}", result);
        assert!(result.contains("str x22, [x9]"), "{}", result);
    }

    #[test]
    fn test_copy_propagation_stops_at_redefinition() {
        let input = "    mov x1, x22\n    mov x22, #7\n    add x0, x1, x13\n    ret\n";
        let result = peephole_optimize(input.to_string());
        assert!(result.contains("add x0, x1, x13"), "{}", result);
    }

    #[test]
    fn test_copy_propagation_skips_symbols() {
        // A symbol named like a register must not be rewritten.
        let input = "    mov x1, x22\n    add x0, x0, :lo12:x1\n    ret\n";
        let result = peephole_optimize(input.to_string());
        assert!(result.contains(":lo12:x1"), "{}", result);
    }
}
