/* Large 64-bit constants used in loops, built once at function entry: as
 * multipliers, addends, xor keys, compare operands and stored values; in a
 * loop that makes calls (the register must survive them), in nested loops,
 * with more distinct constants than get registers, next to bitmask masks
 * that stay immediates, and also used outside the loop. Plus loop indexes
 * kept in range by a mask (`i = (i + 1) & mask`), whose widening is free,
 * and ones that may start out of range. */
#include <stdint.h>
#include <stdio.h>

static uint64_t state;

__attribute__((noinline)) static uint64_t next(void) {
    uint64_t z = (state += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

__attribute__((noinline)) static uint64_t mix_loop(uint64_t s, int n) {
    uint64_t acc = 0x0123456789abcdefull;
    for (int i = 0; i < n; i++) {
        s += 0x9e3779b97f4a7c15ull;
        uint64_t z = (s ^ (s >> 30)) * 0xbf58476d1ce4e5b9ull;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
        acc ^= z ^ 0xfedcba9876543210ull;
        acc = (acc & 0x5555555555555555ull) + ((acc >> 1) & 0x5555555555555555ull) + i;
        if (z == 0x123456789abcdef0ull)
            acc = 0;
        if (z > 0xc000000000000001ull)
            acc += 3;
    }
    return acc * 0x9e3779b97f4a7c15ull;
}

/* Constants live across calls. */
__attribute__((noinline)) static uint64_t with_calls(int n) {
    uint64_t h = 0;
    for (int i = 0; i < n; i++)
        h = (h ^ next()) * 0x100000001b3ull + 0xcbf29ce484222325ull;
    return h;
}

/* More constants than registers handed out; nested loops; stores. */
__attribute__((noinline)) static void many(uint64_t *out, int n) {
    for (int j = 0; j < n; j++) {
        uint64_t x = (uint64_t)j;
        for (int i = 0; i < 4; i++) {
            x = x * 0x2127599bf4325c37ull + 0x880355f21e6d1965ull;
            x ^= x >> 23;
            x = x * 0x9e6c63d0676a9a99ull - 0x4cf5ad432745937full;
            x += 0x1b873593cc9e2d51ull;
            x ^= 0x85ebca6bc2b2ae35ull;
        }
        out[j] = x;
        out[j + n] = 0x7fb5d329728ea185ull;
    }
}

/* A table walked with a masked index, starting at a hashed slot. */
__attribute__((noinline)) static int probe(const uint64_t *table, uint64_t key) {
    uint32_t i = (uint32_t)((key * 0x9e3779b97f4a7c15ull) >> 56);
    int steps = 0;
    while (table[i] && table[i] != key) {
        i = (i + 1) & 255;
        steps++;
    }
    return table[i] ? steps : -1 - steps;
}

/* The index may start out of range (a negative int); masking happens later. */
__attribute__((noinline)) static long walk(const long *a, int start, int n) {
    long s = 0;
    int i = start;
    for (int k = 0; k < n; k++) {
        s += (long)i * 3;
        i = (i + 5) & 63;
        s += a[i];
    }
    return s;
}

int main(void) {
    printf("mix %016llx %016llx\n", (unsigned long long)mix_loop(1, 1000), (unsigned long long)mix_loop(0xdeadbeefull, 7));
    state = 42;
    printf("calls %016llx\n", (unsigned long long)with_calls(500));
    uint64_t out[20];
    many(out, 10);
    uint64_t x = 0;
    for (int i = 0; i < 20; i++)
        x = x * 31 + out[i];
    printf("many %016llx\n", (unsigned long long)x);

    static uint64_t table[256];
    state = 7;
    for (int k = 0; k < 180; k++) {
        uint64_t key = next() | 1;
        uint32_t i = (uint32_t)((key * 0x9e3779b97f4a7c15ull) >> 56);
        while (table[i])
            i = (i + 1) & 255;
        table[i] = key;
    }
    state = 7;
    long found = 0, steps = 0;
    for (int k = 0; k < 200; k++) {
        int r = probe(table, next() | 1);
        found += r >= 0;
        steps += r >= 0 ? r : -1 - r;
    }
    printf("probe %ld found, %ld steps\n", found, steps);

    long a[64];
    for (int i = 0; i < 64; i++)
        a[i] = i * i - 100;
    printf("walk %ld %ld\n", walk(a, -7, 100), walk(a, 3, 64));
    return 0;
}
