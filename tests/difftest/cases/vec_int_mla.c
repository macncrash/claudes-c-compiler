/* Integer vector multiply-adds, fused into mla/mls on AArch64: every lane
 * type from 8 to 32 bits, signed and unsigned, with wrapping products;
 * the product on either side of + and -, broadcast scalars, a product used
 * again (not fused), and 64-bit lanes (no vector multiply). */
#include <stdio.h>

typedef signed char i8x16 __attribute__((vector_size(16)));
typedef unsigned char u8x8 __attribute__((vector_size(8)));
typedef short i16x8 __attribute__((vector_size(16)));
typedef unsigned short u16x4 __attribute__((vector_size(8)));
typedef int i32x4 __attribute__((vector_size(16)));
typedef unsigned u32x4 __attribute__((vector_size(16)));
typedef unsigned u32x2 __attribute__((vector_size(8)));
typedef long long i64x2 __attribute__((vector_size(16)));

typedef unsigned long long u64;
static u64 h;

static void mix(const void *p, unsigned n) {
    const unsigned char *b = p;
    for (unsigned i = 0; i < n; i++)
        h = (h ^ b[i]) * 0x100000001b3ULL;
}

#define KERNELS(T, S)                                                            \
    __attribute__((noinline)) static T S##_madd(T a, T b, T c) { return a * b + c; } \
    __attribute__((noinline)) static T S##_addm(T a, T b, T c) { return c + a * b; } \
    __attribute__((noinline)) static T S##_msub(T a, T b, T c) { return c - a * b; } \
    __attribute__((noinline)) static T S##_subc(T a, T b, T c) { return a * b - c; } \
    __attribute__((noinline)) static T S##_twice(T a, T b, T c, T *keep) {         \
        T p = a * b;                                                             \
        *keep = p;                                                               \
        return p + c;                                                            \
    }

KERNELS(i8x16, i8)
KERNELS(u8x8, u8)
KERNELS(i16x8, i16)
KERNELS(u16x4, u16)
KERNELS(i32x4, i32)
KERNELS(u32x4, u32)
KERNELS(u32x2, u32d)
KERNELS(i64x2, i64)

/* A broadcast scalar factor and addend, as in a hash step. */
__attribute__((noinline)) static u32x4 hash_step(u32x4 x, unsigned k) {
    return x * 2654435761u + k;
}

__attribute__((noinline)) static i16x8 scaled(i16x8 x, short s, i16x8 acc) {
    return acc - x * s;
}

/* Several fused steps in a loop. */
__attribute__((noinline)) static u32x4 chain(u32x4 x, int n) {
    for (int i = 0; i < n; i++)
        x = x * (u32x4){ 3, 5, 7, 9 } + (u32x4){ 1, 2, 3, 4 } - x * x;
    return x;
}

#define RUN(T, S, N)                                                             \
    do {                                                                         \
        for (int r = 0; r < 24; r++) {                                           \
            T a, b, c, keep;                                                     \
            for (int i = 0; i < N; i++) {                                        \
                a[i] = (r * 1103515245u + i * 12345u) >> 3;                      \
                b[i] = (r * 22695477u + i * 7919u + 77u) >> 5;                   \
                c[i] = (r * 69069u + i * 1013904223u) >> 7;                      \
            }                                                                    \
            T v = S##_madd(a, b, c); mix(&v, sizeof v);                          \
            v = S##_addm(a, b, c); mix(&v, sizeof v);                            \
            v = S##_msub(a, b, c); mix(&v, sizeof v);                            \
            v = S##_subc(a, b, c); mix(&v, sizeof v);                            \
            v = S##_twice(a, b, c, &keep); mix(&v, sizeof v); mix(&keep, sizeof keep); \
        }                                                                        \
        printf(#S " %llu\n", h);                                                 \
    } while (0)

int main(void) {
    RUN(i8x16, i8, 16);
    RUN(u8x8, u8, 8);
    RUN(i16x8, i16, 8);
    RUN(u16x4, u16, 4);
    RUN(i32x4, i32, 4);
    RUN(u32x4, u32, 4);
    RUN(u32x2, u32d, 2);
    RUN(i64x2, i64, 2);
    u32x4 x = { 1, 2, 3, 0xffffffffu };
    for (unsigned k = 0; k < 50; k++) {
        x = hash_step(x, k);
        mix(&x, sizeof x);
    }
    i16x8 acc = { 0 };
    for (short s = -20; s < 20; s += 3) {
        acc = scaled((i16x8){ s, 2, -3, 4, 30000, -30000, 7, 8 }, s, acc);
        mix(&acc, sizeof acc);
    }
    u32x4 y = chain((u32x4){ 11, 22, 33, 44 }, 37);
    mix(&y, sizeof y);
    printf("rest %llu\n", h);
    return 0;
}
