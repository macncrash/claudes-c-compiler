/* Values joined by phi copies that may or may not share a register: a
 * conditional update that keeps the old value on one edge (sliding attacks
 * along rays), loops that swap or rotate their variables (the copies must
 * not all collapse into one register), a phi whose incoming value stays
 * live past the join, and branches into blocks that only jump onward. */
#include <stdint.h>
#include <stdio.h>

typedef uint64_t u64;

static u64 rays[8][64];

static int ray_up(int d) { return d <= 2 || d == 7; }

__attribute__((noinline)) static u64 ray_att(int d, int sq, u64 occ) {
    u64 a = rays[d][sq], blockers = a & occ;
    if (blockers)
        a ^= rays[d][ray_up(d) ? __builtin_ctzll(blockers) : 63 - __builtin_clzll(blockers)];
    return a;
}

/* All eight rays inline in one function, as in a chess attack test, with
 * early returns. */
__attribute__((noinline)) static int attacked(int sq, u64 occ, u64 diag, u64 orth, u64 near) {
    if (near & (1ULL << sq))
        return 1;
    u64 b = 0, r = 0;
    for (int d = 1; d < 8; d += 2) {
        u64 a = rays[d][sq], bl = a & occ;
        if (bl)
            a ^= rays[d][ray_up(d) ? __builtin_ctzll(bl) : 63 - __builtin_clzll(bl)];
        b |= a;
    }
    if (b & diag)
        return 2;
    for (int d = 0; d < 8; d += 2) {
        u64 a = rays[d][sq], bl = a & occ;
        if (bl)
            a ^= rays[d][ray_up(d) ? __builtin_ctzll(bl) : 63 - __builtin_clzll(bl)];
        r |= a;
    }
    return (r & orth) != 0 ? 3 : 0;
}

__attribute__((noinline)) static long swap_loop(long a, long b, int n) {
    for (int i = 0; i < n; i++) {
        long t = a;
        a = b;
        b = t + i;
    }
    return a * 1000 + b;
}

__attribute__((noinline)) static long rotate_loop(long a, long b, long c, int n) {
    for (int i = 0; i < n; i++) {
        long t = a;
        a = b;
        b = c;
        c = t ^ i;
    }
    return (a * 31 + b) * 31 + c;
}

/* The old value stays live after the join, next to the new one. */
__attribute__((noinline)) static long keep_old(long x, int k) {
    long y = x;
    if (k & 1)
        y = x * 3 + k;
    return y * 7 + x;
}

/* Fibonacci-style: each value feeds both of the next iteration's. */
__attribute__((noinline)) static unsigned long fib_mix(int n) {
    unsigned long a = 1, b = 2;
    while (n-- > 0) {
        unsigned long c = a + b;
        if (c & 4)
            c ^= a;
        a = b;
        b = c;
    }
    return a ^ (b << 1);
}

/* Several branches to blocks that only jump to the same place. */
__attribute__((noinline)) static int chain(int x) {
    int r = 0;
    if (x & 1) goto a;
    if (x & 2) goto b;
    if (x & 4) goto c;
    r = 1;
    goto out;
a:  goto b;
b:  goto c;
c:  r += x;
out:
    return r;
}

int main(void) {
    for (int sq = 0; sq < 64; sq++) {
        int f = sq & 7, rk = sq >> 3;
        static const int df[8] = { 0, 1, 1, 1, 0, -1, -1, -1 }, dr[8] = { 1, 1, 0, -1, -1, -1, 0, 1 };
        for (int d = 0; d < 8; d++) {
            u64 m = 0;
            for (int ff = f + df[d], rr = rk + dr[d]; ff >= 0 && ff < 8 && rr >= 0 && rr < 8; ff += df[d], rr += dr[d])
                m |= 1ULL << (rr * 8 + ff);
            rays[d][sq] = m;
        }
    }
    u64 occ = 0x91a2b3c4d5e6f708ULL, total = 0;
    int hist[4] = { 0 };
    for (int sq = 0; sq < 64; sq++) {
        for (int d = 0; d < 8; d++)
            total = total * 31 + ray_att(d, sq, occ);
        hist[attacked(sq, occ, 0x0000ff0000ff0000ULL, 0x8100000000000081ULL, 0x24ULL)]++;
        hist[attacked(sq, ~occ, 0x00ff00ff00ff00ffULL, 0x00000000ffff0000ULL, 0)]++;
    }
    printf("rays %llu attacked %d %d %d %d\n", (unsigned long long)total, hist[0], hist[1], hist[2], hist[3]);
    for (int n = 0; n < 6; n++)
        printf("%ld %ld %ld %lu %d\n", swap_loop(3, 5, n), rotate_loop(1, 2, 3, n), keep_old(10 + n, n), fib_mix(n * 7), chain(n));
    return 0;
}
