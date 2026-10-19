/* Floating point: conversions, special values, float vs double precision,
 * complex numbers. */
#include <complex.h>
#include <float.h>
#include <math.h>
#include <stdio.h>

static float fadd(float a, float b) { return a + b; }

int main(void) {
    float f = 0.1f; double d = 0.1;
    printf("%.10f %.17f %d\n", f, d, (double)f == d);
    printf("%.9g\n", fadd(1e8f, 1.0f));
    printf("%d %d %lld %u\n", (int)3.99, (int)-3.99, (long long)1e18, (unsigned)4e9);
    printf("%g %g %g %d\n", INFINITY, -INFINITY, 0.0 / 0.0 == 0.0 / 0.0 ? 1.0 : 0.0, isnan(NAN));
    printf("%.3e %.3e\n", DBL_MAX, DBL_MIN);
    double complex z = 1.0 + 2.0 * I;
    double complex w = z * z;
    printf("%.2f %.2f %.4f\n", creal(w), cimag(w), cabs(z));
    float complex fz = 3.0f - 4.0f * I;
    printf("%.2f\n", cabsf(fz));
    printf("%.6f %.6f\n", fma(2.0, 3.0, 1.0), fmod(10.5, 3.0));
    unsigned long long big = 18446744073709551615ULL;
    printf("%.1f\n", (double)big);
    printf("%d\n", (int)(float)16777217);
    return 0;
}
