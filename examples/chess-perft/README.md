# Chess perft

A bitboard chess move generator that checks itself. `perft(n)` counts every
legal position reachable in exactly `n` moves; the counts for the standard test
positions are published
([chessprogramming.org](https://www.chessprogramming.org/Perft_Results)), so a
miscompiled shift, bit scan, or branch shows up as a wrong number with no
reference compiler involved. Castling, en passant, promotions, and checks are
all exercised by the six positions.

The program also solves two mate puzzles and runs fixed-depth alpha-beta
searches, whose best moves and node counts must match between compilers.

It is the integer counterpart to the float-heavy `vulkan-ocean` demo: 64-bit
bitboard arithmetic, `__builtin_ctzll`/`clzll`/`popcountll`, deep recursion,
and struct copies.

## Run

```sh
make check           # ccc build; exits non-zero if any count is wrong
make bench           # ccc and clang builds; results must be identical
./perft --deep       # adds perft(6) of the start position and perft(5) of Kiwipete
```

`make check` and `./perft --deep` pass with ccc on arm64 macOS. At the time
of writing, the ccc build does about 43 M nodes/s and clang -O2 about
84 M nodes/s. Earlier milestones: 7.7 M nodes/s before struct copies were
compiled to 16-byte `ldp`/`stp` chunks instead of a byte loop, and 24 M
nodes/s before register-addressed loads/stores, gap-aware register
allocation, and function-wide dead-code cleanup in the peephole optimizer,
and 36 M nodes/s before boolean-phi threading and AArch64 addressing folds.
