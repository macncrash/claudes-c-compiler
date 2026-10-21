# difftest baseline: darwin-arm64

- date: 2026-10-20
- ccc: `8b89269a` flags `-O2 -w`
- reference: `Apple clang version 16.0.0 (clang-1600.0.26.6)` flags `-arch arm64 -O0 -w`

| suite | pass | compile_error | asm_error | link_error | crash | timeout | wrong_exit | wrong_output | ref_fail | pass % |
|---|---|---|---|---|---|---|---|---|---|---|
| c-testsuite | 220 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 100.0 |
| cases | 23 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 100.0 |
| gcc-torture | 1345 | 0 | 0 | 1 | 0 | 0 | 0 | 1 | 138 | 99.9 |

## Failures

### link_error (1)

- `gcc-torture/20041114-1`: ccc: error: linker failed (clang): clang: error: couldn't create cache file '/var/folders/4x/8v702dbd30l6xjkp3lgrx23h0000gp/T/xcrun_db-H7K3cKRg' (errno=Operation not permitted)

### wrong_output (1)

- `gcc-torture/return-addr`: line 1: expected '0x16f2970c1 0x16f2970c1 0x16f2970c1 0x16f2970c1', got '0x16b876daf 0x16b876daf 0x16b876daf 0x16b876daf'
