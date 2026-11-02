/* Helpers of 40-60 instructions once cleaned up, with no calls of their
 * own, called inside loops (inlined there): a record encoder with three
 * sizes, also called once outside any loop (where it stays a call). Plus a
 * pawn-move adder that expands promotions, just over the limit (a call),
 * and a small helper that calls out. */
#include <stdio.h>
#include <string.h>

typedef struct { unsigned m[256]; int n; } MoveList;

#define MOVE(f, t, p, fl) ((unsigned)(f) | ((unsigned)(t) << 6) | ((unsigned)(p) << 12) | ((unsigned)(fl) << 15))

static void add_pawn_move(MoveList *l, int from, int to, int promo_rank, int flags)
{
    if ((to >> 3) == promo_rank) {
        for (int t = 4; t >= 1; t--)
            l->m[l->n++] = MOVE(from, to, t, flags);
    } else {
        l->m[l->n++] = MOVE(from, to, 0, flags);
    }
}

static unsigned encode(unsigned char *out, long v, int kind)
{
    if ((unsigned long)(v + 128) < 256) {
        out[0] = (unsigned char)(0x80 | kind);
        out[1] = (unsigned char)v;
        return 2;
    }
    if ((unsigned long)(v + 32768) < 65536) {
        out[0] = (unsigned char)(0x90 | kind);
        out[1] = (unsigned char)(v >> 8);
        out[2] = (unsigned char)v;
        return 3;
    }
    out[0] = (unsigned char)(0xa0 | kind);
    out[1] = (unsigned char)(v >> 24);
    out[2] = (unsigned char)(v >> 16);
    out[3] = (unsigned char)(v >> 8);
    out[4] = (unsigned char)v;
    return 5;
}

static int copy_named(char *dst, const char *src, int k)
{
    int len = (int)strlen(src);
    memcpy(dst, src, (size_t)len + 1);
    dst[0] = (char)('A' + k % 26);
    return len + k;
}

static void __attribute__((noinline)) pawns(MoveList *l)
{
    for (int from = 48; from < 56; from++)
        for (int d = 7; d <= 9; d++)
            add_pawn_move(l, from, from + d, 7, d & 3);
    for (int from = 8; from < 16; from++)
        add_pawn_move(l, from, from + 8, 7, 0);
}

static unsigned __attribute__((noinline)) encode_all(unsigned char *buf, const long *vals, int n)
{
    unsigned at = 0;
    for (int r = 0; r < 3; r++)
        for (int i = 0; i < n; i++)
            at += encode(buf + at, vals[i] * (r + 1), (i + r) % 4);
    return at;
}

int main(void)
{
    MoveList l;
    l.n = 0;
    pawns(&l);
    unsigned h = 0;
    for (int i = 0; i < l.n; i++)
        h = h * 33 + l.m[i];
    printf("moves %d %u\n", l.n, h);

    unsigned char buf[512];
    long vals[] = { 5, -3, 200, -200, 40000, -40000, 1L << 40, -(1L << 50), 0, 127, -128, 32767 };
    unsigned at = encode_all(buf, vals, 12);
    unsigned g = 0;
    for (unsigned i = 0; i < at; i++)
        g = g * 31 + buf[i];
    printf("encoded %u %u\n", at, g);

    char names[4][16];
    int t = 0;
    for (int k = 0; k < 4; k++)
        t += copy_named(names[k], "pawnmove", k * 7);
    t += encode(buf, 77, 2);
    printf("names %d %s %s\n", t, names[1], names[3]);
    return 0;
}
