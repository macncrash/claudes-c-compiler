/* GCC vector-extension operators on every lane type, for 16- and 8-byte
 * vectors, with vector and scalar operands and compound assignment. On
 * AArch64 these compile to whole-vector NEON instructions; elsewhere they
 * are lowered lane by lane. Signed overflow is avoided (UB), and shift
 * counts stay below the lane width. */
#include <stdio.h>
#include <string.h>

typedef signed char        v16i8  __attribute__((vector_size(16)));
typedef unsigned char      v16u8  __attribute__((vector_size(16)));
typedef short              v8i16  __attribute__((vector_size(16)));
typedef unsigned short     v8u16  __attribute__((vector_size(16)));
typedef int                v4i32  __attribute__((vector_size(16)));
typedef unsigned           v4u32  __attribute__((vector_size(16)));
typedef long long          v2i64  __attribute__((vector_size(16)));
typedef unsigned long long v2u64  __attribute__((vector_size(16)));
typedef float              v4f32  __attribute__((vector_size(16)));
typedef double             v2f64  __attribute__((vector_size(16)));
typedef signed char        v8i8   __attribute__((vector_size(8)));
typedef unsigned short     v4u16  __attribute__((vector_size(8)));
typedef int                v2i32  __attribute__((vector_size(8)));
typedef float              v2f32  __attribute__((vector_size(8)));

typedef long long          m2i64  __attribute__((vector_size(16)));
typedef int                m4i32  __attribute__((vector_size(16)));

static unsigned long long hash;

static void mix(const char *what, const void *p, size_t n) {
    const unsigned char *b = p;
    unsigned long long h = 1469598103934665603ull;
    for (size_t i = 0; i < n; i++)
        h = (h ^ b[i]) * 1099511628211ull;
    hash = hash * 31 + h;
    printf("%-12s %016llx\n", what, h);
}
#define MIX(name, v) do { __typeof__(v) t_ = (v); mix(name, &t_, sizeof t_); } while (0)

typedef unsigned v4u32_t __attribute__((vector_size(16)));
typedef float v4f32_t __attribute__((vector_size(16)));

__attribute__((noinline)) static v4u32_t bump(v4u32_t v) { return v + 1u; }
__attribute__((noinline)) static v4f32_t scale(v4f32_t v) { return v * 0.5f; }

/* Integer vectors: every operator, vector/scalar mixes, compound forms. */
#define INT_TESTS(T, a, b, s, sh)                                        \
    do {                                                                 \
        T x = (a), y = (b); __typeof__(s) s_ = (s);                                                  \
        MIX(#T " add", x + y);  MIX(#T " sub", x - y);                   \
        MIX(#T " mul", x * y);  MIX(#T " and", x & y);                   \
        MIX(#T " or", x | y);   MIX(#T " xor", x ^ y);                   \
        MIX(#T " shlv", x << sh); MIX(#T " shrv", x >> sh);              \
        MIX(#T " shls", x << 3); MIX(#T " shrs", x >> 2);                \
        MIX(#T " eq", x == y);  MIX(#T " ne", x != y);                   \
        MIX(#T " lt", x < y);   MIX(#T " le", x <= y);                   \
        MIX(#T " gt", x > y);   MIX(#T " ge", x >= y);                   \
        MIX(#T " neg", -x);     MIX(#T " not", ~x);                      \
        MIX(#T " vs", x + s_);   MIX(#T " sv", s_ - x);                    \
        MIX(#T " cmps", x < s_); MIX(#T " smul", s_ * y);                  \
        T z = x; z += y; z *= s_; z ^= x; z <<= 1; z >>= 1; z -= s_;       \
        MIX(#T " cmpd", z);                                              \
    } while (0)

#define FLT_TESTS(T, a, b, s)                                            \
    do {                                                                 \
        T x = (a), y = (b); __typeof__(s) s_ = (s);                                                  \
        MIX(#T " add", x + y);  MIX(#T " sub", x - y);                   \
        MIX(#T " mul", x * y);  MIX(#T " div", x / y);                   \
        MIX(#T " eq", x == y);  MIX(#T " ne", x != y);                   \
        MIX(#T " lt", x < y);   MIX(#T " le", x <= y);                   \
        MIX(#T " gt", x > y);   MIX(#T " ge", x >= y);                   \
        MIX(#T " neg", -x);                                              \
        MIX(#T " vs", x * s_);   MIX(#T " sv", s_ / y);                    \
        T z = x; z += y; z *= s_; z /= y; z -= s_;                         \
        MIX(#T " cmpd", z);                                              \
    } while (0)

int main(int argc, char **argv) {
    (void)argv;
    int k = argc;  /* runtime values keep the operations out of the folder */

    INT_TESTS(v16i8, ((v16i8){1, -2, 3, -4, 5, -6, 7, -8, 9, 10, -11, 12, 0, 1, 2, 3}) * (signed char)k,
              ((v16i8){2, 2, -3, 4, 6, -6, 0, 1, -9, 3, 4, 5, 6, 7, 8, -1}),
              (signed char)(3 * k), ((v16i8){0, 1, 2, 3, 4, 5, 6, 0, 1, 2, 3, 4, 5, 6, 0, 1}));
    INT_TESTS(v16u8, ((v16u8){1, 200, 3, 255, 5, 128, 7, 8, 9, 10, 11, 12, 0, 1, 2, 3}) * (unsigned char)k,
              ((v16u8){2, 2, 3, 4, 6, 6, 0, 1, 9, 3, 4, 5, 6, 7, 8, 255}),
              (unsigned char)(5 * k), ((v16u8){0, 1, 2, 3, 4, 5, 6, 7, 1, 2, 3, 4, 5, 6, 7, 0}));
    INT_TESTS(v8i16, ((v8i16){100, -200, 300, -400, 5, -6, 7000, -8}) * (short)k,
              ((v8i16){2, 2, -3, 4, 6, -6, 0, 1}), (short)(7 * k), ((v8i16){0, 1, 2, 3, 13, 14, 7, 15}));
    INT_TESTS(v8u16, ((v8u16){100, 60000, 300, 400, 5, 6, 7000, 8}) * (unsigned short)k,
              ((v8u16){2, 2, 3, 4, 6, 6, 0, 1}), (unsigned short)(9 * k), ((v8u16){0, 1, 2, 3, 13, 14, 7, 15}));
    INT_TESTS(v4i32, ((v4i32){100000, -2, 3, -400000}) * k, ((v4i32){2, -7, 3, 4}), 11 * k,
              ((v4i32){0, 31, 5, 17}));
    INT_TESTS(v4u32, ((v4u32){100000, 4000000000u, 3, 7}) * (unsigned)k, ((v4u32){2, 7, 3, 4}),
              13u * k, ((v4u32){0, 31, 5, 17}));
    INT_TESTS(v2i64, ((v2i64){-5000000000LL, 123456789}) * k, ((v2i64){3, -9}), 17LL * k,
              ((v2i64){1, 63}));
    INT_TESTS(v2u64, ((v2u64){18000000000000000000ull, 123456789}) * (unsigned long long)k,
              ((v2u64){3, 9}), 19ull * k, ((v2u64){1, 63}));
    INT_TESTS(v8i8, ((v8i8){1, -2, 3, -4, 5, -6, 7, -8}) * (signed char)k, ((v8i8){2, 2, -3, 4, 6, -6, 0, 1}),
              (signed char)(2 * k), ((v8i8){0, 1, 2, 3, 4, 5, 6, 7}));
    INT_TESTS(v4u16, ((v4u16){65535, 2, 300, 4}) * (unsigned short)k, ((v4u16){2, 3, 4, 5}),
              (unsigned short)(3 * k), ((v4u16){1, 15, 4, 8}));
    INT_TESTS(v2i32, ((v2i32){-7, 2000000}) * k, ((v2i32){3, -4}), 5 * k, ((v2i32){4, 30}));

    FLT_TESTS(v4f32, ((v4f32){1.5f, -2.25f, 3.0f, 1e10f}) * (float)k, ((v4f32){0.5f, -2.25f, 4.0f, -3.0f}),
              2.5f * k);
    FLT_TESTS(v2f64, ((v2f64){1.5, -1e300}) * (double)k, ((v2f64){-0.25, 3.0}), 0.75 * k);
    FLT_TESTS(v2f32, ((v2f32){7.0f, -0.125f}) * (float)k, ((v2f32){2.0f, -0.125f}), 1.25f * k);

    /* Lane-mask selects and casts between same-size vectors. */
    v4f32 fa = { 1, 5, 3, 7 }, fb = { 4, 2, 6, 0 };
    m4i32 m = fa > fb;
    MIX("select", (v4f32)(((m4i32)fa & m) | ((m4i32)fb & ~m)));
    v2f64 da = { 1.0, -2.0 }, db = { 1.0, 3.0 };
    m2i64 dm = da == db;
    MIX("dmask", dm);

    /* Results through pointers and arrays. */
    v4i32 arr[3] = { { 1, 2, 3, 4 }, { 5, 6, 7, 8 }, { 9, 10, 11, 12 } };
    v4i32 *p = arr;
    for (int i = 0; i < 3; i++)
        p[i] = p[i] * k + arr[(i + 1) % 3];
    p[1] -= p[2];
    mix("arrays", arr, sizeof arr);

    /* Local arrays of vectors: braced lanes, vector expressions, designators,
     * and implicit zeroing. */
    v4i32 seed = { 7, 8, 9, 10 };
    v4i32 inits[4] = { { 1, 2 }, seed * k, [3] = { 4, 5, 6, 7 } };
    v2f64 dinit[2] = { [1] = { 0.5, -0.5 } };
    mix("inits", inits, sizeof inits);
    mix("dinit", dinit, sizeof dinit);
    __typeof__(seed + 1) tsum = seed + 1;
    __typeof__(-dinit[1]) tneg = -dinit[1];
    MIX("typeof", tsum);
    MIX("typeofneg", tneg);

    /* Intermediate results kept in registers: a temporary used as a shift
     * count and again afterwards, a call between producer and consumer, and
     * more simultaneously live temporaries than there are registers. */
    v4u32 ua = { 1, 2, 3, 4 }, ub = { 5, 6, 7, 8 };
    MIX("tmpshift", ((ua * ub) >> (ub - ua)) + (ub - ua) * (ua << (ub - ua)));
    MIX("tmpcall", (ua * ub) + bump(ub) * (ua - ub));
    MIX("tmpdeep", ((ua + ub) * (ua - ub)) ^ ((ua * 3u + ub) | ((ub >> 1) - (ua & ub)))
                   ^ (((ua ^ ub) + (ua | ub)) * ((ub - 1u) + (ua + 2u))));
    v4f32 fx = { 1.5f, -2.0f, 3.25f, 0.5f };
    MIX("tmpfloat", (fx * fx - fx / (fx + 4.0f)) * (fx - 1.0f) + fx * scale((v4f32){ 2, 2, 2, 2 }));

    /* Vector variables kept in registers across a loop with a call in it,
     * mixing whole-vector updates with lane reads and writes. */
    v4u32 acc = { 0, 1, 2, 3 }, step = { 3, 5, 7, 11 };
    v2f64 dacc = { 1.0, -1.0 };
    for (int i = 0; i < 5 + k; i++) {
        acc = acc * 3u + step;
        acc[i & 3] ^= (unsigned)i;
        step = bump(step) - acc[0];
        dacc = dacc * 0.5 + (v2f64){ (double)acc[1], (double)i };
        dacc[1] -= dacc[0];
    }
    MIX("varloop", acc);
    MIX("varstep", step);
    MIX("vardacc", dacc);

    printf("all %016llx\n", hash);
    return 0;
}
