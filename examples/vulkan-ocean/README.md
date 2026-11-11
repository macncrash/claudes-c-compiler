# Vulkan ocean

A real-time ocean rendered with Vulkan (MoltenVK on macOS) through an SDL2
window, compiled by ccc. The wave simulation runs in C on the CPU: every frame
it evaluates 24 Gerstner waves at each of 147,456 grid vertices (positions and
analytic normals), split across threads, and writes the result straight into a
mapped vertex buffer. The GPU only shades it, using GLSL shaders compiled to
SPIR-V by `glslc`.

## Requirements (macOS, Apple Silicon)

```sh
brew install sdl2 vulkan-loader vulkan-headers molten-vk shaderc
cargo build --release          # from the repo root, builds ccc
```

## Run

```sh
make                 # builds ./ocean with ccc
./ocean              # drag to orbit, scroll to zoom, space to pause, Esc to quit
./ocean --grid 768 --waves 32 --novsync
```

The title bar shows the CPU simulation time per frame and the compiler that
built the binary.

## Compare against clang

```sh
make bench           # builds ./ocean-clang too, runs both with no window or GPU
```

`--bench N` simulates N frames and prints timing plus a checksum of the vertex
data. The two builds should print the same heights and checksum.

At the time of writing, on one thread of an M3 Max, the ccc build and the
clang -O2 build both take about 4.4 ms per frame (4.3 ms at best). The first
macOS build of ccc took 80 ms. The wave loop is vectorized like clang's: four
waves at a time, each of the six sums still adding its terms in order, so
the checksum is the same.

## Before and after

```sh
make before-after        # three windows, each on one thread, title bars showing the time
make bench-before-after  # the same three with no window or GPU
```

Both build the commit that added macOS support (`FIRST_COMMIT`) from git
history into `build/first`, and compile `ocean.c` with it, with today's ccc and
with clang. The three print the same heights and checksum.
