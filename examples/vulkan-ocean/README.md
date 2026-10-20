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

At the time of writing, the ccc build takes 2.3 ms per frame with all threads
and 17.7 ms on one thread; clang -O2 takes 0.77 ms and 4.3 ms. Before ccc's
AArch64 backend kept floats in FP registers (d8-d15, v16-v31), those were
9.3 ms and 79.6 ms. The remaining gap is mostly integer address arithmetic
and clang's vectorization.
