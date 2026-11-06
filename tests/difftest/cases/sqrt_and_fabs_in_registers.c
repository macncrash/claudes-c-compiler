/* sqrt and fabs work on floating-point registers. Values go in and out of them
 * from registers, from the stack, from constants and across calls, and the
 * sign of zero and NaNs have to survive. */
#include <math.h>
#include <stdio.h>
#include <string.h>

static unsigned bits(float f)
{
    unsigned u;
    memcpy(&u, &f, sizeof u);
    return u;
}

static unsigned long bits_d(double d)
{
    unsigned long u;
    memcpy(&u, &d, sizeof u);
    return u;
}

static float __attribute__((noinline)) root(float x) { return sqrtf(x); }
static double __attribute__((noinline)) root_d(double x) { return sqrt(x); }
static float __attribute__((noinline)) size(float x) { return fabsf(x); }
static double __attribute__((noinline)) size_d(double x) { return fabs(x); }

static float __attribute__((noinline)) identity(float x) { return x; }

/* Live across a call, which clobbers the caller-saved registers. */
static float __attribute__((noinline)) across_a_call(float a, float b)
{
    float r = sqrtf(a);
    float s = identity(b);
    float t = fabsf(s - r);
    float u = identity(t + 1.0f);
    return r * 100.0f + t * 10.0f + u;
}

/* In a loop, feeding sums. */
static double __attribute__((noinline)) in_a_loop(const float *v, int n)
{
    float s = 0;
    double d = 0;
    for (int i = 0; i < n; i++) {
        s += sqrtf(v[i]);
        d += fabs((double)v[i] - 20.0);
    }
    return s * 1000.0 + d;
}

/* More live values than there are registers, so that some are on the stack. */
static float __attribute__((noinline)) pressure(const float *v)
{
    float a0 = sqrtf(v[0]), a1 = sqrtf(v[1]), a2 = sqrtf(v[2]), a3 = sqrtf(v[3]);
    float a4 = sqrtf(v[4]), a5 = sqrtf(v[5]), a6 = sqrtf(v[6]), a7 = sqrtf(v[7]);
    float b0 = fabsf(a0 - 9), b1 = fabsf(a1 - 9), b2 = fabsf(a2 - 9), b3 = fabsf(a3 - 9);
    float b4 = fabsf(a4 - 9), b5 = fabsf(a5 - 9), b6 = fabsf(a6 - 9), b7 = fabsf(a7 - 9);
    float c0 = a0 + b1, c1 = a1 + b2, c2 = a2 + b3, c3 = a3 + b4;
    float c4 = a4 + b5, c5 = a5 + b6, c6 = a6 + b7, c7 = a7 + b0;
    float d0 = sqrtf(c0 * c0), d1 = sqrtf(c1 * c1), d2 = sqrtf(c2 * c2), d3 = sqrtf(c3 * c3);
    float d4 = sqrtf(c4 * c4), d5 = sqrtf(c5 * c5), d6 = sqrtf(c6 * c6), d7 = sqrtf(c7 * c7);
    float e0 = identity(d0 + a7);
    return ((a0 + a1 + a2 + a3 + a4 + a5 + a6 + a7) * 1000.0f + (b0 + b1 + b2 + b3 + b4 + b5 + b6 + b7) * 100.0f
          + (c0 + c1 + c2 + c3 + c4 + c5 + c6 + c7) * 10.0f + d0 + d1 + d2 + d3 + d4 + d5 + d6 + d7) * 2.0f + e0;
}

/* A result nobody reads, and a float and a double side by side. */
static double __attribute__((noinline)) mixed(float f, double d)
{
    (void)sqrtf(f);
    float r = sqrtf(f);
    double q = sqrt(d);
    return (double)r * 100.0 + q + (double)fabsf(-r) + fabs(-q);
}

/* As conditions and through a select. */
static int __attribute__((noinline)) compared(float a, float b)
{
    return (sqrtf(a) > fabsf(b) ? 1 : 2) + (fabsf(a) == fabsf(b)) * 4;
}

int main(void)
{
    float v[8] = { 4, 9, 16, 25, 36, 49, 64, 81 };
    float zero = 0.0f, minus_zero = -0.0f, nan_f = NAN, inf_f = INFINITY;
    double zero_d = 0.0, minus_zero_d = -0.0, inf_d = INFINITY;

    printf("%g %g %g %g\n", root(2.25f), root_d(6.25), size(-3.5f), size_d(-3.5));
    printf("%x %x %lx %lx\n", bits(root(minus_zero)), bits(size(minus_zero)), bits_d(root_d(minus_zero_d)), bits_d(size_d(minus_zero_d)));
    printf("%x %x %x\n", bits(root(zero)), bits(root(inf_f)), bits(size(-inf_f)));
    printf("%d %d %d\n", isnan(root(-1.0f)), isnan(root_d(-1.0)), isnan(size(nan_f)));
    printf("%lx %lx\n", bits_d(root_d(zero_d)), bits_d(size_d(-inf_d)));
    printf("%g %g\n", sqrtf(2.25f), sqrt(6.25));
    printf("%g\n", across_a_call(16.0f, 9.0f));
    printf("%g\n", in_a_loop(v, 8));
    printf("%g\n", pressure(v));
    printf("%g\n", mixed(16.0f, 25.0));
    printf("%d %d %d\n", compared(16.0f, 3.0f), compared(16.0f, -16.0f), compared(4.0f, -9.0f));
    return 0;
}
