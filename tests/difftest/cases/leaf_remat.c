/* Leaf functions where a rarely taken block needs more registers than the
 * caller-saved ones, while addresses computed before it (p + side * 48, a
 * product, a sign-extended index) stay live into it: recomputing them there
 * must give the same values, including for negative indices, and through
 * phis and loops around the block. */
#include <stdio.h>

typedef unsigned long long u64;

typedef struct {
    u64 pieces[2][6];
    u64 occ[2];
    u64 all;
    signed char board[64];
    int side, ep, castle;
} Pos;

static void take(Pos *p, int sq) {
    int pc = p->board[sq];
    u64 m = ~(1ULL << sq);
    p->pieces[pc / 6][pc % 6] &= m;
    p->occ[pc / 6] &= m;
    p->all &= m;
    p->board[sq] = -1;
}

static void put(Pos *p, int c, int type, int sq) {
    u64 b = 1ULL << sq;
    p->pieces[c][type] |= b;
    p->occ[c] |= b;
    p->all |= b;
    p->board[sq] = (signed char)(c * 6 + type);
}

__attribute__((noinline)) static void move(Pos *p, unsigned m) {
    int from = m & 63, to = (m >> 6) & 63, promo = (m >> 12) & 7, flag = (m >> 16) & 3;
    int us = p->side;
    int type = p->board[from] % 6;
    if (flag == 2)
        take(p, to + (us == 0 ? -8 : 8));
    else if (p->board[to] != -1)
        take(p, to);
    take(p, from);
    put(p, us, promo ? promo : type, to);
    if (flag == 3) {
        int ks = (to & 7) == 6;
        int rf = (to & 56) + (ks ? 7 : 0), rt = (to & 56) + (ks ? 5 : 3);
        take(p, rf);
        put(p, us, 3, rt);
    }
    p->castle &= ~(1 << (from & 3)) & ~(1 << (to & 3));
    p->ep = flag == 1 ? (from + to) / 2 : -1;
    p->side = us ^ 1;
}

/* Many values live into a cold block, one of them a negative index. */
__attribute__((noinline)) static long cold(long *a, int k, int n, int sel) {
    long *row = a + (long)k * 6;
    long *col = a + (long)(k - 5) * 2 + 40;
    long s = row[0] + col[0];
    if (sel & 1) {
        long t0 = a[n & 7] * 3, t1 = a[(n + 1) & 7] * 5, t2 = a[(n + 2) & 7] * 7;
        long t3 = a[(n + 3) & 7] * 11, t4 = a[(n + 4) & 7] * 13, t5 = a[(n + 5) & 7] * 17;
        long t6 = a[(n + 6) & 7] * 19, t7 = a[(n + 7) & 7] * 23;
        s += (t0 ^ t1) + (t2 ^ t3) + (t4 ^ t5) + (t6 ^ t7) + t0 * t7 + t1 * t6 + t2 * t5 + t3 * t4;
        s += row[1] + col[1] + (long)k * 6;
    }
    return s + row[2];
}

/* The same inside a loop: the cold block runs in the loop too. */
__attribute__((noinline)) static long looped(long *a, int k, int n) {
    long s = 0;
    long *row = a + (long)k * 6;
    for (int i = 0; i < n; i++) {
        s += row[i & 3];
        if ((i & 7) == 3) {
            long t0 = a[i & 7] * 3, t1 = a[(i + 1) & 7] * 5, t2 = a[(i + 2) & 7] * 7;
            long t3 = a[(i + 3) & 7] * 11, t4 = a[(i + 4) & 7] * 13, t5 = a[(i + 5) & 7] * 17;
            long t6 = a[(i + 6) & 7] * 19, t7 = a[(i + 7) & 7] * 23;
            s ^= (t0 ^ t1) + (t2 ^ t3) + (t4 ^ t5) + (t6 ^ t7) + t0 * t7 + t1 * t6 + t2 * t5 + t3 * t4;
            s += row[1];
        }
    }
    return s;
}

static u64 hash(const Pos *p) {
    u64 h = (u64)p->side * 31 + (u64)(p->ep + 1) * 7 + (u64)p->castle;
    for (int c = 0; c < 2; c++)
        for (int t = 0; t < 6; t++)
            h = h * 1000003 ^ p->pieces[c][t];
    h = h * 31 + p->occ[0] + p->occ[1] * 3 + p->all * 5;
    for (int i = 0; i < 64; i++)
        h = h * 131 + (u64)(unsigned char)p->board[i];
    return h;
}

int main(void) {
    static const unsigned moves[] = {
        12 | 28 << 6 | 1 << 16, 52 | 36 << 6 | 1 << 16, 6 | 21 << 6, 57 | 42 << 6,
        4 | 6 << 6 | 3 << 16, 60 | 58 << 6 | 3 << 16, 28 | 37 << 6, 51 | 35 << 6 | 1 << 16,
        37 | 43 << 6 | 2 << 16, 9 | 1 << 6 | 4 << 12, 48 | 8 << 6 | 1 << 12,
    };
    Pos p = { 0 };
    for (int i = 0; i < 64; i++)
        p.board[i] = -1;
    static const int back[8] = { 3, 1, 2, 4, 5, 2, 1, 3 };
    for (int f = 0; f < 8; f++) {
        put(&p, 0, back[f], f);
        put(&p, 0, 0, 8 + f);
        put(&p, 1, 0, 48 + f);
        put(&p, 1, back[f], 56 + f);
    }
    p.ep = -1;
    p.castle = 15;
    for (unsigned i = 0; i < sizeof moves / sizeof moves[0]; i++) {
        p.side = (int)(i & 1);
        move(&p, moves[i]);
        printf("%u %llu\n", i, hash(&p));
    }
    long a[64];
    for (int i = 0; i < 64; i++)
        a[i] = (long)i * i - 300;
    for (int k = 0; k < 9; k++)
        printf("%ld %ld %ld\n", cold(a, k, k * 3, k), cold(a, k, k + 1, 1), looped(a, k, k * 5 + 3));
    return 0;
}
