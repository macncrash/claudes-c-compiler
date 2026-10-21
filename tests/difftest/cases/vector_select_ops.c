/* Vector selects and clamps written with masks: a & ~b (folded into one
 * instruction when ~b has no other use, kept when it does or when b changes
 * in between), results assigned straight to variables (including variables
 * the operation itself reads), a constant literal copied into a variable
 * in a loop, and short vectors and float structs returned in SIMD
 * registers by functions that need no stack frame. */
#include <stdio.h>

typedef float f4 __attribute__((vector_size(16)));
typedef int i4 __attribute__((vector_size(16)));
typedef unsigned u2 __attribute__((vector_size(8)));
typedef double d2 __attribute__((vector_size(16)));

struct pt { float x, y; };
struct quad { double a, b, c, d; };

static f4 xs[32], ys[32], out[32];
static unsigned long long h = 1469598103934665603ULL;

static void mix(const void *p, unsigned long n) {
    const unsigned char *b = p;
    for (unsigned long i = 0; i < n; i++)
        h = (h ^ b[i]) * 1099511628211ULL;
}

__attribute__((noinline)) static void clamp(float limit) {
    for (int i = 0; i < 32; i++) {
        f4 x = xs[i], y = ys[i];
        i4 m = x > y;
        f4 v = (f4)(((i4)x & m) | ((i4)y & ~m));
        i4 hi = v > limit, lo = v < -limit;
        f4 lim = (f4){ limit, limit, limit, limit };
        v = (f4)(((i4)v & ~(hi | lo)) | ((i4)lim & hi) | ((i4)(-lim) & lo));
        out[i] = v;
    }
}

__attribute__((noinline)) static void masks(void) {
    for (int i = 0; i < 32; i++) {
        i4 a = (i4)xs[i], b = (i4)ys[i];
        i4 nb = ~b;
        i4 r1 = a & nb;              /* ~b used twice: kept */
        i4 r2 = nb & (a ^ 0x55);
        i4 c = b;
        i4 nc = ~c;
        c = c + 1;                   /* source changes before the And */
        i4 r3 = (a & nc) ^ c;
        i4 v = a;
        v = v & ~b;                  /* the result goes to a variable it reads */
        v = ~a & v;
        v = (v | b) & ~(a >> 3);
        out[i] = (f4)(r1 + r2 + r3 + v);
    }
}

/* A literal copied into a variable that the loop then changes: the copy
 * must happen every iteration. */
__attribute__((noinline)) static void accumulate(int n) {
    for (int i = 0; i < n; i++) {
        f4 k = (f4){ 0.5f, 1.5f, -2.0f, 4.0f };
        if (i & 1)
            k = k * 2.0f;
        out[i & 31] += k;
    }
}

__attribute__((noinline)) static u2 step(u2 acc, u2 v, unsigned k) {
    return acc * 3u + (v ^ k);
}

__attribute__((noinline)) static d2 dstep(d2 a, double s) {
    return a * s - (d2){ 1.0, -1.0 };
}

__attribute__((noinline)) static struct pt mid(struct pt a, struct pt b) {
    struct pt r = { (a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f };
    return r;
}

__attribute__((noinline)) static struct quad scale(struct quad q, double s) {
    struct quad r = { q.a * s, q.b * s, q.c * s, q.d * s };
    return r;
}

int main(int argc, char **argv) {
    (void)argv;
    for (int i = 0; i < 32; i++) {
        xs[i] = (f4){ i * 7.5f - 100, 50 - i * 3.0f, i * i * 0.25f, -i * 9.0f };
        ys[i] = (f4){ 20 - i * 2.0f, i * 4.0f - 30, 90 - i * 5.0f, i * 1.5f };
    }
    clamp(60.0f + argc);
    mix(out, sizeof out);
    printf("clamp %g %g %g %g\n", out[0][0], out[5][1], out[17][2], out[31][3]);
    masks();
    mix(out, sizeof out);
    printf("masks %d %d\n", ((i4)out[3])[1], ((i4)out[29])[2]);
    accumulate(70);
    mix(out, sizeof out);
    printf("acc %g %g\n", out[0][1], out[31][3]);

    u2 acc = { 1, 2 };
    d2 d = { 1.0, 2.0 };
    struct pt p = { 0, 0 };
    struct quad q = { 1, 2, 3, 4 };
    for (int i = 0; i < 50; i++) {
        acc = step(acc, (u2){ (unsigned)i, -(unsigned)i }, i & 7);
        d = dstep(d, 0.5 + i * 0.01);
        p = mid(p, (struct pt){ (float)i, (float)-i });
        q = scale(q, i & 1 ? 0.5 : 2.0);
    }
    mix(&acc, sizeof acc);
    printf("calls %u %u %g %g %g %g %g %g\n", acc[0], acc[1], d[0], d[1], p.x, p.y, q.a, q.d);
    printf("hash %016llx\n", h);
    return 0;
}
