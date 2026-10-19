#!/bin/sh
# Download the external C test suites used by difftest.py into suites/.
# Revisions are pinned so baselines stay comparable over time.
set -eu
cd "$(dirname "$0")"
mkdir -p suites

C_TESTSUITE_REV=5c7275656d751de0e68b2d340a95b5681858ed07
GCC_TAG=releases/gcc-14.2.0

if [ ! -d suites/c-testsuite ]; then
    git clone --quiet https://github.com/c-testsuite/c-testsuite suites/c-testsuite
    git -C suites/c-testsuite checkout --quiet "$C_TESTSUITE_REV"
fi

if [ ! -d suites/gcc-torture ]; then
    tmp=suites/.gcc-sparse
    rm -rf "$tmp"
    git clone --quiet --depth 1 --branch "$GCC_TAG" --filter=blob:none --sparse \
        https://github.com/gcc-mirror/gcc "$tmp"
    git -C "$tmp" sparse-checkout set gcc/testsuite/gcc.c-torture/execute
    mv "$tmp/gcc/testsuite/gcc.c-torture/execute" suites/gcc-torture
    rm -rf "$tmp"
fi

echo "suites ready: $(ls suites)"
