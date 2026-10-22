/* Floating-point contraction (-ffp-contract=on, clang's default): a * b + c
 * within one expression is computed with a single rounding, in each form
 * (product on either side, subtracted, negated, compound assignment,
 * vectors), while a product kept in a variable, converted to another type,
 * or computed in another statement is rounded first. The inputs make the
 * two differ: (1 + 2^-27)(1 - 2^-27) - 1 is -2^-54 fused and 0 rounded.
 * Results print exactly (%a), so a missing or extra contraction shows. */
#include <stdio.h>

typedef double d2 __attribute__((vector_size(16)));
typedef float f4 __attribute__((vector_size(16)));

static volatile double va = 1.0 + 0x1p-27, vb = 1.0 - 0x1p-27, vone = 1.0;
static volatile float fa = 1.0f + 0x1p-12f, fb = 1.0f - 0x1p-12f;

__attribute__((noinline)) static void scalars(double a, double b, double c) {
    double t = a * b;
    printf("add      %a %a\n", a * b - c, -c + a * b);
    printf("sub      %a %a\n", c - a * b, a * b - c * 1.0);
    printf("neg      %a %a\n", -(a * b) + c, -(a * b) - c);
    printf("stmt     %a\n", t - c);
    printf("both     %a\n", a * b - c * c);
    printf("cond     %a\n", a * b + (c > 0 ? -c : c));
    double x = -c;
    x += a * b;
    double y = c;
    y -= a * b;
    printf("compound %a %a\n", x, y);
    float f = (float)a, g = (float)b;
    printf("convert  %a\n", (double)(f * g) - c);
}

__attribute__((noinline)) static void floats(float a, float b) {
    printf("float    %a %a\n", (double)(a * b - 1.0f), (double)(1.0f - a * b));
}

__attribute__((noinline)) static void vectors(double a, double b) {
    d2 va2 = { a, b }, vb2 = { b, a }, one = { 1.0, 1.0 };
    d2 r1 = va2 * vb2 - one;
    d2 r2 = one - va2 * vb2;
    d2 r3 = va2 * vb2 - one * 3.0 / 3.0;
    d2 r4 = va2 * vb2 + -1.0;
    d2 acc = -one;
    acc += va2 * vb2;
    d2 t = va2 * vb2;
    d2 r5 = t - one;
    printf("vec      %a %a %a %a\n", r1[0], r1[1], r2[0], r2[1]);
    printf("vec      %a %a %a\n", r3[0], r4[1], acc[0]);
    printf("vec stmt %a\n", r5[0]);
    f4 fx = { 1.0f + 0x1p-12f, 3.0f, 0.1f, -2.5f }, fy = { 1.0f - 0x1p-12f, 1.0f / 3.0f, 10.0f, 0.4f };
    f4 fr = fx * fy - 1.0f;
    f4 fs = fx * 2.0f - fy / 7.0f;
    printf("vec f    %a %a %a %a %a\n", (double)fr[0], (double)fr[1], (double)fr[2], (double)fr[3], (double)fs[2]);
}

int main(void) {
    scalars(va, vb, vone);
    floats(fa, fb);
    vectors(va, vb);
    return 0;
}
