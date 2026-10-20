# Builds the ocean demo with ccc (default) and, for comparison, with clang.
#
#   make                 # ./ocean        built by ccc
#   make ocean-clang     # ./ocean-clang  built by Apple clang
#   make bench           # simulate-only timing of both, no GPU needed

PREFIX  ?= /opt/homebrew
GLSLC   ?= glslc
CCC     ?= ../../target/release/ccc
CLANG   ?= clang -arch arm64
CFLAGS  ?= -O2
BENCH_FRAMES ?= 300

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

build/%.inc: shaders/%
	@mkdir -p build
	$(GLSLC) -O -mfmt=c -o $@ $<

bench: ocean ocean-clang
	./ocean --bench $(BENCH_FRAMES)
	@echo
	./ocean-clang --bench $(BENCH_FRAMES)

clean:
	rm -rf build ocean ocean-clang

.PHONY: all bench clean
