/* Bitwise selects (x & m) | (y & ~m) in every operand order, including one
 * whose mask changes between the two halves; a vector negation of a value
 * that stays constant in a loop (computed once) next to one that changes;
 * small constant broadcasts at the edges of what fits an immediate; calls
 * passing vectors and float structs in a loop, with values live across the
 * call; and 32-bit unsigned negation and complement that wrap. */
#include <stdio.h>

typedef int i4 __attribute__((vector_size(16)));
typedef float f4 __attribute__((vector_size(16)));
typedef unsigned u2 __attribute__((vector_size(8)));
typedef unsigned char u8x8 __attribute__((vector_size(8)));
typedef signed char s8x8 __attribute__((vector_size(8)));
typedef unsigned short u16x4 __attribute__((vector_size(8)));
typedef long long s64x2 __attribute__((vector_size(16)));

struct pt { float x, y; };

static i4 xs[16], ys[16], out[16];
static unsigned long long h = 1469598103934665603ULL;

static void mix(const void *p, unsigned long n) {
    const unsigned char *b = p;
    for (unsigned long i = 0; i < n; i++)
        h = (h ^ b[i]) * 1099511628211ULL;
}

__attribute__((noinline)) static void selects(void) {
    for (int i = 0; i < 16; i++) {
        i4 x = xs[i], y = ys[i];
        i4 m = x > y;
        i4 r1 = (x & m) | (y & ~m);
        i4 r2 = (m & x) | (~m & y);
        i4 r3 = (y & ~m) | (x & m);
        i4 a = x & m;
        m = m ^ 0x0f0f;               /* the mask changes: no select */
        i4 b = y & ~m;
        i4 r4 = a | b;
        x = (x & m) | (y & ~m);       /* the result replaces an operand */
        out[i] = r1 + r2 * 3 + r3 * 5 + r4 * 7 + x * 11;
    }
}

__attribute__((noinline)) static void ext(f4 *p) { *p = *p * 0.5f; }

__attribute__((noinline)) static void clamp(float limit, int n) {
    f4 lim = (f4){ limit, limit, limit, limit };
    f4 drift = lim;
    for (int i = 0; i < n; i++) {
        f4 v = (f4)(xs[i & 15] * 3);
        f4 lo = -lim;                 /* constant in the loop */
        f4 lo2 = -drift;              /* changes every iteration */
        i4 below = v < lo, below2 = v < lo2;
        v = (f4)(((i4)v & ~below) | ((i4)lo & below));
        ext(&v);                      /* a call inside the loop */
        out[i & 15] = (i4)v + below2;
        drift = drift * 0.75f;
    }
}

__attribute__((noinline)) static void broadcasts(void) {
    u8x8 a = { 1, 2, 3, 250, 251, 252, 253, 254 };
    s8x8 b = { -3, -2, -1, 0, 1, 2, 3, 4 };
    u16x4 c = { 1, 2, 300, 65535 };
    s64x2 d = { -5, 5 };
    u8x8 ra = (a + 255) ^ (a & 0);
    s8x8 rb = (b * -1) + (b ^ 1);
    u16x4 rc = (c + 200) * (c + 256);
    s64x2 rd = (d & 0) + (d ^ 1);
    f4 rf = (f4){ 1.5f, -2.0f, 0.0f, 3.0f } * 0.0f + 0.0f;
    mix(&ra, 8); mix(&rb, 8); mix(&rc, 8); mix(&rd, 16); mix(&rf, 16);
    printf("broadcast %u %d %u %lld\n", ra[3], rb[2], rc[3], (long long)rd[0]);
}

__attribute__((noinline)) static u2 step(u2 acc, u2 v, unsigned k) {
    return acc * 3u + (v ^ k);
}

__attribute__((noinline)) static struct pt mid(struct pt a, struct pt b, int w) {
    return (struct pt){ (a.x + b.x) * w, (a.y - b.y) * w };
}

__attribute__((noinline)) static void calls(int n) {
    u2 acc = { 1, 2 };
    struct pt p = { 1.0f, 2.0f };
    unsigned live = 7;
    for (int i = 0; i < n; i++) {
        unsigned k = i & 7, kk = (unsigned)i * 2654435761u;
        acc = step(acc, (u2){ (unsigned)i, -(unsigned)i }, k) + (u2){ kk, ~kk };
        p = mid(p, (struct pt){ (float)i, (float)k }, (int)(k & 1) + 1);
        live = live * 31 + kk + k;    /* live across both calls */
    }
    mix(&acc, sizeof acc);
    printf("calls %u %u %g %g %u\n", acc[0], acc[1], p.x, p.y, live);
}

int main(int argc, char **argv) {
    (void)argv;
    for (int i = 0; i < 16; i++) {
        xs[i] = (i4){ i * 7 - 50, 13 - i, i * i - 40, -i * 3 };
        ys[i] = (i4){ 20 - i * 2, i * 5 - 30, 9 - i, i * 11 - 77 };
    }
    selects();
    mix(out, sizeof out);
    printf("selects %d %d %d\n", out[0][0], out[7][2], out[15][3]);
    clamp(40.0f + argc, 40);
    mix(out, sizeof out);
    printf("clamp %d %d\n", out[3][1], out[12][3]);
    broadcasts();
    calls(100);
    unsigned u = 0;
    for (unsigned i = 0; i < 70000; i += 999)
        u = u * 33 + -i + ~(i * 3);
    printf("unsigned %u\n", u);
    printf("hash %016llx\n", h);
    return 0;
}
