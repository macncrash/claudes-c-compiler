/* Short vectors and HFA structs moving between calls without going through
 * memory: parameters moved straight into the registers that hold them,
 * results returned from a register or a struct's own storage, and call
 * results kept in a register for the vector ops that use them. */
#include <stdio.h>

typedef float f4 __attribute__((vector_size(16)));
typedef unsigned u2 __attribute__((vector_size(8)));
typedef double d2 __attribute__((vector_size(16)));

struct hfa3 { float a, b, c; };
struct hfa4 { double a, b, c, d; };

__attribute__((noinline)) static u2 step(u2 acc, u2 v, unsigned k) {
    return acc * 3u + (v ^ k);
}

/* Returns a parameter unchanged. */
__attribute__((noinline)) static f4 ident(f4 v) { return v; }

/* Returns a parameter that lives across a call. */
__attribute__((noinline)) static f4 keep(f4 v, int n) {
    printf("keep %d\n", n);
    return v;
}

/* Several returns, one of them a call result. */
__attribute__((noinline)) static d2 pick(d2 a, d2 b, int which) {
    if (which == 0)
        return a;
    if (which == 1)
        return a + b;
    return pick(b, a, which - 2);
}

/* HFA structs returned from their own storage, through a pointer and
 * from a local. */
__attribute__((noinline)) static struct hfa3 load3(const struct hfa3 *p) { return *p; }
__attribute__((noinline)) static struct hfa4 make4(double x) {
    struct hfa4 r = { x, x * 2, x * 3, x * 4 };
    return r;
}
__attribute__((noinline)) static struct hfa4 choose4(const struct hfa4 *p, const struct hfa4 *q, int c) {
    return c ? *p : *q;
}

int main(int argc, char **argv) {
    (void)argv;
    u2 acc = { 1, 2 };
    for (unsigned i = 0; i < 20; i++)
        acc = step(acc, (u2){ i, -i }, i & 7);
    printf("step %u %u\n", acc[0], acc[1]);

    /* Call results used directly by vector ops and by other calls. */
    f4 a = { 1, 2, 3, 4 };
    f4 b = ident(a) * 2 + ident(a + 1);
    printf("ident %g %g %g %g\n", b[0], b[1], b[2], b[3]);
    f4 c = ident(ident(b) - a);
    printf("nest %g %g %g %g\n", c[0], c[1], c[2], c[3]);
    f4 k = keep(c, argc) + keep(a, argc + 1);
    printf("keep %g %g %g %g\n", k[0], k[1], k[2], k[3]);

    for (int w = 0; w < 4; w++) {
        d2 p = pick((d2){ 1.5, 2.5 }, (d2){ 10, 20 }, w);
        printf("pick%d %g %g\n", w, p[0], p[1]);
    }

    struct hfa3 s3 = { 1.25f, 2.5f, 3.75f };
    struct hfa3 t3 = load3(&s3);
    printf("load3 %g %g %g\n", t3.a, t3.b, t3.c);
    struct hfa4 m = make4(argc + 0.5);
    printf("make4 %g %g %g %g\n", m.a, m.b, m.c, m.d);
    struct hfa4 n = make4(7);
    for (int i = 0; i < 2; i++) {
        struct hfa4 r = choose4(&m, &n, i);
        printf("choose4 %g %g %g %g\n", r.a, r.b, r.c, r.d);
    }
    return 0;
}
