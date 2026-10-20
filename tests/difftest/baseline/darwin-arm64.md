# difftest baseline: darwin-arm64

- date: 2026-10-20
- ccc: `7fe458a9` flags `-O2 -w`
- reference: `Apple clang version 16.0.0 (clang-1600.0.26.6)` flags `-arch arm64 -O0 -w`

| suite | pass | compile_error | asm_error | link_error | crash | timeout | wrong_exit | wrong_output | ref_fail | pass % |
|---|---|---|---|---|---|---|---|---|---|---|
| c-testsuite | 220 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 100.0 |
| cases | 16 | 0 | 0 | 0 | 0 | 0 | 0 | 1 | 0 | 94.1 |
| gcc-torture | 1337 | 1 | 0 | 6 | 2 | 0 | 0 | 1 | 138 | 99.3 |

## Failures

### compile_error (1)

- `gcc-torture/970214-2`: tests/difftest/suites/gcc-torture/970214-2.c:7:11: error: expected ')' before character constant

### link_error (6)

- `gcc-torture/20020720-1`: ccc: error: linker failed (clang): Undefined symbols for architecture arm64:
- `gcc-torture/20041114-1`: ccc: error: linker failed (clang): Undefined symbols for architecture arm64:
- `gcc-torture/compare-3`: ccc: error: linker failed (clang): Undefined symbols for architecture arm64:
- `gcc-torture/pure-1`: ccc: error: linker failed (clang): Undefined symbols for architecture arm64:
- `gcc-torture/shiftopt-1`: ccc: error: linker failed (clang): Undefined symbols for architecture arm64:
- `gcc-torture/string-opt-18`: ccc: error: linker failed (clang): Undefined symbols for architecture arm64:

### crash (2)

- `gcc-torture/builtin-constant`: signal 6
- `gcc-torture/builtin-types-compatible-p`: signal 6

### wrong_output (2)

- `cases/abi_args`: line 3: expected 'small_stack 120936', got 'small_stack 936'
- `gcc-torture/return-addr`: line 1: expected '0x16f2970c1 0x16f2970c1 0x16f2970c1 0x16f2970c1', got '0x16d68ed7f 0x16d68ed7f 0x16d68ed7f 0x16d68ed7f'
