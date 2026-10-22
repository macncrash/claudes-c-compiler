//! Native AArch64 assembler.
//!
//! Parses `.s` assembly text (as emitted by the AArch64 codegen) and produces
//! ELF `.o` object files, removing the dependency on `aarch64-linux-gnu-gcc`
//! for assembly.
//!
//! Architecture:
//! - `parser.rs`     – Tokenize + parse assembly text into `AsmStatement` items
//! - `encoder.rs`    – Encode AArch64 instructions into 32-bit machine words
//! - `elf_writer.rs` – Write ELF object files with sections, symbols, and relocations

pub mod parser;
pub mod encoder;
pub mod elf_writer;

use std::collections::HashMap;
use parser::{parse_asm, AsmStatement, Operand, AsmDirective, DataValue};
use elf_writer::ElfWriter;

/// Assemble AArch64 assembly text into an ELF object file.
///
/// This is the default assembler (used when the `gcc_assembler` feature is disabled).
pub fn assemble(asm_text: &str, output_path: &str) -> Result<(), String> {
    let statements = parse_asm(asm_text)?;
    let statements = expand_literal_pools(&statements);
    let statements = resolve_numeric_labels(&statements);
    let mut writer = ElfWriter::new();
    writer.process_statements(&statements)?;
    writer.write_elf(output_path)?;
    Ok(())
}

/// Expand `LdrLiteralPool` pseudo-instructions into literal pool loads.
///
/// `ldr Xn, =symbol` must load the exact linker-resolved address via a literal
/// pool, not via `adrp+add` (which gives a PC-relative address). This matters
/// for code that runs at a different address than its linked address (e.g.,
/// early boot code running at physical addresses before MMU setup).
///
/// This pass replaces each `LdrLiteralPool` with a `ldr Xn, .Llpool_N`
/// instruction and accumulates pool entries. Pool entries are flushed (emitted
/// as `.quad` data with labels) when:
/// - A `.ltorg` / `.pool` directive is encountered
/// - A section-changing directive is encountered (`.section`, `.pushsection`, `.text`, `.data`)
/// - The end of the statement list is reached
///
/// Each pool entry is 8-byte aligned and consists of:
///   .Llpool_N: .quad symbol[+addend]
fn expand_literal_pools(statements: &[AsmStatement]) -> Vec<AsmStatement> {
    struct PoolEntry {
        label: String,
        symbol: String,
        addend: i64,
    }

    let mut result = Vec::with_capacity(statements.len());
    let mut pending_pool: Vec<PoolEntry> = Vec::new();
    let mut pool_counter: usize = 0;

    // TODO: Track W vs X register to emit .long (4-byte) entries for W registers
    // instead of always emitting .quad (8-byte). Currently safe on little-endian
    // (loads low 32 bits) but wrong on big-endian and wastes 4 bytes per W entry.
    let flush_pool = |pool: &mut Vec<PoolEntry>, out: &mut Vec<AsmStatement>| {
        if pool.is_empty() {
            return;
        }
        // Align pool to 8 bytes
        out.push(AsmStatement::Directive(AsmDirective::Balign(8)));
        for entry in pool.drain(..) {
            out.push(AsmStatement::Label(entry.label));
            if entry.addend != 0 {
                out.push(AsmStatement::Directive(AsmDirective::Quad(vec![
                    DataValue::SymbolOffset(entry.symbol, entry.addend),
                ])));
            } else {
                out.push(AsmStatement::Directive(AsmDirective::Quad(vec![
                    DataValue::Symbol(entry.symbol),
                ])));
            }
        }
    };

    for stmt in statements {
        match stmt {
            AsmStatement::LdrLiteralPool { reg, symbol, addend } => {
                let pool_label = format!(".Llpool_{}", pool_counter);
                pool_counter += 1;

                // Emit: ldr reg, .Llpool_N (PC-relative literal load)
                result.push(AsmStatement::Instruction {
                    mnemonic: "ldr".to_string(),
                    operands: vec![
                        Operand::Reg(reg.clone()),
                        Operand::Symbol(pool_label.clone()),
                    ],
                    raw_operands: format!("{}, {}", reg, pool_label),
                });

                pending_pool.push(PoolEntry {
                    label: pool_label,
                    symbol: symbol.clone(),
                    addend: *addend,
                });
            }
            AsmStatement::Directive(AsmDirective::Ltorg) => {
                flush_pool(&mut pending_pool, &mut result);
            }
            AsmStatement::Directive(AsmDirective::Section(_))
            | AsmStatement::Directive(AsmDirective::PushSection(_))
            | AsmStatement::Directive(AsmDirective::PopSection)
            | AsmStatement::Directive(AsmDirective::Previous) => {
                // Flush pool before section change (pool must be in the same section)
                flush_pool(&mut pending_pool, &mut result);
                result.push(stmt.clone());
            }
            _ => {
                result.push(stmt.clone());
            }
        }
    }

    // Flush any remaining pool entries at the end
    flush_pool(&mut pending_pool, &mut result);

    result
}

/// Check if a label name is a GNU assembler numeric label (e.g., "1", "42").
fn is_numeric_label(name: &str) -> bool {
    !name.is_empty() && name.bytes().all(|b| b.is_ascii_digit())
}

/// Check if a string is a numeric forward/backward reference like "1f" or "2b".
/// Returns Some((number_str, is_forward)) if it is, None otherwise.
fn parse_numeric_ref(name: &str) -> Option<(&str, bool)> {
    if name.len() < 2 {
        return None;
    }
    let last = name.as_bytes()[name.len() - 1];
    let num_part = &name[..name.len() - 1];
    if !num_part.bytes().all(|b| b.is_ascii_digit()) {
        return None;
    }
    match last {
        b'f' | b'F' => Some((num_part, true)),
        b'b' | b'B' => Some((num_part, false)),
        _ => None,
    }
}

/// Resolve a numeric label reference to its unique name.
fn resolve_numeric_name(
    name: &str,
    current_idx: usize,
    defs: &HashMap<String, Vec<(usize, String)>>,
) -> Option<String> {
    let (num, is_forward) = parse_numeric_ref(name)?;
    let def_list = defs.get(num)?;

    if is_forward {
        def_list.iter()
            .find(|(idx, _)| *idx > current_idx)
            .map(|(_, name)| name.clone())
    } else {
        def_list.iter()
            .rev()
            .find(|(idx, _)| *idx < current_idx)
            .map(|(_, name)| name.clone())
    }
}

/// Resolve numeric local labels (1:, 2:, etc.) and their references (1f, 1b)
/// into unique internal label names.
///
/// GNU assembler numeric labels can be defined multiple times. Each forward
/// reference `Nf` refers to the next definition of `N`, and each backward
/// reference `Nb` refers to the most recent definition of `N`.
fn resolve_numeric_labels(statements: &[AsmStatement]) -> Vec<AsmStatement> {
    // First pass: find all numeric label definitions and assign unique names.
    let mut defs: HashMap<String, Vec<(usize, String)>> = HashMap::new();
    let mut unique_counter: HashMap<String, usize> = HashMap::new();

    for (i, stmt) in statements.iter().enumerate() {
        if let AsmStatement::Label(name) = stmt {
            if is_numeric_label(name) {
                let count = unique_counter.entry(name.clone()).or_insert(0);
                let unique_name = format!(".Lnum_{}_{}", name, *count);
                *count += 1;
                defs.entry(name.clone()).or_default().push((i, unique_name));
            }
        }
    }

    // If no numeric labels found, return original
    if defs.is_empty() {
        return statements.to_vec();
    }

    // Second pass: resolve all references
    let mut result = Vec::with_capacity(statements.len());
    for (i, stmt) in statements.iter().enumerate() {
        match stmt {
            AsmStatement::Label(name) if is_numeric_label(name) => {
                if let Some(def_list) = defs.get(name) {
                    if let Some((_, unique_name)) = def_list.iter().find(|(idx, _)| *idx == i) {
                        result.push(AsmStatement::Label(unique_name.clone()));
                        continue;
                    }
                }
                result.push(stmt.clone());
            }
            AsmStatement::Instruction { mnemonic, operands, raw_operands } => {
                let new_ops: Vec<Operand> = operands.iter().map(|op| {
                    resolve_numeric_operand(op, i, &defs)
                }).collect();
                result.push(AsmStatement::Instruction {
                    mnemonic: mnemonic.clone(),
                    operands: new_ops,
                    raw_operands: raw_operands.clone(),
                });
            }
            AsmStatement::Directive(dir) => {
                result.push(AsmStatement::Directive(resolve_numeric_directive(dir, i, &defs)));
            }
            _ => result.push(stmt.clone()),
        }
    }

    result
}

/// Resolve numeric label references in a single operand.
fn resolve_numeric_operand(
    op: &Operand,
    current_idx: usize,
    defs: &HashMap<String, Vec<(usize, String)>>,
) -> Operand {
    match op {
        Operand::Symbol(name) => {
            if let Some(resolved) = resolve_numeric_name(name, current_idx, defs) {
                Operand::Symbol(resolved)
            } else {
                op.clone()
            }
        }
        Operand::Label(name) => {
            if let Some(resolved) = resolve_numeric_name(name, current_idx, defs) {
                Operand::Label(resolved)
            } else {
                op.clone()
            }
        }
        Operand::SymbolOffset(name, off) => {
            if let Some(resolved) = resolve_numeric_name(name, current_idx, defs) {
                Operand::SymbolOffset(resolved, *off)
            } else {
                op.clone()
            }
        }
        Operand::MemExpr { base, expr, writeback } => {
            let resolved_expr = resolve_numeric_refs_in_expr(expr, current_idx, defs);
            Operand::MemExpr { base: base.clone(), expr: resolved_expr, writeback: *writeback }
        }
        Operand::Expr(expr) => {
            let resolved_expr = resolve_numeric_refs_in_expr(expr, current_idx, defs);
            Operand::Expr(resolved_expr)
        }
        _ => op.clone(),
    }
}

/// Resolve numeric label references (e.g., `1b`, `2f`) within an expression string.
/// Replaces each occurrence with the resolved unique label name (e.g., `.Lnum_1_3`).
fn resolve_numeric_refs_in_expr(
    expr: &str,
    current_idx: usize,
    defs: &HashMap<String, Vec<(usize, String)>>,
) -> String {
    let bytes = expr.as_bytes();
    let mut result = String::with_capacity(expr.len());
    let mut i = 0;
    while i < bytes.len() {
        if bytes[i].is_ascii_digit() {
            let start = i;
            // Skip hex literals (0x..., 0X...) and binary literals (0b..., 0B...)
            // to avoid misinterpreting hex digits as label refs (e.g., 0x1b)
            if bytes[i] == b'0' && i + 1 < bytes.len()
                && (bytes[i + 1] == b'x' || bytes[i + 1] == b'X'
                    || bytes[i + 1] == b'b' || bytes[i + 1] == b'B')
            {
                // Consume the entire hex/binary literal
                i += 2; // skip 0x or 0b
                while i < bytes.len() && bytes[i].is_ascii_alphanumeric() {
                    i += 1;
                }
                result.push_str(&expr[start..i]);
                continue;
            }
            while i < bytes.len() && bytes[i].is_ascii_digit() {
                i += 1;
            }
            // Check if followed by 'b' or 'f' (not part of a longer identifier)
            if i < bytes.len()
                && (bytes[i] == b'b' || bytes[i] == b'f' || bytes[i] == b'B' || bytes[i] == b'F')
                && (i + 1 >= bytes.len() || !bytes[i + 1].is_ascii_alphanumeric())
            {
                let label_ref = &expr[start..=i];
                i += 1;
                if let Some(resolved) = resolve_numeric_name(label_ref, current_idx, defs) {
                    result.push_str(&resolved);
                } else {
                    result.push_str(label_ref);
                }
            } else {
                // Regular number
                result.push_str(&expr[start..i]);
            }
        } else {
            result.push(bytes[i] as char);
            i += 1;
        }
    }
    result
}

/// Resolve numeric label references in data directives.
fn resolve_numeric_directive(
    dir: &AsmDirective,
    current_idx: usize,
    defs: &HashMap<String, Vec<(usize, String)>>,
) -> AsmDirective {
    match dir {
        AsmDirective::Byte(vals) => {
            AsmDirective::Byte(resolve_numeric_data_values(vals, current_idx, defs))
        }
        AsmDirective::Long(vals) => {
            AsmDirective::Long(resolve_numeric_data_values(vals, current_idx, defs))
        }
        AsmDirective::Quad(vals) => {
            AsmDirective::Quad(resolve_numeric_data_values(vals, current_idx, defs))
        }
        _ => dir.clone(),
    }
}

/// Resolve numeric label references in data values.
fn resolve_numeric_data_values(
    vals: &[DataValue],
    current_idx: usize,
    defs: &HashMap<String, Vec<(usize, String)>>,
) -> Vec<DataValue> {
    vals.iter().map(|v| {
        match v {
            DataValue::Symbol(name) => {
                if let Some(resolved) = resolve_numeric_name(name, current_idx, defs) {
                    DataValue::Symbol(resolved)
                } else {
                    v.clone()
                }
            }
            DataValue::SymbolOffset(name, off) => {
                if let Some(resolved) = resolve_numeric_name(name, current_idx, defs) {
                    DataValue::SymbolOffset(resolved, *off)
                } else {
                    v.clone()
                }
            }
            DataValue::SymbolDiff(a, b) => {
                let new_a = resolve_numeric_name(a, current_idx, defs).unwrap_or_else(|| a.clone());
                let new_b = resolve_numeric_name(b, current_idx, defs).unwrap_or_else(|| b.clone());
                DataValue::SymbolDiff(new_a, new_b)
            }
            DataValue::SymbolDiffAddend(a, b, add) => {
                let new_a = resolve_numeric_name(a, current_idx, defs).unwrap_or_else(|| a.clone());
                let new_b = resolve_numeric_name(b, current_idx, defs).unwrap_or_else(|| b.clone());
                DataValue::SymbolDiffAddend(new_a, new_b, *add)
            }
            _ => v.clone(),
        }
    }).collect()
}

#[cfg(test)]
mod tests {
    use super::parser::{parse_asm, AsmStatement};
    use super::encoder::{encode_instruction, EncodeResult, RelocType};

    /// Instruction forms the AArch64 codegen and peephole optimizer emit
    /// (logical immediates, register-offset loads/stores, csel, memcpy
    /// chunks), checked against encodings from clang/LLVM.
    #[test]
    fn test_codegen_instruction_forms_match_llvm() {
        let cases = [
            ("and w0, w1, #-8", 0x121d7020u32),
            ("and x11, x1, #63", 0x9240142b),
            ("orr x3, x4, #0xff00", 0xb2781c83),
            ("eor w5, w6, #1", 0x520000c5),
            ("and x7, x8, #-8", 0x927df107),
            ("ldr x12, [x14, #112]", 0xf94039cc),
            ("ldrsw x22, [x21, #184]", 0xb980bab6),
            ("ldrsb x12, [x15]", 0x398001ec),
            ("ldrb w3, [x4, #120]", 0x3941e083),
            ("ldrh w3, [x4, #6]", 0x79400c83),
            ("str w13, [x21, #192]", 0xb900c2ad),
            ("strb wzr, [x15, #1]", 0x390005ff),
            ("strh w2, [x3, #2]", 0x79000462),
            ("str xzr, [x22, #1016]", 0xf901fedf),
            ("add x15, x11, x0", 0x8b00016f),
            ("sub x3, x4, #8", 0xd1002083),
            ("csel x0, x2, x3, le", 0x9a83d040),
            ("csel x15, x2, x0, gt", 0x9a80c04f),
            ("sxtw x23, w11", 0x93407d77),
            ("mov x3, x0", 0xaa0003e3),
            ("ldp x12, x13, [x10], #16", 0xa8c1354c),
            ("stp x12, x13, [x9], #16", 0xa881352c),
            ("subs x11, x11, #1", 0xf100056b),
            // Shifted-register operands and register-offset addressing
            // (peephole shift/index folding).
            ("orr w13, w12, w0, lsl #6", 0x2a00198d),
            ("add x14, x15, x11, lsl #2", 0x8b0b09ee),
            ("sub x3, x4, x5, asr #7", 0xcb851c83),
            ("eor x1, x2, x3, lsr #1", 0xca430441),
            ("ldr x10, [x4, x15, lsl #3]", 0xf86f788a),
            ("ldrsw x1, [x2, x3, lsl #2]", 0xb8a37841),
            ("ldrb w1, [x2, x3]", 0x38636841),
            ("strb w1, [x2, x3]", 0x38236841),
            ("ldrh w5, [x6, x7, lsl #1]", 0x786778c5),
            ("str w13, [x14, x11, lsl #2]", 0xb82b79cd),
            ("ldr d0, [x1, x2, lsl #3]", 0xfc627820),
            ("ldr s1, [x1, x2, lsl #2]", 0xbc627821),
            ("str x9, [x10, x11]", 0xf82b6949),
            ("ldrsb x3, [x4, x5]", 0x38a56883),
        ];
        for (text, expected) in cases {
            let stmts = parse_asm(text).unwrap_or_else(|e| panic!("{}: {}", text, e));
            let Some(AsmStatement::Instruction { mnemonic, operands, raw_operands }) =
                stmts.into_iter().find(|s| matches!(s, AsmStatement::Instruction { .. }))
            else {
                panic!("{}: not parsed as an instruction", text)
            };
            match encode_instruction(&mnemonic, &operands, &raw_operands) {
                Ok(EncodeResult::Word(w)) => assert_eq!(w, expected, "{}: got {:#010x}", text, w),
                other => panic!("{}: unexpected encoding {:?}", text, other.map(|_| ())),
            }
        }
    }

    /// Forms added by the later AArch64 optimizations: fused multiply-adds
    /// (scalar and vector), bitwise selects, shift-and-accumulate and
    /// immediate shifts, movi broadcasts, bic/mvn/fneg, w-register neg/mvn,
    /// register-offset q/d accesses, compares with shifted 12-bit immediates
    /// and lane inserts, checked against encodings from clang/LLVM.
    #[test]
    fn test_optimized_instruction_forms_match_llvm() {
        let cases = [
            ("fmla v28.4s, v27.4s, v1.4s", 0x4e21cf7cu32),
            ("fmla v28.2d, v0.2d, v27.2d", 0x4e7bcc1c),
            ("fmls v3.4s, v2.4s, v1.4s", 0x4ea1cc43),
            ("fmls v24.2d, v25.2d, v26.2d", 0x4efacf38),
            ("fmadd d16, d0, d1, d2", 0x1f410810),
            ("fmsub d16, d0, d1, d2", 0x1f418810),
            ("fnmsub d16, d0, d1, d2", 0x1f618810),
            ("fnmadd d16, d0, d1, d2", 0x1f610810),
            ("fmadd s17, s0, s1, s16", 0x1f014011),
            ("fnmsub s5, s6, s7, s8", 0x1f27a0c5),
            ("bsl v24.16b, v27.16b, v26.16b", 0x6e7a1f78),
            ("bit v20.16b, v21.16b, v22.16b", 0x6eb61eb4),
            ("bif v23.8b, v24.8b, v25.8b", 0x2ef91f17),
            ("usra v28.8b, v0.8b, #2", 0x2f0e141c),
            ("usra v28.4s, v1.4s, #17", 0x6f2f143c),
            ("ssra v20.8h, v21.8h, #15", 0x4f1116b4),
            ("ssra v19.2d, v18.2d, #63", 0x4f411653),
            ("ushr v28.8b, v0.8b, #1", 0x2f0f041c),
            ("sshr v2.4s, v3.4s, #31", 0x4f210462),
            ("ushr v4.2d, v5.2d, #63", 0x6f4104a4),
            ("shl v28.4s, v27.4s, #13", 0x4f2d577c),
            ("shl v6.16b, v7.16b, #7", 0x4f0f54e6),
            ("shl v8.2d, v9.2d, #32", 0x4f605528),
            ("sshr v10.8h, v11.8h, #4", 0x4f1c056a),
            ("movi v1.2s, #3", 0x0f000461),
            ("movi v1.4s, #250", 0x4f070741),
            ("movi v2.16b, #0", 0x4f00e402),
            ("movi v3.8b, #255", 0x0f07e7e3),
            ("movi v4.4h, #200", 0x0f068504),
            ("movi v5.8h, #7", 0x4f0084e5),
            ("bic v29.16b, v26.16b, v25.16b", 0x4e791f5d),
            ("bic v28.8b, v24.8b, v28.8b", 0x0e7c1f1c),
            ("mvn v28.16b, v28.16b", 0x6e205b9c),
            ("fneg v29.4s, v21.4s", 0x6ea0fabd),
            ("fneg v28.2d, v28.2d", 0x6ee0fb9c),
            ("neg w0, w10", 0x4b0a03e0),
            ("mvn w0, w0", 0x2a2003e0),
            ("ldr q1, [x21, x20]", 0x3cf46aa1),
            ("ldr d0, [x22, x10]", 0xfc6a6ac0),
            ("str q28, [x21, x20]", 0x3cb46abc),
            ("str d28, [x23, x20]", 0xfc346afc),
            ("ldr q1, [x22, x20, lsl #4]", 0x3cf47ac1),
            ("ldr d1, [x21, x20, lsl #3]", 0xfc747aa1),
            ("cmp x20, #65536", 0xf140429f),
            ("cmp x10, #32768", 0xf140215f),
            ("cmp w20, #4096", 0x7140069f),
            ("cmp x10, x21", 0xeb15015f),
            ("fcmgt v25.4s, v27.4s, v26.4s", 0x6ebae779),
            ("fcmgt v22.4s, v17.4s, v24.4s", 0x6eb8e636),
            ("ins v27.s[1], w0", 0x4e0c1c1b),
            ("dup v27.4s, w10", 0x4e040d5b),
            ("and w0, w10, #7", 0x12000940),
        ];
        let mut failures = Vec::new();
        for (text, expected) in cases {
            let stmts = parse_asm(text).unwrap_or_else(|e| panic!("{}: {}", text, e));
            let Some(AsmStatement::Instruction { mnemonic, operands, raw_operands }) =
                stmts.into_iter().find(|s| matches!(s, AsmStatement::Instruction { .. }))
            else {
                panic!("{}: not parsed as an instruction", text)
            };
            match encode_instruction(&mnemonic, &operands, &raw_operands) {
                Ok(EncodeResult::Word(w)) if w == expected => {}
                Ok(EncodeResult::Word(w)) => failures.push(format!("{text}: got {w:#010x}, want {expected:#010x}")),
                Ok(_) => failures.push(format!("{text}: not a single word")),
                Err(e) => failures.push(format!("{text}: {e}")),
            }
        }
        assert!(failures.is_empty(), "\n{}", failures.join("\n"));
    }

    /// NEON forms the vector-extension codegen emits (whole-vector ops on
    /// every arrangement, dup from a GPR, lane moves, q/d loads and stores),
    /// checked against encodings from clang/LLVM.
    #[test]
    fn test_vector_instruction_forms_match_llvm() {
        let cases = [
            ("dup v1.4s, w0", 0x4e040c01u32),
            ("add v27.4s, v0.4s, v1.4s", 0x4ea1841b),
            ("fmul v27.4s, v0.4s, v1.4s", 0x6e21dc1b),
            ("ins v26.b[0], w0", 0x4e011c1a),
            ("dup v1.16b, w0", 0x4e010c01),
            ("mul v28.16b, v26.16b, v1.16b", 0x4e219f5c),
            ("orr v27.16b, v28.16b, v28.16b", 0x4ebc1f9b),
            ("add v28.16b, v27.16b, v25.16b", 0x4e39877c),
            ("sub v28.16b, v27.16b, v25.16b", 0x6e39877c),
            ("and v28.16b, v27.16b, v25.16b", 0x4e391f7c),
            ("eor v28.16b, v27.16b, v25.16b", 0x6e391f7c),
            ("sshl v28.16b, v27.16b, v24.16b", 0x4e38477c),
            ("neg v1.16b, v23.16b", 0x6e20bae1),
            ("cmeq v28.16b, v27.16b, v25.16b", 0x6e398f7c),
            ("mvn v28.16b, v28.16b", 0x6e205b9c),
            ("cmgt v28.16b, v25.16b, v27.16b", 0x4e3b373c),
            ("cmge v28.16b, v25.16b, v27.16b", 0x4e3b3f3c),
            ("ushl v28.16b, v21.16b, v1.16b", 0x6e2146bc),
            ("cmhi v28.16b, v1.16b, v21.16b", 0x6e35343c),
            ("cmhs v28.16b, v1.16b, v21.16b", 0x6e353c3c),
            ("dup v1.8h, w0", 0x4e020c01),
            ("mul v28.8h, v0.8h, v1.8h", 0x4e619c1c),
            ("add v28.8h, v0.8h, v1.8h", 0x4e61841c),
            ("sub v28.8h, v0.8h, v1.8h", 0x6e61841c),
            ("sshl v28.8h, v0.8h, v1.8h", 0x4e61441c),
            ("neg v1.8h, v1.8h", 0x6e60b821),
            ("cmeq v28.8h, v0.8h, v1.8h", 0x6e618c1c),
            ("cmgt v28.8h, v1.8h, v0.8h", 0x4e60343c),
            ("cmge v28.8h, v1.8h, v0.8h", 0x4e603c3c),
            ("ushl v28.8h, v0.8h, v1.8h", 0x6e61441c),
            ("cmhi v28.8h, v1.8h, v0.8h", 0x6e60343c),
            ("cmhs v28.8h, v1.8h, v0.8h", 0x6e603c3c),
            ("mul v28.4s, v0.4s, v1.4s", 0x4ea19c1c),
            ("sub v28.4s, v0.4s, v1.4s", 0x6ea1841c),
            ("sshl v28.4s, v0.4s, v1.4s", 0x4ea1441c),
            ("neg v1.4s, v1.4s", 0x6ea0b821),
            ("cmeq v28.4s, v0.4s, v1.4s", 0x6ea18c1c),
            ("cmgt v28.4s, v1.4s, v0.4s", 0x4ea0343c),
            ("cmge v28.4s, v1.4s, v0.4s", 0x4ea03c3c),
            ("ushl v28.4s, v0.4s, v1.4s", 0x6ea1441c),
            ("cmhi v28.4s, v1.4s, v0.4s", 0x6ea0343c),
            ("cmhs v28.4s, v1.4s, v0.4s", 0x6ea03c3c),
            ("add v28.2d, v0.2d, v1.2d", 0x4ee1841c),
            ("sub v28.2d, v0.2d, v1.2d", 0x6ee1841c),
            ("sshl v28.2d, v0.2d, v1.2d", 0x4ee1441c),
            ("neg v1.2d, v1.2d", 0x6ee0b821),
            ("dup v1.2d, x0", 0x4e080c01),
            ("cmeq v28.2d, v0.2d, v1.2d", 0x6ee18c1c),
            ("cmgt v28.2d, v1.2d, v0.2d", 0x4ee0343c),
            ("cmge v28.2d, v1.2d, v0.2d", 0x4ee03c3c),
            ("ushl v28.2d, v0.2d, v1.2d", 0x6ee1441c),
            ("cmhi v28.2d, v1.2d, v0.2d", 0x6ee0343c),
            ("cmhs v28.2d, v1.2d, v0.2d", 0x6ee03c3c),
            ("fadd v28.4s, v0.4s, v1.4s", 0x4e21d41c),
            ("fsub v28.4s, v0.4s, v1.4s", 0x4ea1d41c),
            ("fdiv v28.4s, v0.4s, v1.4s", 0x6e21fc1c),
            ("fcmeq v28.4s, v0.4s, v1.4s", 0x4e21e41c),
            ("fcmgt v28.4s, v1.4s, v0.4s", 0x6ea0e43c),
            ("fcmge v28.4s, v1.4s, v0.4s", 0x6e20e43c),
            ("fneg v28.4s, v0.4s", 0x6ea0f81c),
            ("fmul v28.2d, v0.2d, v1.2d", 0x6e61dc1c),
            ("fadd v28.2d, v0.2d, v1.2d", 0x4e61d41c),
            ("fsub v28.2d, v0.2d, v1.2d", 0x4ee1d41c),
            ("fdiv v28.2d, v0.2d, v1.2d", 0x6e61fc1c),
            ("fcmeq v28.2d, v0.2d, v1.2d", 0x4e61e41c),
            ("fcmgt v28.2d, v1.2d, v0.2d", 0x6ee0e43c),
            ("fcmge v28.2d, v1.2d, v0.2d", 0x6e60e43c),
            ("fneg v28.2d, v0.2d", 0x6ee0f81c),
            ("ins v27.s[0], w0", 0x4e041c1b),
            ("ins v22.d[0], x0", 0x4e081c16),
            ("add v28.8b, v0.8b, v1.8b", 0x0e21841c),
            ("mul v28.4h, v0.4h, v1.4h", 0x0e619c1c),
            ("fadd v28.2s, v0.2s, v1.2s", 0x0e21d41c),
            ("fcmgt v28.2s, v1.2s, v0.2s", 0x2ea0e43c),
            ("cmhi v28.4h, v1.4h, v0.4h", 0x2e60343c),
            ("dup v1.2s, w0", 0x0e040c01),
            ("dup v1.8b, w0", 0x0e010c01),
            ("dup v1.4h, w0", 0x0e020c01),
            ("neg v1.2s, v1.2s", 0x2ea0b821),
            ("ushl v28.2s, v0.2s, v1.2s", 0x2ea1441c),
            ("orr v20.8b, v28.8b, v28.8b", 0x0ebc1f94),
            ("mvn v28.8b, v28.8b", 0x2e205b9c),
            ("and v28.8b, v0.8b, v1.8b", 0x0e211c1c),
            ("umov w0, v27.s[1]", 0x0e0c3f60),
            ("umov x0, v27.d[1]", 0x4e183f60),
            ("umov w0, v27.b[3]", 0x0e073f60),
            ("umov w0, v27.h[2]", 0x0e0a3f60),
            ("ins v27.h[1], w0", 0x4e061c1b),
            ("ins v21.d[1], x0", 0x4e181c15),
            ("sxtb x0, w0", 0x93401c00),
            ("sxth x0, w0", 0x93403c00),
            ("ldr q27, [sp, #384]", 0x3dc063fb),
            ("str q28, [x22]", 0x3d8002dc),
            ("ldr q1, [x26, x10]", 0x3cea6b41),
            ("ldr d26, [x0]", 0xfd40001a),
            ("str d27, [sp, #16]", 0xfd000bfb),
            ("ldr q0, [x0]", 0x3dc00000),
            ("str q27, [x29, #32]", 0x3d800bbb),
        ];
        let mut failures = Vec::new();
        for (text, expected) in cases {
            let stmts = parse_asm(text).unwrap_or_else(|e| panic!("{}: {}", text, e));
            let Some(AsmStatement::Instruction { mnemonic, operands, raw_operands }) =
                stmts.into_iter().find(|s| matches!(s, AsmStatement::Instruction { .. }))
            else {
                panic!("{}: not parsed as an instruction", text)
            };
            match encode_instruction(&mnemonic, &operands, &raw_operands) {
                Ok(EncodeResult::Word(w)) if w == expected => {}
                Ok(EncodeResult::Word(w)) => failures.push(format!("{}: got {:#010x}, want {:#010x}", text, w, expected)),
                other => failures.push(format!("{}: unexpected encoding {:?}", text, other.map(|_| ()))),
            }
        }
        assert!(failures.is_empty(), "{}", failures.join("\n"));
    }

    /// Global addresses with a constant offset folded into the relocation
    /// addend (peephole `fold_offset_into_global_addr`). Words match LLVM's
    /// unrelocated encodings.
    #[test]
    fn test_global_addr_offset_relocations() {
        let cases = [
            ("adrp x14, rays+512", 0x9000000eu32, true),
            ("add x14, x14, :lo12:rays+512", 0x910001ce, false),
        ];
        for (text, expected, is_adrp) in cases {
            let stmts = parse_asm(text).unwrap_or_else(|e| panic!("{}: {}", text, e));
            let Some(AsmStatement::Instruction { mnemonic, operands, raw_operands }) =
                stmts.into_iter().find(|s| matches!(s, AsmStatement::Instruction { .. }))
            else {
                panic!("{}: not parsed as an instruction", text)
            };
            match encode_instruction(&mnemonic, &operands, &raw_operands) {
                Ok(EncodeResult::WordWithReloc { word, reloc }) => {
                    assert_eq!(word, expected, "{}: got {:#010x}", text, word);
                    assert_eq!(reloc.symbol, "rays", "{}", text);
                    assert_eq!(reloc.addend, 512, "{}", text);
                    if is_adrp {
                        assert!(matches!(reloc.reloc_type, RelocType::AdrpPage21), "{}", text);
                    } else {
                        assert!(matches!(reloc.reloc_type, RelocType::AddAbsLo12), "{}", text);
                    }
                }
                other => panic!("{}: unexpected encoding {:?}", text, other.map(|_| ())),
            }
        }
    }
}
