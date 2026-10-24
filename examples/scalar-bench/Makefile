# Scalar integer benchmark: kernels checked by independent routes, plus a
# ccc vs clang timing comparison.
#
#   make check    # build with ccc, exit non-zero if any check fails
#   make bench    # build with ccc and clang, run both, compare check lines

CCC    ?= ../../target/release/ccc
CLANG  ?= clang -arch arm64
CFLAGS ?= -O2

all: scalarbench

# Rebuild when the compiler changes too (when CCC names a file).
scalarbench: scalarbench.c $(wildcard $(firstword $(CCC)))
	$(CCC) $(CFLAGS) -DCOMPILER_NAME='"ccc"' -o $@ scalarbench.c

scalarbench-clang: scalarbench.c
	$(CLANG) $(CFLAGS) -DCOMPILER_NAME='"clang"' -o $@ scalarbench.c

check: scalarbench
	./scalarbench

# Check results and checksums must match exactly between compilers.
bench: scalarbench scalarbench-clang
	./scalarbench --long | tee build-ccc.txt
	@echo
	./scalarbench-clang --long | tee build-clang.txt
	@grep -E '^check' build-ccc.txt > build-ccc.cmp
	@grep -E '^check' build-clang.txt > build-clang.cmp
	@diff build-ccc.cmp build-clang.cmp && echo "ccc and clang results identical"

clean:
	rm -f scalarbench scalarbench-clang build-*.txt build-*.cmp

.PHONY: all check bench clean
