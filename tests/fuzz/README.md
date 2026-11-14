# Differential fuzzers

Each script here generates random C programs in one area of the language,
builds them with ccc and with a reference compiler (clang), runs both, and
reports any program whose output differs. They found the silent
miscompiles fixed on this branch; the hand-reduced regression cases for
those live in `tests/difftest/cases/`. Running the fuzzers themselves at a
fixed seed checks everything they cover, not just the reduced cases.

## Running

```sh
python3 tests/fuzz/run.py --ccc target/release/ccc            # 3 programs per fuzzer
python3 tests/fuzz/run.py --ccc target/release/ccc --count 20 --seed 7 -j 8
python3 tests/fuzz/run.py --ccc target/release/ccc --only sfz,vafz
```

`run.py` exits 1 if any fuzzer found a difference, and keeps the work
directory (generated sources, a `.diff` or `diff.txt` per failing program,
and each fuzzer's `log.txt`). Each fuzzer can also be run alone:

```sh
python3 tests/fuzz/sfz.py CCC CLANG WORKDIR SEED COUNT
```

Requirements: python3 and clang. On Apple silicon `run.py` passes a clang
wrapper that builds for arm64 (a Python running under Rosetta would
otherwise get x86_64 objects). `gcc-15` (`GCC=` to override, Homebrew path
by default) is optional: some fuzzers use it as a second opinion to drop
lines where clang itself is the odd one out.

Programs with undefined behaviour are kept out by construction or by
skipping files that clang's `-fsanitize=undefined` flags; implementation-
defined output (e.g. spacing in stringified macro expansions) is compared
the way each script's docstring describes.

The ABI fuzzers (`abifuzz`, `sfz`, `vafz`, `vafz2`) write the caller and
the callee to separate files and link every pairing of ccc and clang
objects; a ccc-only build cannot show a calling-convention mismatch.

## The fuzzers

| script | area |
|---|---|
| `rtfuzz` | integer arithmetic on runtime values (narrowing, div/rem, shifts) |
| `cfuzz` | constant folding against run-time evaluation (ccc only) |
| `ffuzz` | constant folding of floating-point expressions against run time |
| `fpfuzz` | floating point on runtime values, NaN/inf/-0/subnormals |
| `cvfz` | integer <-> floating conversions, all widths incl. `__int128` |
| `i128fz` | `__int128` arithmetic |
| `cxfz` | `_Complex` arithmetic |
| `cffuzz` | control flow: nested if/switch, GNU case ranges, bounded loops |
| `loopfuzz` | loop nests over arrays and maybe-overlapping pointers |
| `rdfuzz` | float-sum loops (vectorized reductions and near misses) |
| `memfz` | memory access and aliasing: union punning of every width, char and memcpy access, overlapping copies, aliasing pointers |
| `bffuzz` | bit-field layout, static initializers and access |
| `bfrt` | bit-field runtime operations |
| `pkfz` | bit-field layout under `#pragma pack` and attributes |
| `ifuzz` | aggregate initializers, static and automatic |
| `pfuzz` | address constants in static initializers |
| `adfz` | static address constants into arrays, members, strings, functions |
| `clfz` | compound literals and `_Generic` |
| `stfz` | struct returns and copies |
| `sfz` | struct passing and returns across compilers (ABI) |
| `abifuzz` | calling convention: random signatures across compilers |
| `vafz` | variadic functions: every argument kind, across compilers |
| `vafz2` | shapes of variadic code: `va_list` passing, `va_copy`, returns, `snprintf` |
| `vmfz` | variadic macros: `__VA_ARGS__`, `#`, `##`, `__VA_OPT__`, GNU `args...` |
| `gnufz` | GNU extensions: builtins, overflow builtins, statement expressions, vectors |
| `gnfz` | `_Generic`, `sizeof` and `typeof` of random expressions |
| `cqfz` | `_Generic`/`typeof` with const through calls and compound literals |
| `vlafz` | alloca and VLAs: scopes, loops, goto, over-aligned types |
| `vlfz` | `sizeof`/`typeof` of VLA expressions, casts to pointer-to-VLA |
| `vlfz2` | VLA typedefs, `typeof` of VLA type names, VLA parameters |
| `strfz` | string and character literals |
| `wsfz` | wide and UTF string literals in every kind of initializer |
| `atfz` | atomics, single-threaded |
| `tlsfz` | thread-local storage, also from several threads |
| `sjfz` | setjmp/longjmp with locals live across the jump |
| `asmfz` | arm64 inline asm operands and constraints |
