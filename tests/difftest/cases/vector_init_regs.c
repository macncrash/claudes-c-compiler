/* Vector initializers built in registers: compound literals and
 * initialized locals that are passed, returned or copied whole, partial
 * initializers, calls in the middle of an initializer, and lane updates
 * that must keep the other lanes across loop iterations and calls. */
#include <stdint.h>
#include <stdio.h>

typedef uint32_t u2 __attribute__((vector_size(8)));
typedef float f4 __attribute__((vector_size(16)));
typedef double d2 __attribute__((vector_size(16)));
typedef uint8_t b8 __attribute__((vector_size(8)));
typedef int16_t h8 __attribute__((vector_size(16)));

static int calls;

__attribute__((noinline)) static u2 step(u2 acc, u2 v, uint32_t k) { return acc * 3u + (v ^ k); }
__attribute__((noinline)) static float sum4(f4 v) { return v[0] + v[1] * 2 + v[2] * 3 + v[3] * 4; }
__attribute__((noinline)) static float side(float x) { calls++; return x * 10; }
__attribute__((noinline)) static uint32_t mix(b8 b, h8 h) {
    uint32_t s = 0;
    for (int i = 0; i < 8; i++)
        s = s * 31 + b[i] + (uint16_t)h[i];
    return s;
}
/* Ten vector arguments: the last two are passed on the stack. */
__attribute__((noinline)) static float many(f4 a, f4 b, f4 c, f4 d, f4 e, f4 f, f4 g, f4 h, f4 i, f4 j) {
    return sum4(a) + sum4(b) + sum4(c) + sum4(d) + sum4(e) + sum4(f) + sum4(g) + sum4(h) + sum4(i) * 7 + sum4(j) * 11;
}

/* Returned initializers. */
__attribute__((noinline)) static f4 make(float x, float y) { return (f4){ x, y, 0, 1 }; }
__attribute__((noinline)) static d2 make_d(double x) { d2 r = { x, -x }; return r; }
__attribute__((noinline)) static f4 make_partial(float x) { f4 r = { x }; return r; }
__attribute__((noinline)) static b8 make_b(uint8_t x) { return (b8){ x, 1, 2, 3, x, 5, 6, 7 }; }

int main(int argc, char **argv) {
    (void)argv;
    u2 acc = { 1, 2 };
    for (uint32_t i = 0; i < 30; i++)
        acc = step(acc, (u2){ i, -i }, i & 7);
    printf("step %u %u\n", acc[0], acc[1]);

    float x = argc + 0.5f;
    printf("sum4 %g %g\n", sum4((f4){ x, x, 1, 2 }), sum4((f4){ x }));
    f4 m = make(x, 3);
    f4 p = make_partial(x);
    d2 d = make_d(x);
    printf("make %g %g %g %g | %g %g %g %g | %g %g\n", m[0], m[1], m[2], m[3], p[0], p[1], p[2], p[3], d[0], d[1]);
    b8 bb = make_b(200);
    printf("make_b %u %u %u\n", bb[0], bb[4], bb[7]);

    /* Calls in the middle of an initializer: the lanes stored before each
     * call have to survive it. */
    f4 c = { side(1), side(2), x, side(3) };
    printf("side %g %g %g %g calls %d\n", c[0], c[1], c[2], c[3], calls);
    printf("sum4 side %g\n", sum4((f4){ x, side(4), side(5), x }));

    /* Byte and halfword lanes, copied to another variable before use. */
    for (int k = 0; k < 3; k++) {
        b8 b = { (uint8_t)k, 1, 2, 3, 4, 5, 6, (uint8_t)(k * 40) };
        h8 h = { (int16_t)-k, 2, 3, 4, 5, 6, 7, (int16_t)(k * 1000) };
        b8 b2 = b;
        printf("mix %u\n", mix(b2, h));
    }

    /* One lane updated per iteration: the other lane carries over, across
     * the call, from the previous iteration. */
    u2 v = { 7, 9 };
    for (uint32_t i = 0; i < 4; i++) {
        v[i & 1] = i * 11;
        u2 r = step(v, v, i);
        printf("carry %u %u -> %u %u\n", v[0], v[1], r[0], r[1]);
    }

    /* Initializers passed in registers and on the stack. */
    printf("many %g\n", many((f4){ 1 }, (f4){ 0, 1 }, (f4){ 0, 0, 1 }, (f4){ 0, 0, 0, 1 },
                             (f4){ x, 0, 0, 0 }, (f4){ 0, x }, (f4){ 0, 0, x }, (f4){ 0, 0, 0, x },
                             (f4){ 1, 2, 3, 4 }, (f4){ x, x, x, x }));
    return 0;
}
