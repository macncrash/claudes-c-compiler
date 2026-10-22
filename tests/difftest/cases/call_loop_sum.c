/* A perft-style loop: copy a struct through a pointer held in a register,
 * call functions on the copy, skip on an int result tested for zero, and sum
 * the results of a recursive call in a variable carried around the loop
 * (a phi that lives across the calls). Struct copies cover the unrolled
 * sizes, with every tail, and the looped one. */
#include <stdio.h>
#include <string.h>

typedef unsigned long long u64;

typedef struct {
    u64 bits[12];
    int side, ep;
} Pos;

typedef struct {
    int m[64];
    int n;
} List;

__attribute__((noinline)) static void gen(const Pos *p, List *l) {
    l->n = 0;
    for (int i = 0; i < 12; i++)
        if ((p->bits[i] * 0x9e3779b97f4a7c15ULL) >> 62 != (u64)p->side)
            l->m[l->n++] = i;
    if (l->n > 5)
        l->n = 5;
}

__attribute__((noinline)) static void apply(Pos *p, int m) {
    p->bits[m] ^= 0x9e3779b97f4a7c15ULL >> (m + p->side);
    p->bits[(m + 3) % 12] += p->bits[m];
    p->side ^= 1;
}

__attribute__((noinline)) static int bad(const Pos *p, int side) {
    u64 x = (u64)side;
    for (int i = 0; i < 12; i++)
        x = (x ^ p->bits[i]) * 0x100000001b3ULL;
    return (int)((x >> 40) & 3) == 1 ? 3 : 0;
}

static u64 walk(const Pos *p, int depth) {
    List l;
    u64 n = 0;
    gen(p, &l);
    for (int i = 0; i < l.n; i++) {
        Pos next = *p;
        apply(&next, l.m[i]);
        if (bad(&next, p->side))
            continue;
        n += depth == 1 ? 1 : walk(&next, depth - 1);
    }
    return n;
}

/* Several sums carried around a loop with calls, one updated on only some
 * paths. */
__attribute__((noinline)) static long ident(long x) { return x; }

__attribute__((noinline)) static long sums(int n) {
    long a = 0, b = 1, c = 0;
    for (int i = 0; i < n; i++) {
        long v = ident(i * 7 + 3);
        if (v & 1)
            a += v;
        else
            b = b * 3 + v;
        if (ident(v % 5) == 0)
            continue;
        c ^= a + b;
    }
    return a * 1000003 + b * 31 + c;
}

#define COPY(N) \
    __attribute__((noinline)) static unsigned copy##N(const unsigned char *src) { \
        struct { unsigned char b[N]; } s; \
        memcpy(&s, src, N); \
        unsigned h = 0; \
        for (int i = 0; i < N; i++) \
            h = h * 31 + s.b[i]; \
        return h; \
    }
COPY(1) COPY(3) COPY(7) COPY(15) COPY(31) COPY(63) COPY(200) COPY(600)

typedef struct { unsigned char b[600]; } Big;

__attribute__((noinline)) static unsigned copy_big(const Big *src, int k) {
    Big s = *src;
    s.b[k] ^= 0x5a;
    unsigned h = 0;
    for (int i = 0; i < 600; i += 7)
        h = h * 33 + s.b[i];
    return h;
}

int main(void) {
    Pos p;
    for (int i = 0; i < 12; i++)
        p.bits[i] = 0x0123456789abcdefULL * (u64)(i + 1) ^ (0xff00ff00ULL << i);
    p.side = 0;
    p.ep = -1;
    for (int d = 1; d <= 5; d++)
        printf("walk(%d) = %llu\n", d, walk(&p, d));
    for (int n = 0; n < 40; n += 13)
        printf("sums(%d) = %ld\n", n, sums(n));
    static unsigned char buf[700];
    for (int i = 0; i < 700; i++)
        buf[i] = (unsigned char)(i * 37 + 11);
    printf("%u %u %u %u %u %u %u %u %u\n", copy1(buf + 1), copy3(buf + 2), copy7(buf + 3), copy15(buf + 4),
           copy31(buf + 5), copy63(buf + 6), copy200(buf + 7), copy600(buf + 8), copy_big((const Big *)(buf + 16), 99));
    return 0;
}
