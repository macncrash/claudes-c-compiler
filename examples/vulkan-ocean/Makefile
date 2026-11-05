# Builds the ocean demo with ccc (default) and, for comparison, with clang.
#
#   make                 # ./ocean        built by ccc
#   make ocean-clang     # ./ocean-clang  built by Apple clang
#   make bench           # simulate-only timing of both, no GPU needed
#   make before-after    # three windows: first macOS ccc, ccc today, clang
#   make bench-before-after   # the same three, simulate-only, on one thread

PREFIX  ?= /opt/homebrew
GLSLC   ?= glslc
CCC     ?= ../../target/release/ccc
CLANG   ?= clang -arch arm64
CFLAGS  ?= -O2
BENCH_FRAMES ?= 300

# The commit that added macOS support, built from git history for comparison.
FIRST_COMMIT ?= 66c604f9
FIRST_CCC = build/first/target/release/ccc

CPPFLAGS = -I$(PREFIX)/include -Ibuild \
           -DVULKAN_LIBRARY='"$(PREFIX)/lib/libvulkan.1.dylib"'
LDLIBS   = -L$(PREFIX)/lib -Wl,-rpath,$(PREFIX)/lib -lSDL2 -lvulkan -lpthread -lm

SHADERS  = ocean.vert ocean.frag sky.vert sky.frag
SPIRV    = $(SHADERS:%=build/%.inc)

all: ocean

ocean: ocean.c $(SPIRV)
	$(CCC) $(CFLAGS) $(CPPFLAGS) -DCOMPILER_NAME='"ccc"' -o $@ ocean.c $(LDLIBS)

ocean-clang: ocean.c $(SPIRV)
	$(CLANG) $(CFLAGS) $(CPPFLAGS) -DCOMPILER_NAME='"clang"' -o $@ ocean.c $(LDLIBS)

$(FIRST_CCC):
	mkdir -p build/first
	git -C ../.. archive $(FIRST_COMMIT) | tar -x -C build/first
	cargo build --release --manifest-path build/first/Cargo.toml

ocean-first: ocean.c $(SPIRV) | $(FIRST_CCC)
	$(FIRST_CCC) $(CFLAGS) $(CPPFLAGS) -DCOMPILER_NAME='"ccc, first macOS build"' -o $@ ocean.c $(LDLIBS)

build/%.inc: shaders/%
	@mkdir -p build
	$(GLSLC) -O -mfmt=c -o $@ $<

bench: ocean ocean-clang
	./ocean --bench $(BENCH_FRAMES)
	@echo
	./ocean-clang --bench $(BENCH_FRAMES)

before-after: ocean ocean-first ocean-clang
	./ocean-first --threads 1 --novsync & ./ocean --threads 1 --novsync & ./ocean-clang --threads 1 --novsync & wait

bench-before-after: ocean ocean-first ocean-clang
	./ocean-first --threads 1 --bench $(BENCH_FRAMES)
	@echo
	./ocean --threads 1 --bench $(BENCH_FRAMES)
	@echo
	./ocean-clang --threads 1 --bench $(BENCH_FRAMES)

clean:
	rm -rf build ocean ocean-clang ocean-first

.PHONY: all bench clean
