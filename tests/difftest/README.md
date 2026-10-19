# difftest — differential testing against a reference compiler

`difftest.py` compiles each test program with ccc and with a reference
compiler (clang by default), runs both binaries, and compares exit status
and stdout. The reference defines "correct", so the harness can check
programs that weren't written as self-checking tests.

## Quick start

```sh
cargo build --release
tests/difftest/fetch_suites.sh          # once: c-testsuite + gcc.c-torture/execute
tests/difftest/difftest.py              # run everything, print a summary
tests/difftest/difftest.py --compare    # diff against the saved baseline; exits 1 on regressions
```

Useful options:

| option | meaning |
|---|---|
| `--suite NAME` | `cases`, `c-testsuite`, or `gcc-torture` (repeatable) |
| `-k SUBSTR` | only tests whose id contains `SUBSTR` |
| `-v` | list every failure with a one-line reason |
| `--save-baseline` | write `baseline/<platform>.json` and `.md` |
| `--ccc-flags`, `--ref-flags` | compiler flags (defaults `-O2 -w` / `-O0 -w`) |
| `--json PATH` | write full results for ad-hoc analysis |

## Suites

- **cases**: hand-written tests in `cases/`. These target the platform ABI and
  runtime: argument passing, variadics, TLS, atomics, constructors, libc
  interop. If `NAME.helper.c` exists, the reference compiler builds it and the
  result is linked into *both* binaries, so the test checks calls across the
  ccc/clang boundary.
- **c-testsuite**: <https://github.com/c-testsuite/c-testsuite> single-exec tests.
- **gcc-torture**: GCC's `gcc.c-torture/execute` (GCC 14.2.0). Tests whose
  `dg-` directives need options we don't model are skipped.

The external suites are downloaded into `suites/` (gitignored) at pinned
revisions, so baselines stay comparable. Reference results are cached in
`suites/.ref-cache.json`, keyed by source, compiler version, and flags.

## Statuses

`pass`; failures `compile_error`, `asm_error` (assembler rejected ccc's
output), `link_error`, `crash` (signal), `timeout`, `wrong_exit`,
`wrong_output`; and `ref_fail`: the reference compiler couldn't build or run
the test (GCC-only extensions, glibc-only functions), so ccc isn't judged
on it. The pass rate excludes `ref_fail`.

## Baselines

`baseline/<platform>.md` is the human-readable record and
`baseline/<platform>.json` is what `--compare` reads. Re-record after
intentional changes with `--save-baseline`, and commit both files.

## macOS notes

- The harness pins the reference compiler to the hardware architecture
  (`-arch arm64`). An x86_64 Python running under Rosetta would otherwise
  make clang target x86_64.
- macOS scans each newly linked executable on first launch, which adds 1–3 s
  per binary and dominates the run time (about 12 min for the full set).
  Adding your terminal app under *System Settings → Privacy & Security →
  Developer Tools* usually removes this delay.
