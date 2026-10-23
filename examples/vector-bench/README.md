# Vector benchmark

Six kernels written with GCC vector extensions
(`__attribute__((vector_size(N)))`), each also written as plain scalar loops.
The program runs both versions over the same data and compares them (exactly
for integers, within a relative 1e-5 for floating point), so a miscompiled
vector operation shows up as `FAIL` with no reference compiler involved.
Then it times the vector versions.

| kernel     | what it exercises |
|------------|-------------------|
| `saxpy`    | `float4` multiply-add with a vector and a constant broadcast |
| `xorshift` | `uint4` shifts by constants, xor, multiply |
| `select`   | lane-mask compares (`x > y` gives -1/0 lanes) and bitwise selects |
| `dmix`     | `double2` multiply, divide, scalar broadcasts |
| `blend`    | 8-byte `uint8x8` vectors, shifts and adds |
| `call`     | 8-byte vectors passed to and returned from a non-inlined function |

It is the vector counterpart to `chess-perft`, which is scalar integer code:
ccc has no auto-vectorizer, so only code that uses vector types benefits from
its NEON code generation.

## Run

```sh
make check           # ccc build; exits non-zero if any check fails
make bench           # ccc and clang builds; check lines must be identical
./vecbench --long    # five times as many timing repetitions
```

`make check` passes with ccc on arm64 macOS. Millions of vectors per second
at the time of writing (`./vecbench`, Apple M-series; median of several runs
after the first, which is consistently slower):

| kernel     | ccc before NEON codegen | ccc | clang -O2 |
|------------|------:|------:|------:|
| `saxpy`    |   249 | 3690 | 3660 |
| `xorshift` |    82 | 2110 | 2090 |
| `select`   |    27 | 1455 | 1320 |
| `dmix`     |   333 | 3710 | 3705 |
| `blend`    |    79 | 3705 | 3535 |
| `call`     |   150 | 1240 | 1240 |

"Before NEON codegen" lowered every vector operation lane by lane through
memory. ccc now emits one NEON instruction per operation and keeps
temporaries, local vector variables, vector parameters, initializers,
constant vector literals and loop-invariant constants and operations in NEON
registers. Shifts by constants use the immediate forms; bitwise selects,
shift-and-accumulates and multiply-adds (`mla`/`mls` on integer lanes;
`fmla`/`fmls` on float lanes when contracted) take one instruction.
Vector arguments and return values use SIMD registers as AAPCS64 requires,
and move between those and the registers holding the values without going
through the stack. (clang's `xorshift` also uses the SHA3 extension's
three-way `eor3`, but there it saves no instructions; the gap it was once
blamed for was the separate integer multiply and add.) Calls clobber the
registers holding constants and vector variables. Around one, ccc saves a
variable only if it may have changed since it was last saved, and after
one it rebuilds a constant or reloads a variable only if a vector op uses
it before the next call, or else where the loop exits. So `call`'s loop no
longer saves, reloads or rebuilds those of the other kernels on every
iteration.
