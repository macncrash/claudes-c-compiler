#!/bin/sh
# Build ccc on a Linux host and check recent codegen changes with the builtin
# (standalone) assembler and linker:
#   - unit tests
#   - chess-perft against published perft counts (native target)
#   - difftest cases and c-testsuite against gcc/clang (native target)
#   - AArch64 struct_copy and chess-perft: natively on arm64 hosts, or under
#     qemu-aarch64 when an aarch64 sysroot is installed on x86 hosts
#
# Usage (from the repo root): sh tests/linux-check.sh
set -u
cd "$(dirname "$0")/.."

status=0
step() { printf '\n=== %s\n' "$*"; }
fail() { echo "FAIL: $*"; status=1; }

arch=$(uname -m)
case "$arch" in
    x86_64) native=ccc-x86 ;;
    aarch64 | arm64) native=ccc-arm ;;
    riscv64) native=ccc-riscv ;;
    *) echo "unsupported host architecture: $arch"; exit 2 ;;
esac
ref_cc=$(command -v clang || command -v gcc)
bin=target/release

step "build ($arch)"
cargo build --release || exit 1
$bin/$native --version | head -3

step "unit tests"
cargo test --release --lib 2>&1 | grep -E 'test result|FAILED|panicked' || fail "unit tests did not run"
cargo test --release --lib >/dev/null 2>&1 || fail "unit tests"

step "chess-perft ($native, native)"
$bin/$native -O2 -o /tmp/perft-ccc examples/chess-perft/perft.c && /tmp/perft-ccc || fail "perft ($native)"

step "difftest: cases and c-testsuite ($native vs $ref_cc)"
[ -d tests/difftest/suites/c-testsuite ] || sh tests/difftest/fetch_suites.sh >/dev/null 2>&1 || true
python3 tests/difftest/difftest.py --ccc "$bin/$native" --ref-cc "$ref_cc" \
    --suite cases --suite c-testsuite -v 2>&1 | tail -15

# AArch64-specific codegen (FP registers, memcpy chunks, peephole passes).
arm_runner=skip
if [ "$native" = ccc-arm ]; then
    arm_runner=""
elif command -v qemu-aarch64 >/dev/null && [ -d /usr/aarch64-linux-gnu ]; then
    arm_runner="qemu-aarch64 -L /usr/aarch64-linux-gnu"
else
    echo
    echo "skipping AArch64 checks: need qemu-user and an aarch64 sysroot"
    echo "  (e.g. apt install qemu-user gcc-aarch64-linux-gnu)"
fi
run_arm() { $arm_runner "$@"; }
if [ "$arm_runner" != skip ]; then
    step "AArch64: struct_copy (builtin assembler and linker)"
    if $bin/ccc-arm -O2 -o /tmp/struct-copy-arm tests/difftest/cases/struct_copy.c; then
        out=$(run_arm /tmp/struct-copy-arm)
        echo "$out" | tail -3
        echo "$out" | grep -q BAD && fail "struct_copy reported BAD"
        [ "$(echo "$out" | grep -c ' ok ')" = 34 ] || fail "struct_copy: expected 34 ok sizes"
    else
        fail "struct_copy did not build"
    fi
    step "AArch64: chess-perft"
    $bin/ccc-arm -O2 -o /tmp/perft-arm examples/chess-perft/perft.c && run_arm /tmp/perft-arm || fail "perft (ccc-arm)"
fi

echo
[ $status = 0 ] && echo "ALL LINUX CHECKS PASSED" || echo "SOME LINUX CHECKS FAILED"
exit $status
