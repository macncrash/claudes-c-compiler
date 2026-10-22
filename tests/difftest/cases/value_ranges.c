/* Casts, shifts and divisions whose operand ranges make them no-ops or
 * constants, next to ones that only look like it: byte values divided and
 * reduced (including negative bytes), bit fields of a move word, 32-bit
 * values at and past the sign bit, and sums of small fields halved.
 * Also code shaped like a chess move: struct fields indexed by a variable,
 * a select of two early values used much later, values live into some
 * blocks and out of others, byte stores of extended values, and
 * sign-extended indexes used for addressing. */
#include <stdint.h>
#include <stdio.h>

typedef struct {
    uint64_t pieces[2][6];
    uint64_t occ[2];
    uint64_t all;
    int8_t board[64];
    int side, ep, castle;
} Pos;

static int castle_mask[64];

/* Inputs the compiler cannot see through. */
static volatile int8_t bytes[] = { 0, 1, 5, 6, 11, -1, -6, -7, -128, 127, 100, -100 };
static volatile uint32_t words[] = { 0, 1, 63, 0x7fffffff, 0x80000000u, 0xffffffffu, 0x12345678u, 0xfedcba98u };

__attribute__((noinline)) static long div_rem_byte(int8_t b) {
    int pc = b;
    int q = pc / 6, r = pc % 6;
    long idx = (long)q * 48 + (long)r * 8; /* sign-extended into addressing */
    return idx * 1000 + q * 10 + r;
}

__attribute__((noinline)) static unsigned fields(uint32_t m) {
    unsigned from = m & 63, to = (m >> 6) & 63, promo = (m >> 12) & 7, flag = (m >> 16) & 3;
    unsigned hi = (m >> 20) & 0xfff, top = m >> 31;
    long wide = (long)(int)((m >> 6) & 63); /* sext(trunc(x)) of a small field */
    return from + to * 64 + promo * 4096 + flag * 32768 + hi * 7 + top * 3 + (unsigned)wide;
}

__attribute__((noinline)) static long casts(uint32_t w) {
    int s = (int)w;          /* negative past the sign bit */
    long a = s;              /* sign-extends */
    long b = (long)w;        /* zero-extends */
    unsigned u = (unsigned)(w >> 1);
    long c = (int)u;         /* below 2^31: either extension */
    return a * 3 + b * 5 + c * 7 + (int)(w & 0xffff) - (long)(int8_t)w;
}

__attribute__((noinline)) static int halves(int from, int to) {
    from &= 63, to &= 63;
    int mid = (from + to) / 2;    /* non-negative: no sign fix-up needed */
    int neg = (from - to) / 2;    /* may be negative */
    int rem = (from - to) % 4;
    return mid * 10000 + neg * 100 + rem;
}

__attribute__((noinline)) static int masks(int x) {
    int a = x & 63;
    int b = a & 7;            /* reassociation must keep the mask exact */
    int c = (x & 0xf0) | 0x0f;
    int d = c | 0x07;         /* already set: c itself */
    return a * 1000 + b * 100 + c + d;
}

static void put(Pos *p, int color, int type, int sq) {
    uint64_t b = 1ULL << sq;
    p->pieces[color][type] |= b;
    p->occ[color] |= b;
    p->all |= b;
    p->board[sq] = (int8_t)(color * 6 + type);
}

static void take(Pos *p, int sq) {
    int pc = p->board[sq];
    uint64_t keep = ~(1ULL << sq);
    p->pieces[pc / 6][pc % 6] &= keep;
    p->occ[pc / 6] &= keep;
    p->all &= keep;
    p->board[sq] = -1;
}

__attribute__((noinline)) static void make_move(Pos *p, uint32_t m) {
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
        int kingside = (to & 7) == 6;
        take(p, (to & 56) + (kingside ? 7 : 0));
        put(p, us, 3, (to & 56) + (kingside ? 5 : 3));
    }
    p->castle &= castle_mask[from] & castle_mask[to];
    p->ep = flag == 1 ? (from + to) / 2 : -1;
    p->side = us ^ 1;
}

static uint64_t pos_hash(const Pos *p) {
    uint64_t h = 1469598103934665603ULL;
    const unsigned char *b = (const unsigned char *)p;
    for (unsigned i = 0; i < sizeof *p; i++)
        h = (h ^ b[i]) * 1099511628211ULL;
    return h;
}

/* Byte and halfword stores of extended values, and an index sign-extended
 * for two different addresses. */
__attribute__((noinline)) static void stores(int8_t *b, int16_t *h, int v, int i) {
    b[i & 7] = (int8_t)v;
    h[i & 7] = (int16_t)(int8_t)v;      /* byte extension, then halfword store */
    h[(i + 1) & 7] = (int16_t)v;
}

__attribute__((noinline)) static long sext_index(const long *a, const int *b, int i, int j) {
    int k = i - j;                       /* may be negative */
    return a[k + 4] * 3 + b[k + 4] + (long)k * 48;
}

/* A row of a 2-D array picked by a signed index (smaddl), and a signed
 * index reaching below the base. */
__attribute__((noinline)) static uint64_t row(const Pos *p, int by, int k) {
    const uint64_t *pc = p->pieces[by];
    return pc[0] + pc[k & 3] * 3 + p->occ[by];
}

__attribute__((noinline)) static long below(const long *mid, int i, int j) {
    long s = 0;
    for (int n = 0; n < j; n++)
        s += mid[i - n] * (n + 1);
    return s;
}

/* Values that are not what their type's usual extension would give: a
 * negative short widened to unsigned (and xored with 0), and an int that a
 * later pass computes with a 32-bit not, both widened to long long. */
__attribute__((noinline)) static int short_to_unsigned(int y) {
    unsigned x = (short)y;
    return (0LL > (0U ^ x)) + 2 * ((0U ^ (short)y) < 0LL);
}

__attribute__((noinline)) static long long not_widened(unsigned x) {
    int y = x;
    y = ~y;
    return ((long long)x & y) ^ ((long long)y << 1);
}

int main(void) {
    for (unsigned i = 0; i < sizeof bytes; i++)
        printf("byte %d: %ld\n", bytes[i], div_rem_byte(bytes[i]));
    for (unsigned i = 0; i < sizeof words / sizeof words[0]; i++)
        printf("word %08x: %u %ld %d\n", (unsigned)words[i], fields(words[i]), casts(words[i]), masks((int)words[i]));
    for (int f = 0; f < 64; f += 9)
        for (int t = 0; t < 64; t += 13)
            printf("%d ", halves(f, t));
    printf("\n");

    for (int sq = 0; sq < 64; sq++)
        castle_mask[sq] = 15;
    castle_mask[0] = 13, castle_mask[4] = 12, castle_mask[7] = 14;
    castle_mask[56] = 7, castle_mask[60] = 3, castle_mask[63] = 11;

    Pos p = { 0 };
    for (int sq = 0; sq < 64; sq++)
        p.board[sq] = -1;
    static const int8_t back[8] = { 3, 1, 2, 4, 5, 2, 1, 3 };
    for (int f = 0; f < 8; f++) {
        put(&p, 0, back[f], f);
        put(&p, 0, 0, 8 + f);
        put(&p, 1, 0, 48 + f);
        put(&p, 1, back[f], 56 + f);
    }
    p.castle = 15;
    p.ep = -1;
#define MV(f, t, pr, fl) ((uint32_t)((f) | ((t) << 6) | ((pr) << 12) | ((fl) << 16)))
    uint32_t moves[] = {
        MV(12, 28, 0, 1), MV(51, 35, 0, 1), MV(28, 35, 0, 0),  /* double pushes, capture */
        MV(6, 21, 0, 0), MV(52, 36, 0, 1), MV(35, 44, 0, 2),   /* en passant */
        MV(5, 12, 0, 0), MV(59, 52, 0, 0), MV(4, 6, 0, 3),     /* castle kingside */
        MV(57, 42, 0, 0), MV(44, 53, 0, 0), MV(58, 51, 0, 0),
        MV(53, 62, 4, 0),                                      /* capture and promote */
        MV(60, 58, 0, 3),                                      /* castle queenside */
    };
    for (unsigned i = 0; i < sizeof moves / sizeof moves[0]; i++) {
        make_move(&p, moves[i]);
        printf("move %u: side %d ep %d castle %d hash %016llx\n", i, p.side, p.ep, p.castle,
               (unsigned long long)pos_hash(&p));
    }

    int8_t b[8] = { 0 };
    int16_t h[8] = { 0 };
    for (int v = -300; v < 300; v += 77)
        stores(b, h, v, v & 7);
    for (int i = 0; i < 8; i++)
        printf("%d/%d ", b[i], h[i]);
    printf("\n");
    long a[9];
    int c[9];
    for (int i = 0; i < 9; i++)
        a[i] = i * 11 - 40, c[i] = i * -7;
    for (int i = 0; i < 5; i++)
        printf("%ld ", sext_index(a, c, i, 2));
    printf("\n");
    for (int by = 0; by < 2; by++)
        printf("row %d: %llu %llu\n", by, (unsigned long long)row(&p, by, 1), (unsigned long long)row(&p, by, 6));
    printf("below %ld %ld\n", below(a + 6, 0, 5), below(a + 6, -1, 4));
    for (unsigned i = 0; i < sizeof words / sizeof words[0]; i++)
        printf("%d %lld\n", short_to_unsigned((int)words[i]), not_widened(words[i]));
    return 0;
}
