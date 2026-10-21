/* Built by the reference compiler: functions that take and return GCC
 * vector types, and calls back into the program's own functions. */
#include <stdint.h>

typedef float f4 __attribute__((vector_size(16)));
typedef double d2 __attribute__((vector_size(16)));
typedef int32_t i4 __attribute__((vector_size(16)));
typedef uint8_t u16b __attribute__((vector_size(16)));
typedef float f2 __attribute__((vector_size(8)));
typedef int32_t i2 __attribute__((vector_size(8)));
typedef uint16_t u4h __attribute__((vector_size(8)));

struct hfa2 { double a, b; };

f4 h_scale_f4(f4 v, float s) { return v * s; }
d2 h_add_d2(d2 a, d2 b) { return a + b; }
i4 h_mix_i4(int k, i4 a, i4 b) { return a * k - b; }
u16b h_xor_u16b(u16b a, u16b b) { return a ^ b; }
f2 h_neg_f2(f2 v) { return -v; }
i2 h_swap_i2(i2 v) { return (i2){ v[1], v[0] }; }
u4h h_shift_u4h(u4h v, int n) { return v << n; }

/* Vectors mixed with scalars and an HFA struct: SIMD registers are shared. */
double h_mixed(double x, f4 a, struct hfa2 s, float y, d2 b, int n, i2 c) {
    return x + a[0] + a[3] * 2 + s.a * 3 + s.b * 4 + y * 5 + b[0] * 6 + b[1] * 7 + n * 8 + c[0] * 9 + c[1] * 10;
}

/* Ten vector arguments: the last two go on the stack. */
float h_many(f4 a, f4 b, f4 c, f4 d, f4 e, f4 f, f4 g, f4 h, f4 i, f4 j) {
    f4 t = a + b * 2 + c * 3 + d * 4 + e * 5 + f * 6 + g * 7 + h * 8 + i * 9 + j * 10;
    return t[0] + t[1] + t[2] + t[3];
}

/* Call back into the program's functions through pointers. */
f4 h_apply_f4(f4 (*fn)(f4, f4), f4 a, f4 b) { return fn(a, b) * 2.0f; }
i2 h_apply_i2(i2 (*fn)(i2, int), i2 v) { return fn(v, 3) + 1; }
float h_apply_many(float (*fn)(f4, f4, f4, f4, f4, f4, f4, f4, f4, d2)) {
    f4 one = { 1, 2, 3, 4 };
    return fn(one, one * 2, one * 3, one * 4, one * 5, one * 6, one * 7, one * 8, one * 9, (d2){ 0.5, 0.25 });
}
