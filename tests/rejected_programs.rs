//! Programs ccc must reject with a diagnostic. Each of these once crashed or
//! hung the compiler, which difftest cannot show: it only runs programs that
//! clang accepts.

use std::path::PathBuf;
use std::process::{Command, Stdio};
use std::time::{Duration, Instant};

const TIME_LIMIT: Duration = Duration::from_secs(30);

/// Compile `source`, and check it stops with a diagnostic holding `expected`.
/// Returns the diagnostics.
fn assert_rejected(name: &str, source: &str, expected: &str) -> String {
    let path: PathBuf = std::env::temp_dir().join(format!("ccc-rejected-{}-{name}.c", std::process::id()));
    std::fs::write(&path, source).unwrap();
    // To a file: a diagnostic quotes its line, which can be longer than a pipe holds.
    let log = path.with_extension("log");
    let mut child = Command::new(env!("CARGO_BIN_EXE_ccc"))
        .args(["-O2", "-S", "-o", "/dev/null"])
        .arg(&path)
        .stdout(Stdio::null())
        .stderr(std::fs::File::create(&log).unwrap())
        .spawn()
        .unwrap();
    let start = Instant::now();
    while child.try_wait().unwrap().is_none() {
        if start.elapsed() > TIME_LIMIT {
            child.kill().unwrap();
            let _ = child.wait();
            let _ = std::fs::remove_file(&path);
            let _ = std::fs::remove_file(&log);
            panic!("{name}: still compiling after {TIME_LIMIT:?}");
        }
        std::thread::sleep(Duration::from_millis(5));
    }
    let status = child.wait().unwrap();
    let stderr = String::from_utf8_lossy(&std::fs::read(&log).unwrap()).into_owned();
    let _ = std::fs::remove_file(&path);
    let _ = std::fs::remove_file(&log);
    assert_eq!(status.code(), Some(1), "{name}: {stderr}");
    assert!(!stderr.contains("internal error") && !stderr.contains("panicked"), "{name}: {stderr}");
    assert!(stderr.contains(expected), "{name}: {stderr}");
    stderr
}

const MAIN: &str = "int main(void) { return 0; }\n";

#[test]
fn macro_parameter_that_is_not_a_name() {
    // Once an endless loop: only names moved the parameter scan on.
    assert_rejected("param", &format!("#define f(type* a, b) a\n{MAIN}"), "invalid macro definition");
    assert_rejected("param2", &format!("#define f(1) a\n{MAIN}"), "invalid macro definition");
}

#[test]
fn goto_a_label_that_is_nowhere() {
    // Once a panic in shrink_split, which looked up the block.
    let source = "int f(int a) {\n  if (a) goto top;\n  a++;\n  return a;\n}\n";
    assert_rejected("goto", &format!("{source}{MAIN}"), "use of undeclared label 'top'");
    let source = "void *f(void) { return &&nowhere; }\n";
    assert_rejected("label_addr", &format!("{source}{MAIN}"), "use of undeclared label 'nowhere'");
}

#[test]
fn range_designator_too_big_to_expand() {
    // Once took all the memory there was.
    let source = "int v[10] = { [0 ... 0x7fffffffffff] = 1 };\n";
    assert_rejected("range", &format!("{source}{MAIN}"), "range designator is too large");
    let source = "int v[10] = { [-0x7fffffffffffffff ... 0x7fffffffffffffff] = 1 };\n";
    assert_rejected("range_overflow", &format!("{source}{MAIN}"), "range designator is too large");
}

#[test]
fn integer_operator_on_a_float() {
    // Once a panic in the backend, which has no float `and`.
    for (name, expr) in [("and", "d & 1"), ("or", "1 | d"), ("xor", "d ^ d"), ("mod", "d % 2"), ("shl", "d << 1"), ("shr", "1 >> d")] {
        let source = format!("double d;\nint f(void) {{ return {expr}; }}\n{MAIN}");
        assert_rejected(name, &source, "invalid operands to binary");
    }
    assert_rejected("compound", &format!("double d;\nvoid f(void) {{ d &= 1; }}\n{MAIN}"), "invalid operands to binary");
    assert_rejected("not", &format!("double d;\nint f(void) {{ return ~d; }}\n{MAIN}"), "invalid operand to unary ~");
}

#[test]
fn struct_that_holds_itself() {
    // Accepted, and with an unnamed member, an endless recursion.
    let expected = "field has incomplete type 'struct s'";
    assert_rejected("named", &format!("struct s {{ struct s x; }};\n{MAIN}"), expected);
    assert_rejected("array", &format!("struct s {{ int a; struct s x[2]; }};\n{MAIN}"), expected);
    assert_rejected("typedef", &format!("typedef struct s t;\nstruct s {{ t x; }};\n{MAIN}"), expected);
    assert_rejected("unnamed", &format!("struct s {{ char c; }};\nstruct s {{ struct s; }};\n{MAIN}"), expected);
    assert_rejected("union", &format!("union u {{ union u x; }};\n{MAIN}"), "field has incomplete type 'union u'");
}

#[test]
fn thousands_of_errors_on_one_line() {
    // Each error quoted the whole line: minutes, for one bad macro.
    let line = "int a = ) ; ".repeat(40_000);
    let stderr = assert_rejected("errors", &format!("{line}\n{MAIN}"), "too many errors, stopping");
    // And it is quoted only around the caret: 100 errors, not 100 copies of 480KB.
    assert!(stderr.len() < 100_000, "{} bytes of diagnostics", stderr.len());
}

#[test]
fn nesting_without_end() {
    // Each of these once overflowed the stack.
    let n = 25_000;
    let body = |s: String| format!("int f(int a) {{\n{s}\nreturn a;\n}}\n{MAIN}");
    let nest = |open: &str, close: &str, mid: &str| format!("{}{mid}{}", open.repeat(n), close.repeat(n));
    // The diagnostic ends the parse: the levels that unwind after it each
    // missed their closing token, and once quoted the whole line saying so
    // (300MB of errors). A long line is quoted only around the caret.
    let rejected = |name: &str, source: &str| {
        let stderr = assert_rejected(name, source, "nesting is too deep");
        assert!(stderr.len() < 1000, "{name}: {} bytes of diagnostics", stderr.len());
    };
    rejected("parens", &body(format!("a = {};", nest("(", ")", "a"))));
    rejected("blocks", &body(nest("{", "}", "a++;")));
    rejected("ifs", &body(nest("if (a) {", "}", "a++;")));
    rejected("unary", &body(format!("a = {}a;", "- ".repeat(n))));
    rejected("ternary", &body(format!("a = {}a;", "a ? a : ".repeat(n))));
    rejected("labels", &body(format!("{}a++;", (0..n).map(|i| format!("l{i}: ")).collect::<String>())));
    rejected("init", &format!("int x = {};\n{MAIN}", nest("{", "}", "1")));
    rejected("structs", &format!("struct {} v;\n{MAIN}", nest("{ struct ", " m; }", "{ int a; }")));
}
