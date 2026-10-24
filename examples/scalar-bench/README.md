# Scalar benchmark

Seven integer kernels of the branchy, memory-bound kind most C programs spend
their time in. Each result is checked by an independent route, so a
miscompiled branch, load or shift shows up as `FAIL` with no reference
compiler involved. The check lines also print checksums that must match
between compilers. Then each kernel is timed.

| kernel   | what it does | checked by | item |
|----------|--------------|------------|------|
| `hash`   | open-addressing table of 64-bit keys: insert, hit and miss lookups, backward-shift deletion | counts, and the values of the keys left after deletion | one operation |
| `sort`   | quicksort (median of three, insertion sort below 16) of 100,000 `int32_t` | ascending order, sum and xor unchanged | one element |
| `interp` | `switch`-dispatched register bytecode machine counting primes below 30,000 by trial division | a sieve | one instruction |
| `tree`   | unbalanced binary search tree of 50,000 random keys: insert, hit and miss lookups, in-order walk | every key found, every miss missed, walk ascending | one operation |
| `words`  | tokenize 256 KB of text, lowercase, FNV-hash and count words in a chained table | the text generator's own tallies | one byte |
| `lz`     | LZ77 compression of 64 KB with hash chains (depth 8), then decompression | the round trip | one byte |
| `crc`    | table-driven CRC-32 of 256 KB | the standard check value and a bitwise CRC | one byte |

It complements `chess-perft` (bitboard arithmetic, recursion, struct copies)
and `vector-bench` (GCC vector types): ccc matches clang on both of those, and
this program is meant to find where it does not.

## Run

```sh
make check              # ccc build; exits non-zero if any check fails
make bench              # ccc and clang builds; check lines must be identical
./scalarbench --long    # five times as many timing repetitions
```

`make check` passes with ccc on arm64 macOS. Millions of items per second at
the time of writing (`./scalarbench --long`, Apple M-series; medians of seven
alternating runs):

| kernel   | ccc | clang -O2 | ccc / clang |
|----------|------:|------:|-----:|
| `hash`   |   101 |   121 | 0.84 |
| `sort`   |  21.7 |  23.2 | 0.93 |
| `interp` |  1379 |   862 | 1.60 |
| `tree`   |  15.3 |  17.4 | 0.88 |
| `words`  |   184 |   227 | 0.81 |
| `lz`     |   121 |   141 | 0.86 |
| `crc`    |   405 |   452 | 0.90 |
