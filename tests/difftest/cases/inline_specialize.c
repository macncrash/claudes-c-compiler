/* Inlining of helpers called with constant arguments (sized after folding the
 * constants in), and register allocation of values that are live only in some
 * of the blocks between their definition and last use. */
#include <stdio.h>
#include <stdint.h>

typedef unsigned long long u64;

static u64 table[8][64];
static int side_effects;

static int lsb(u64 b) { return __builtin_ctzll(b); }
static int msb(u64 b) { return 63 - __builtin_clzll(b); }
static int goes_up(int d) { return d <= 2 || d == 7; }

/* Like a sliding-piece ray: one table row per direction, constant at every
 * call site, so each call folds to a few instructions. */
static u64 ray(int d, int sq, u64 occ)
{
    u64 a = table[d][sq], blockers = a & occ;
    if (blockers)
        a ^= table[d][goes_up(d) ? lsb(blockers) : msb(blockers)];
    return a;
}

static u64 diagonals(int sq, u64 occ)
{
    return ray(1, sq, occ) | ray(3, sq, occ) | ray(5, sq, occ) | ray(7, sq, occ);
}

static u64 lines(int sq, u64 occ)
{
    return ray(0, sq, occ) | ray(2, sq, occ) | ray(4, sq, occ) | ray(6, sq, occ);
}

/* Constant arguments that need a conversion on the way in. */
static int narrow(unsigned char c, short s, int mode)
{
    int r = c * 3 + s;
    if (mode == 0)
        r += 7;
    else if (mode == 1)
        r -= 11;
    else if (mode == 2)
        r ^= 0x55;
    else
        side_effects++;
    return r;
}

static double scale(double x, int k)
{
    if (k == 0)
        return x;
    if (k == 1)
        return x * 2.5;
    if (k == 2)
        return x / 4.0;
    return -x;
}

/* A constant that selects a path with a call and a store. */
static int maybe_log(int level, int v)
{
    if (level > 2) {
        side_effects += v;
        printf("log %d\n", v);
        return v * 2;
    }
    return v + level;
}

static __attribute__((noinline)) u64 opaque(u64 x) { return x * 0x9e3779b97f4a7c15ULL; }

/* `keep` is used only after the branch that skips the call, which the
 * layout places after the call block. */
static __attribute__((noinline)) u64 cold_use(u64 a, u64 b, int which)
{
    u64 keep = a ^ (b << 3);
    u64 r;
    if (which & 1) {
        r = keep + 1;
    } else {
        r = opaque(b);
        r += opaque(r);
    }
    return r ^ (which > 4 ? keep : 0);
}

/* Lowered phi: defined in two predecessors, a call in a sibling block. */
static __attribute__((noinline)) u64 phi_around_call(u64 a, int n)
{
    u64 acc = 0;
    for (int i = 0; i < n; i++) {
        u64 v;
        if ((i % 3) == 0)
            v = a + i;
        else if ((i % 3) == 1)
            v = opaque(a ^ i);
        else
            v = a - i;
        acc = acc * 31 + v;
    }
    return acc;
}

int main(void)
{
    for (int d = 0; d < 8; d++)
        for (int sq = 0; sq < 64; sq++)
            table[d][sq] = opaque((u64)(d * 64 + sq + 1)) | (1ULL << sq);

    u64 sum = 0;
    for (int sq = 0; sq < 64; sq++) {
        u64 occ = opaque((u64)sq) & 0x00ff00ff00ff00ffULL;
        sum = sum * 3 + diagonals(sq, occ);
        sum = sum * 5 + lines(sq, occ);
        sum ^= ray(2, sq, occ) + ray(5, sq, ~occ);
    }
    printf("rays %llx\n", sum);

    printf("narrow %d %d %d %d\n", narrow(300, -40000, 0), narrow(255, 32767, 1),
           narrow(-1, -1, 2), narrow(7, 8, 9));
    printf("scale %.3f %.3f %.3f %.3f\n", scale(3, 0), scale(3, 1), scale(3, 2), scale(3, 7));
    printf("maybe_log %d %d %d\n", maybe_log(0, 5), maybe_log(2, 6), maybe_log(3, 7));
    printf("side_effects %d\n", side_effects);

    u64 c = 0;
    for (int w = 0; w < 8; w++)
        c = c * 7 + cold_use(0x1234 + w, 0x5678ULL << w, w);
    printf("cold_use %llx\n", c);
    printf("phi_around_call %llx %llx\n", phi_around_call(99, 10), phi_around_call(1, 0));
    return 0;
}
