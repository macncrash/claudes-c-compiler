# difftest baseline: darwin-arm64

- date: 2026-10-27
- ccc: `c5586aa7` flags `-O2 -w`
- reference: `Apple clang version 16.0.0 (clang-1600.0.26.6)` flags `-arch arm64 -O0 -w`

| suite | pass | compile_error | asm_error | link_error | crash | timeout | wrong_exit | wrong_output | ref_fail | pass % |
|---|---|---|---|---|---|---|---|---|---|---|
| c-testsuite | 220 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 100.0 |
| cases | 91 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 100.0 |
| gcc-torture | 1351 | 0 | 0 | 0 | 0 | 0 | 0 | 1 | 138 | 99.9 |

## Failures

### wrong_output (1)

- `gcc-torture/return-addr`: line 1: expected '0x16f2970c1 0x16f2970c1 0x16f2970c1 0x16f2970c1', got '0x16d286ddf 0x16d286ddf 0x16d286ddf 0x16d286ddf'
