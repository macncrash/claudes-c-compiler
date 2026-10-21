# Vector-extension benchmark: vector kernels checked against scalar versions
# of themselves, plus a ccc vs clang timing comparison.
#
#   make check    # build with ccc, exit non-zero if any check fails
#   make bench    # build with ccc and clang, run both, compare check lines

CCC    ?= ../../target/release/ccc
CLANG  ?= clang -arch arm64
CFLAGS ?= -O2

all: vecbench

# Rebuild when the compiler changes too (when CCC names a file).
vecbench: vecbench.c $(wildcard $(firstword $(CCC)))
	$(CCC) $(CFLAGS) -DCOMPILER_NAME='"ccc"' -o $@ vecbench.c -lm

vecbench-clang: vecbench.c
	$(CLANG) $(CFLAGS) -DCOMPILER_NAME='"clang"' -o $@ vecbench.c -lm

check: vecbench
	./vecbench

# Check results must match exactly between compilers.
bench: vecbench vecbench-clang
	./vecbench --long | tee build-ccc.txt
	@echo
	./vecbench-clang --long | tee build-clang.txt
	@grep -E '^check' build-ccc.txt > build-ccc.cmp
	@grep -E '^check' build-clang.txt > build-clang.cmp
	@diff build-ccc.cmp build-clang.cmp && echo "ccc and clang results identical"

clean:
	rm -f vecbench vecbench-clang build-*.txt build-*.cmp

.PHONY: all check bench clean
