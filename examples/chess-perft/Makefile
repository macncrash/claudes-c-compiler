# Chess perft benchmark: correctness against published move counts, plus a
# ccc vs clang comparison.
#
#   make check    # build with ccc, exit non-zero if any count is wrong
#   make bench    # build with ccc and clang, run both, compare search output

CCC    ?= ../../target/release/ccc
CLANG  ?= clang -arch arm64
CFLAGS ?= -O2

all: perft

perft: perft.c
	$(CCC) $(CFLAGS) -DCOMPILER_NAME='"ccc"' -o $@ perft.c

perft-clang: perft.c
	$(CLANG) $(CFLAGS) -DCOMPILER_NAME='"clang"' -o $@ perft.c

check: perft
	./perft

# Search node counts and best moves must match exactly between compilers.
bench: perft perft-clang
	./perft | tee build-ccc.txt
	@echo
	./perft-clang | tee build-clang.txt
	@grep -E '^(perft|mate|search)' build-ccc.txt | sed 's/  [0-9.]* s$$//' > build-ccc.cmp
	@grep -E '^(perft|mate|search)' build-clang.txt | sed 's/  [0-9.]* s$$//' > build-clang.cmp
	@diff build-ccc.cmp build-clang.cmp && echo "ccc and clang results identical"

clean:
	rm -f perft perft-clang build-*.txt build-*.cmp

.PHONY: all check bench clean
