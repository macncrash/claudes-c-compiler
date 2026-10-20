# difftest baseline: darwin-arm64

- date: 2026-10-20
- ccc: `f86c5934` flags `-O2 -w`
- reference: `Apple clang version 16.0.0 (clang-1600.0.26.6)` flags `-arch arm64 -O0 -w`

| suite | pass | compile_error | asm_error | link_error | crash | timeout | wrong_exit | wrong_output | ref_fail | pass % |
|---|---|---|---|---|---|---|---|---|---|---|
| c-testsuite | 220 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 100.0 |
| cases | 21 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 100.0 |
| gcc-torture | 1343 | 0 | 0 | 1 | 2 | 0 | 0 | 1 | 138 | 99.7 |

## Failures

### link_error (1)

- `gcc-torture/20041114-1`: ccc: error: linker failed (clang): Undefined symbols for architecture arm64:

### crash (2)

- `gcc-torture/builtin-constant`: signal 6
- `gcc-torture/builtin-types-compatible-p`: signal 6

### wrong_output (1)

- `gcc-torture/return-addr`: line 1: expected '0x16f2970c1 0x16f2970c1 0x16f2970c1 0x16f2970c1', got '0x16eedad7f 0x16eedad7f 0x16eedad7f 0x16eedad7f'
