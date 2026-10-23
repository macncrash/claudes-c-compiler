/* Addresses built from a quotient and remainder by the row length, which
 * cancel to a plain index: a[x / N][x % N] is the x-th element (C division
 * truncates, so x / N * N + x % N == x for every x, negative ones too).
 * Rows of other lengths, 3-D arrays, struct fields after the array, mixed
 * sizes, and chains that must stay as they are (a variable divisor, a
 * different divisor, an extra index). */
#include <stdio.h>

typedef unsigned long long u64;

static u64 flat[2 * 6 + 64];

typedef struct {
    u64 pieces[2][6];
    u64 occ[2];
    signed char board[64];
} Pos;

/* Negative codes too: the address stays inside flat. */
__attribute__((noinline)) static u64 grid_get(signed char pc) {
    const u64 *mid = flat + 32;
    return *(mid + (pc / 6) * 6 + pc % 6);
}

__attribute__((noinline)) static void take(Pos *p, int sq) {
    int pc = p->board[sq];
    u64 keep = ~(1ULL << sq);
    p->pieces[pc / 6][pc % 6] &= keep;
    p->occ[pc / 6] &= keep;
    p->board[sq] = -1;
}

__attribute__((noinline)) static int cube(const int (*c)[5][7], int x) {
    return c[x / 35][x % 35 / 7][x % 7] + c[x / 35][x / 7 % 5][x % 7];
}

__attribute__((noinline)) static short rows3(const short (*r)[3], unsigned x) {
    return r[x / 3][x % 3];
}

__attribute__((noinline)) static long var_div(const long *a, int x, int n) {
    return a[(x / n) * n + x % n];
}

__attribute__((noinline)) static long other_div(const long (*a)[6], int x) {
    return a[x / 6][x % 4] + a[x / 5][x % 6];
}

__attribute__((noinline)) static long extra(const long (*a)[6], int x, int k) {
    return a[x / 6][x % 6 / 2 + k] + a[x / 6 + k][x % 6];
}

int main(void) {
    for (int i = 0; i < (int)(sizeof flat / sizeof flat[0]); i++)
        flat[i] = 0x9e3779b97f4a7c15ULL * (u64)(i + 1);
    u64 h = 0;
    for (int pc = -12; pc < 30; pc++)
        h = h * 31 + grid_get((signed char)pc);
    printf("grid %llu\n", h);

    Pos p;
    for (int i = 0; i < 12; i++)
        p.pieces[i / 6][i % 6] = ~0ULL >> i;
    p.occ[0] = p.occ[1] = ~0ULL;
    for (int i = 0; i < 64; i++)
        p.board[i] = (signed char)(i % 12);
    for (int sq = 0; sq < 64; sq += 5)
        take(&p, sq);
    h = 0;
    for (int i = 0; i < 12; i++)
        h = h * 31 + p.pieces[i / 6][i % 6];
    printf("take %llu %llu %llu\n", h, p.occ[0], p.occ[1]);

    static int c[4][5][7];
    for (int i = 0; i < 4 * 35; i++)
        (&c[0][0][0])[i] = i * i - 77 * i;
    u64 s = 0;
    for (int x = 0; x < 4 * 35; x++)
        s = s * 3 + (u64)cube((const int (*)[5][7])c, x);
    printf("cube %llu\n", s);

    static short r[40][3];
    for (int i = 0; i < 120; i++)
        (&r[0][0])[i] = (short)(i * 977);
    s = 0;
    for (unsigned x = 0; x < 120; x++)
        s = s * 5 + (u64)rows3((const short (*)[3])r, x);
    printf("rows3 %llu\n", s);

    static long a[20][6];
    for (int i = 0; i < 120; i++)
        (&a[0][0])[i] = (long)i * 1000003 - 5;
    s = 0;
    for (int x = 0; x < 90; x++)
        s = s * 7 + (u64)var_div(&a[0][0], x, x % 9 + 1) + (u64)other_div((const long (*)[6])a, x)
            + (u64)extra((const long (*)[6])a, x, x % 3);
    printf("mixed %llu\n", s);
    return 0;
}
