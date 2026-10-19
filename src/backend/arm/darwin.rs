//! Darwin (arm64 macOS) support: Mach-O assembly translation and host toolchain.
//!
//! The AArch64 codegen emits GNU/ELF-flavored assembly. On Darwin we rewrite
//! that text into Apple assembler syntax and hand it to the host's clang
//! (LLVM integrated assembler) and ld64, instead of the builtin ELF assembler
//! and linker. The rewrite covers:
//!
//! - C symbol names get the Mach-O `_` prefix; `.L` local labels become `L`
//! - `:lo12:` / `:got:` / `:got_lo12:` relocation operators become
//!   `@PAGEOFF` / `@GOTPAGE` / `@GOTPAGEOFF`, and `adrp x, sym` gets `@PAGE`
//! - ELF section names map to Mach-O `segment,section` pairs
//! - ELF-only directives (`.type`, `.size`, `.protected`, ...) are dropped,
//!   and `.hidden` / `.weak` / `.comm` / `.local` map to their Mach-O forms
//!
//! ABI differences (variadic calls, va_list, long double) are handled in the
//! codegen and frontend, keyed off `common::types::target_is_darwin()`.

use std::path::PathBuf;
use std::process::Command;

use crate::common::fx_hash::FxHashSet;
use crate::common::temp_files::TempFile;

/// Translate GNU/ELF AArch64 assembly text into Apple Mach-O assembly.
pub(crate) fn translate_to_macho(asm: &str) -> String {
    let symbols = SymbolInfo::collect(asm);
    let mut state = State::default();
    let mut out = String::with_capacity(asm.len() + asm.len() / 8);
    for line in asm.lines() {
        translate_line(line, &symbols, &mut state, &mut out);
    }
    state.emit_trailer(&mut out);
    out
}

/// What the current section needs beyond a plain rename.
#[derive(Default, Clone, Copy, PartialEq)]
enum SectionKind {
    #[default]
    Plain,
    /// .tdata/.tbss: labels define a thread-local's initial image.
    ThreadLocal,
    /// .fini_array/.dtors: entries are destructor function pointers.
    Destructors,
    /// .bss: each object is a label followed by `.zero N`.
    ZeroFill,
}

/// Translation state carried across lines.
#[derive(Default)]
struct State {
    section: SectionKind,
    /// Thread-locals: code references the TLV descriptor `_x`, emitted in
    /// __thread_vars at the end; `_x$tlv$init` holds the initial image.
    tls_vars: Vec<String>,
    /// Destructor functions. ld64 ignores __mod_term_func, so (like clang)
    /// we register them with __cxa_atexit from a generated initializer.
    destructors: Vec<String>,
}

impl State {
    fn emit_trailer(&self, out: &mut String) {
        if !self.tls_vars.is_empty() {
            emit(out, ".section __DATA,__thread_vars,thread_local_variables");
            for name in &self.tls_vars {
                let sym = macho_sym(name);
                emit(out, ".p2align 3");
                emit(out, &format!("{}:", sym));
                emit(out, "    .quad __tlv_bootstrap");
                emit(out, "    .quad 0");
                emit(out, &format!("    .quad {}$tlv$init", sym));
            }
        }
        if !self.destructors.is_empty() {
            emit_text_section(out);
            emit(out, "Lccc_register_dtors:");
            emit(out, "    stp x29, x30, [sp, #-16]!");
            emit(out, "    mov x29, sp");
            // Register in order: atexit runs them in reverse, like .fini_array.
            for dtor in &self.destructors {
                emit(out, &format!("    adrp x0, {}@GOTPAGE", dtor));
                emit(out, &format!("    ldr x0, [x0, {}@GOTPAGEOFF]", dtor));
                emit(out, "    mov x1, #0");
                emit(out, "    adrp x2, ___dso_handle@GOTPAGE");
                emit(out, "    ldr x2, [x2, ___dso_handle@GOTPAGEOFF]");
                emit(out, "    bl ___cxa_atexit");
            }
            emit(out, "    ldp x29, x30, [sp], #16");
            emit(out, "    ret");
            emit(out, ".section __DATA,__mod_init_func,mod_init_funcs");
            emit(out, ".p2align 3");
            emit(out, "    .quad Lccc_register_dtors");
        }
    }
}

/// Symbols defined in (and declared local to) the translation unit.
struct SymbolInfo {
    defined: FxHashSet<String>,
    local: FxHashSet<String>,
}

impl SymbolInfo {
    fn collect(asm: &str) -> Self {
        let mut defined = FxHashSet::default();
        let mut local = FxHashSet::default();
        for line in asm.lines() {
            let t = line.trim();
            if let Some(name) = label_name(t) {
                defined.insert(name.to_string());
            } else if let Some(rest) = t.strip_prefix(".comm ") {
                if let Some(name) = rest.split(',').next() {
                    defined.insert(name.trim().to_string());
                }
            } else if let Some(rest) = t.strip_prefix(".set ") {
                if let Some(name) = rest.split(',').next() {
                    defined.insert(name.trim().to_string());
                }
            } else if let Some(rest) = t.strip_prefix(".local ") {
                local.insert(rest.trim().to_string());
            }
        }
        SymbolInfo { defined, local }
    }
}

/// If `t` is a label definition (`name:`), return the name.
fn label_name(t: &str) -> Option<&str> {
    let name = t.strip_suffix(':')?;
    if !name.is_empty() && name.chars().all(is_ident_char) && !name.starts_with(|c: char| c.is_ascii_digit()) {
        Some(name)
    } else {
        None
    }
}

fn is_ident_start(c: char) -> bool {
    c.is_ascii_alphabetic() || c == '_' || c == '.' || c == '$'
}

fn is_ident_char(c: char) -> bool {
    c.is_ascii_alphanumeric() || c == '_' || c == '.' || c == '$'
}

/// Mach-O spelling of a symbol: `.Lfoo` -> `Lfoo`, `foo` -> `_foo`.
fn macho_sym(name: &str) -> String {
    if let Some(rest) = name.strip_prefix(".L") {
        format!("L{}", rest)
    } else {
        format!("_{}", name)
    }
}

/// Rewrite every symbol reference in an assembler expression
/// (e.g. `foo+8`, `.Lcase - .Ltable`), leaving numbers and `.` alone.
fn symbolize_expr(expr: &str) -> String {
    let mut out = String::with_capacity(expr.len() + 4);
    let chars: Vec<char> = expr.chars().collect();
    let mut i = 0;
    while i < chars.len() {
        let c = chars[i];
        if c.is_ascii_digit() {
            // Number literal (decimal, hex, or a numeric local label like `1f`)
            while i < chars.len() && chars[i].is_ascii_alphanumeric() {
                out.push(chars[i]);
                i += 1;
            }
        } else if c == '"' {
            // String literal: copy verbatim
            out.push(c);
            i += 1;
            while i < chars.len() {
                out.push(chars[i]);
                if chars[i] == '\\' && i + 1 < chars.len() {
                    out.push(chars[i + 1]);
                    i += 2;
                    continue;
                }
                i += 1;
                if chars[i - 1] == '"' {
                    break;
                }
            }
        } else if is_ident_start(c) {
            let start = i;
            while i < chars.len() && is_ident_char(chars[i]) {
                i += 1;
            }
            let ident: String = chars[start..i].iter().collect();
            if ident == "." {
                out.push('.');
            } else {
                out.push_str(&macho_sym(&ident));
            }
        } else {
            out.push(c);
            i += 1;
        }
    }
    out
}

fn translate_line(line: &str, syms: &SymbolInfo, state: &mut State, out: &mut String) {
    let t = line.trim();
    if t.is_empty() {
        out.push('\n');
        return;
    }
    if let Some(name) = label_name(t) {
        out.push_str(&macho_sym(name));
        if state.section == SectionKind::ThreadLocal {
            out.push_str("$tlv$init");
            state.tls_vars.push(name.to_string());
        }
        out.push_str(":\n");
        return;
    }
    if t.starts_with('.') {
        translate_directive(line, t, syms, state, out);
    } else {
        out.push_str(&translate_instruction(line));
        out.push('\n');
    }
}

/// Split a directive line into its name and argument text.
fn split_directive(t: &str) -> (&str, &str) {
    match t.find(|c: char| c.is_whitespace()) {
        Some(pos) => (&t[..pos], t[pos..].trim()),
        None => (t, ""),
    }
}

fn emit(out: &mut String, s: &str) {
    out.push_str(s);
    out.push('\n');
}

fn translate_directive(line: &str, t: &str, syms: &SymbolInfo, state: &mut State, out: &mut String) {
    let (name, args) = split_directive(t);
    if matches!(name, ".text" | ".data" | ".bss" | ".section") {
        state.section = SectionKind::Plain;
    }
    if state.section == SectionKind::Destructors {
        match name {
            ".quad" | ".xword" | ".dword" => state.destructors.push(symbolize_expr(args)),
            ".align" | ".p2align" => {}
            _ => emit(out, line),
        }
        return;
    }
    match name {
        // ELF-only metadata with no Mach-O equivalent.
        ".type" | ".size" | ".protected" | ".internal" | ".symver" | ".local"
        | ".ident" | ".addrsig" | ".addrsig_sym" => {}
        ".text" => emit_text_section(out),
        ".data" => emit(out, ".data"),
        ".bss" => {
            emit(out, ".section __DATA,__bss");
            state.section = SectionKind::ZeroFill;
        }
        // A zero-size object would leave relocations pointing at an empty
        // zero-fill section, which ld64 rejects; give it a byte.
        ".zero" if args == "0" && state.section == SectionKind::ZeroFill => emit(out, "    .zero 1"),
        ".section" => state.section = translate_section(args, out),
        ".globl" | ".global" => emit(out, &format!(".globl {}", macho_sym(args))),
        ".hidden" => emit(out, &format!(".private_extern {}", macho_sym(args))),
        ".weak" => {
            let sym = macho_sym(args);
            if syms.defined.contains(args) {
                emit(out, &format!(".globl {}", sym));
                emit(out, &format!(".weak_definition {}", sym));
            } else {
                emit(out, &format!(".weak_reference {}", sym));
            }
        }
        ".comm" => {
            // ELF: .comm sym,size,align_bytes. Mach-O: alignment is log2.
            let parts: Vec<&str> = args.split(',').map(str::trim).collect();
            let sym = parts.first().copied().unwrap_or("");
            let size = parts.get(1).copied().unwrap_or("0");
            let align: u64 = parts.get(2).and_then(|a| a.parse().ok()).unwrap_or(1);
            let log2 = if align <= 1 { 0 } else { align.trailing_zeros() };
            if syms.local.contains(sym) {
                emit(out, &format!(".zerofill __DATA,__bss,{},{},{}", macho_sym(sym), size, log2));
            } else {
                emit(out, &format!(".comm {},{},{}", macho_sym(sym), size, log2));
            }
        }
        ".set" | ".equ" => {
            if let Some((lhs, rhs)) = args.split_once(',') {
                emit(out, &format!(".set {}, {}", macho_sym(lhs.trim()), symbolize_expr(rhs.trim())));
            }
        }
        // GNU AArch64 `.align` is a power-of-two exponent, like `.p2align`.
        ".align" => emit(out, &format!(".p2align {}", args)),
        ".xword" | ".dword" => emit(out, &format!("    .quad {}", symbolize_expr(args))),
        ".word" => emit(out, &format!("    .long {}", symbolize_expr(args))),
        ".quad" | ".long" | ".short" | ".hword" | ".byte" | ".4byte" | ".8byte" | ".2byte" => {
            emit(out, &format!("    {} {}", name, symbolize_expr(args)))
        }
        ".string" => emit(out, &format!("    .asciz {}", args)),
        ".cfi_personality" | ".cfi_lsda" => {
            if let Some((enc, sym)) = args.split_once(',') {
                emit(out, &format!("{} {}, {}", name, enc.trim(), symbolize_expr(sym.trim())));
            }
        }
        // Everything else (.cfi_*, .loc, .file, .ascii, .asciz, .zero, .p2align,
        // .balign, .inst, ...) is spelled the same in both syntaxes.
        _ => emit(out, line),
    }
}

fn emit_text_section(out: &mut String) {
    emit(out, ".text");
    emit(out, ".p2align 2");
}

/// Map an ELF `.section name[,flags,type]` to a Mach-O section.
fn translate_section(args: &str, out: &mut String) -> SectionKind {
    let sect = args.split(',').next().unwrap_or("").trim().trim_matches('"');
    let starts = |p: &str| sect == p || sect.starts_with(&format!("{}.", p));
    if starts(".tdata") {
        emit(out, ".section __DATA,__thread_data,thread_local_regular");
        return SectionKind::ThreadLocal;
    } else if starts(".tbss") {
        emit(out, ".section __DATA,__thread_bss,thread_local_zerofill");
        return SectionKind::ThreadLocal;
    } else if starts(".fini_array") || starts(".dtors") {
        return SectionKind::Destructors;
    } else if starts(".text") {
        emit_text_section(out);
    } else if starts(".rodata") || starts(".data.rel.ro") {
        // __DATA,__const rather than __TEXT,__const: read-only data may hold
        // pointers, which need load-time fixups.
        emit(out, ".section __DATA,__const");
    } else if starts(".data") {
        emit(out, ".data");
    } else if starts(".bss") {
        emit(out, ".section __DATA,__bss");
        return SectionKind::ZeroFill;
    } else if starts(".init_array") || starts(".ctors") {
        emit(out, ".section __DATA,__mod_init_func,mod_init_funcs");
    } else if sect.starts_with(".note") || sect.starts_with(".comment") {
        // ELF notes (e.g. the trailing empty .note.GNU-stack) have no Mach-O
        // meaning; any contents are parked in an unreferenced section.
        emit(out, ".section __DATA,__ccc_note");
    } else if sect.starts_with("__") && sect.contains(',') {
        // Already a Mach-O segment,section pair (from inline asm or attributes).
        emit(out, &format!(".section {}", args));
    } else {
        // Custom section from __attribute__((section)): Mach-O section names are
        // limited to 16 characters and conventionally lowercase identifiers.
        let mut name: String = sect.trim_start_matches('.').chars()
            .map(|c| if c.is_ascii_alphanumeric() { c } else { '_' })
            .collect();
        name.truncate(14);
        emit(out, &format!(".section __DATA,__{}", name));
    }
    SectionKind::Plain
}

/// Instructions whose last operand is a branch/label target.
fn takes_label_operand(mnemonic: &str) -> bool {
    matches!(mnemonic, "b" | "bl" | "cbz" | "cbnz" | "tbz" | "tbnz" | "adr" | "adrp")
        || mnemonic.starts_with("b.")
}

fn translate_instruction(line: &str) -> String {
    // Relocation operators can appear in any instruction (add, ldr, str, ...).
    let s = rewrite_reloc_ops(line);
    let t = s.trim_start();
    let (mnemonic, operands) = match t.find(|c: char| c.is_whitespace()) {
        Some(p) => (&t[..p], &t[p..]),
        None => (t, ""),
    };
    if !takes_label_operand(mnemonic) {
        return s;
    }
    let pos = operands.rfind(',').map_or(0, |p| p + 1);
    let target = operands[pos..].trim();
    // The last operand of these is always a label, so a name like `fp` or `h0`
    // is a C symbol here, never a register.
    if !target.starts_with(is_ident_start) || target.contains('@') {
        return s;
    }
    let mut sym = symbolize_expr(target);
    if mnemonic == "adrp" {
        sym.push_str("@PAGE");
    }
    let indent = &s[..s.len() - t.len()];
    format!("{}{}{} {}", indent, mnemonic, &operands[..pos], sym)
}

/// Rewrite ELF relocation operators: `:lo12:sym` -> `sym@PAGEOFF`, etc.
fn rewrite_reloc_ops(line: &str) -> String {
    const OPS: &[(&str, &str)] = &[
        (":tlvp_lo12:", "@TLVPPAGEOFF"),
        (":tlvp:", "@TLVPPAGE"),
        (":got_lo12:", "@GOTPAGEOFF"),
        (":got:", "@GOTPAGE"),
        (":lo12:", "@PAGEOFF"),
    ];
    if !line.contains(':') {
        return line.to_string();
    }
    let mut s = line.to_string();
    for (op, suffix) in OPS {
        while let Some(pos) = s.find(op) {
            let sym_start = pos + op.len();
            let sym_end = s[sym_start..]
                .find(|c: char| !is_ident_char(c))
                .map(|e| sym_start + e)
                .unwrap_or(s.len());
            let sym = macho_sym(&s[sym_start..sym_end]);
            s = format!("{}{}{}{}", &s[..pos], sym, suffix, &s[sym_end..]);
        }
    }
    s
}

/// The macOS SDK's `usr/include` (honors `$SDKROOT`, else asks `xcrun`).
pub(crate) fn sdk_include_dir() -> Option<PathBuf> {
    let sdk = match std::env::var("SDKROOT") {
        Ok(s) if !s.is_empty() => s,
        _ => command_stdout("xcrun", &["--show-sdk-path"])?,
    };
    let dir = PathBuf::from(sdk).join("usr/include");
    dir.is_dir().then_some(dir)
}

/// clang's resource headers (stddef.h, stdarg.h, float.h, ...), which the
/// SDK does not ship.
pub(crate) fn clang_include_dir() -> Option<PathBuf> {
    let dir = PathBuf::from(command_stdout("clang", &["-print-resource-dir"])?).join("include");
    dir.is_dir().then_some(dir)
}

fn command_stdout(program: &str, args: &[&str]) -> Option<String> {
    let out = Command::new(program).args(args).output().ok()?;
    if !out.status.success() {
        return None;
    }
    let s = String::from_utf8(out.stdout).ok()?.trim().to_string();
    (!s.is_empty()).then_some(s)
}

/// Assemble Mach-O assembly text with the host clang (LLVM integrated assembler).
pub(crate) fn assemble(asm_text: &str, output_path: &str, extra_args: &[String]) -> Result<(), String> {
    let keep_asm = std::env::var("CCC_KEEP_ASM").is_ok();
    let asm_file = if keep_asm {
        let mut f = TempFile::with_path(format!("{}.s", output_path).into());
        f.set_keep(true);
        f
    } else {
        let stem = std::path::Path::new(output_path)
            .file_stem()
            .and_then(|s| s.to_str())
            .unwrap_or("asm");
        TempFile::new("ccc_asm", stem, "s")
    };
    std::fs::write(asm_file.path(), asm_text)
        .map_err(|e| format!("Failed to write assembly: {}", e))?;
    assemble_file(asm_file.to_str(), output_path, extra_args)
}

/// Assemble a `.s`/`.S` file with the host clang.
pub(crate) fn assemble_file(input: &str, output_path: &str, extra_args: &[String]) -> Result<(), String> {
    let mut cmd = Command::new("clang");
    cmd.args(["-arch", "arm64", "-c", "-o", output_path, input]);
    cmd.args(extra_args);
    run(cmd, "assembler")
}

/// Link objects into an executable or dylib with the host clang/ld64.
pub(crate) fn link(object_files: &[&str], output_path: &str, user_args: &[String]) -> Result<(), String> {
    let mut cmd = Command::new("clang");
    cmd.args(["-arch", "arm64", "-o", output_path]);
    cmd.args(object_files);
    for arg in user_args {
        match arg.as_str() {
            // ld64 cannot produce fully static executables.
            "-static" => {}
            "-shared" => { cmd.arg("-dynamiclib"); }
            _ => { cmd.arg(arg); }
        }
    }
    run(cmd, "linker")
}

fn run(mut cmd: Command, what: &str) -> Result<(), String> {
    let result = cmd.output()
        .map_err(|e| format!("Failed to run {} (clang): {}", what, e))?;
    if !result.status.success() {
        let stderr = String::from_utf8_lossy(&result.stderr);
        return Err(format!("{} failed (clang): {}", what, stderr));
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn rewrites_symbols_and_relocs() {
        let asm = "\
.section .rodata
.Lstr0:
    .byte 104, 105, 0
.section .data
.align 2
.type g, @object
.size g, 4
g:
    .long 5
.section .text
.globl main
.type main, %function
main:
    adrp x0, .Lstr0
    add x0, x0, :lo12:.Lstr0
    adrp x1, :got:stderr
    ldr x1, [x1, :got_lo12:stderr]
    bl puts
    b.eq .LBB2
    cbz w0, .LBB3
    ret
.size main, .-main
";
        let out = translate_to_macho(asm);
        assert!(out.contains("Lstr0:"));
        assert!(out.contains("_g:"));
        assert!(out.contains(".globl _main"));
        assert!(out.contains("adrp x0, Lstr0@PAGE"));
        assert!(out.contains("add x0, x0, Lstr0@PAGEOFF"));
        assert!(out.contains("adrp x1, _stderr@GOTPAGE"));
        assert!(out.contains("ldr x1, [x1, _stderr@GOTPAGEOFF]"));
        assert!(out.contains("bl _puts"));
        assert!(out.contains("b.eq LBB2"));
        assert!(out.contains("cbz w0, LBB3"));
        // C symbols spelled like registers
        assert!(translate_to_macho("    bl h0\n    adrp x0, fp\n").contains("bl _h0\n    adrp x0, _fp@PAGE"));
        assert!(!out.contains(".type"));
        assert!(!out.contains(".size"));
    }

    #[test]
    fn data_expressions() {
        assert_eq!(symbolize_expr("foo+8"), "_foo+8");
        assert_eq!(symbolize_expr(".Lcase - .Ltab"), "Lcase - Ltab");
        assert_eq!(symbolize_expr("0x10"), "0x10");
    }
}
