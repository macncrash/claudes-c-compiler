/* AAPCS64 short vectors: 8- and 16-byte GCC vector types are passed in
 * single SIMD registers (d/q), share them with float, double and HFA
 * arguments, overflow to the stack after v7, and come back in v0. The
 * helper is built by the reference compiler, so every call here crosses
 * the compiler boundary in one direction or the other. */
#include <stdint.h>
#include <stdio.h>

typedef float f4 __attribute__((vector_size(16)));
typedef double d2 __attribute__((vector_size(16)));
typedef int32_t i4 __attribute__((vector_size(16)));
typedef uint8_t u16b __attribute__((vector_size(16)));
typedef float f2 __attribute__((vector_size(8)));
typedef int32_t i2 __attribute__((vector_size(8)));
typedef uint16_t u4h __attribute__((vector_size(8)));

struct hfa2 { double a, b; };

f4 h_scale_f4(f4 v, float s);
d2 h_add_d2(d2 a, d2 b);
i4 h_mix_i4(int k, i4 a, i4 b);
u16b h_xor_u16b(u16b a, u16b b);
f2 h_neg_f2(f2 v);
i2 h_swap_i2(i2 v);
u4h h_shift_u4h(u4h v, int n);
double h_mixed(double x, f4 a, struct hfa2 s, float y, d2 b, int n, i2 c);
float h_many(f4 a, f4 b, f4 c, f4 d, f4 e, f4 f, f4 g, f4 h, f4 i, f4 j);
f4 h_apply_f4(f4 (*fn)(f4, f4), f4 a, f4 b);
i2 h_apply_i2(i2 (*fn)(i2, int), i2 v);
float h_apply_many(float (*fn)(f4, f4, f4, f4, f4, f4, f4, f4, f4, d2));

/* Called by the helper. */
static f4 sub_f4(f4 a, f4 b) { return a - b; }
static i2 mul_i2(i2 v, int k) { return v * k; }
static float many_mine(f4 a, f4 b, f4 c, f4 d, f4 e, f4 f, f4 g, f4 h, f4 i, d2 j) {
    f4 t = a + b + c + d + e + f + g + h + i;
    return t[0] + t[1] * 2 + t[2] * 3 + t[3] * 4 + (float)(j[0] * 100 + j[1] * 1000);
}

/* Defined here and called here, through a pointer the optimizer can't see
 * through, so both sides use this compiler's convention. */
__attribute__((noinline)) static d2 local_d2(d2 a, f2 b, i4 c) {
    return a * (double)b[1] + (double)c[2];
}

static void pf4(const char *name, f4 v) { printf("%-8s %g %g %g %g\n", name, v[0], v[1], v[2], v[3]); }
static void pd2(const char *name, d2 v) { printf("%-8s %g %g\n", name, v[0], v[1]); }

int main(int argc, char **argv) {
    (void)argv;
    int k = argc + 1;
    f4 a = { 1.5f, -2.0f, 3.25f, 4.0f };
    f4 b = { 0.5f, 0.25f, -1.0f, 8.0f };

    pf4("scale", h_scale_f4(a, (float)k));
    pd2("add_d2", h_add_d2((d2){ 1.5, -2.5 }, (d2){ 0.25, 10.0 }));
    i4 m = h_mix_i4(k, (i4){ 1, 2, 3, 4 }, (i4){ 10, 20, 30, 40 });
    printf("mix_i4   %d %d %d %d\n", m[0], m[1], m[2], m[3]);
    u16b x = h_xor_u16b((u16b){ 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 },
                        (u16b){ 255, 0, 255, 0, 15, 15, 15, 15, 240, 240, 240, 240, 1, 2, 4, 8 });
    printf("xor_u16b");
    for (int i = 0; i < 16; i++)
        printf(" %u", x[i]);
    printf("\n");
    f2 n = h_neg_f2((f2){ 1.25f, -3.5f });
    printf("neg_f2   %g %g\n", n[0], n[1]);
    i2 s = h_swap_i2((i2){ 7, -9 });
    printf("swap_i2  %d %d\n", s[0], s[1]);
    u4h sh = h_shift_u4h((u4h){ 1, 3, 0x8001, 0x00ff }, k);
    printf("shift    %u %u %u %u\n", sh[0], sh[1], sh[2], sh[3]);

    printf("mixed    %g\n", h_mixed(0.5, a, (struct hfa2){ 1.0, 2.0 }, 3.0f, (d2){ 4.0, 5.0 }, k, (i2){ 6, 7 }));
    printf("many     %g\n", h_many(a, b, a * 2, b * 2, a + b, a - b, a * b, -a, -b, a * 3));

    pf4("apply", h_apply_f4(sub_f4, a, b));
    i2 ai = h_apply_i2(mul_i2, (i2){ 4, -5 });
    printf("apply_i2 %d %d\n", ai[0], ai[1]);
    printf("apply_m  %g\n", h_apply_many(many_mine));

    d2 (*volatile lp)(d2, f2, i4) = local_d2;
    pd2("local", lp((d2){ 1.0, 2.0 }, (f2){ 0, 3 }, (i4){ 0, 0, k, 0 }));
    return 0;
}
