/* Arguments of math builtins are converted as for a call of the library
 * function. ccc passed them as they were: a double given to
 * __builtin_fabsf went as its bits read as a float, a float given to
 * __builtin_fabs likewise, and __builtin_printf without <stdio.h> passed a
 * float without promoting it to double. Builtins it did not know
 * (__builtin_fminf, __builtin_truncf, __builtin_ldexpf, __builtin_copysignl)
 * were called as undeclared functions, which no library defines, and
 * __builtin_fabsl returned a 128-bit long double on Apple arm64, where
 * long double is a double. */
#include <string.h>

int printf(const char *, ...);

#define T(name, expr) \
    __attribute__((noinline)) double name(double x, float u, long k) { return expr; }
T(b1, __builtin_copysignf(2.0f, x))
T(b2, __builtin_copysignf(x, u))
T(b3, __builtin_fabsf(x))
T(b4, __builtin_sqrtf(x))
T(b5, __builtin_copysign(u, u))
T(b6, __builtin_fabs(u))
T(b7, __builtin_sqrt(u))
T(b8, __builtin_fminf(x, u))
T(b9, __builtin_fmaxf(u, x))
T(b10, __builtin_fabsf(k))
T(b11, __builtin_copysign(k, x))
T(b12, __builtin_fabs(k))
T(b13, __builtin_floorf(x))
T(b14, __builtin_truncf(x))
T(b15, __builtin_fmin(u, k))
T(b16, __builtin_ldexpf(x, k & 7))
T(b17, __builtin_copysignl(u, x))
T(b18, __builtin_fabsl(u))
T(b19, __builtin_hypotf(x, k))
T(b20, __builtin_cbrt(u))

typedef double (*fn)(double, float, long);
static fn fns[] = { b1, b2, b3, b4, b5, b6, b7, b8, b9, b10, b11, b12, b13, b14, b15, b16, b17, b18, b19, b20 };

int main(void)
{
    double xs[] = { -2.75, 1e30, 0.1, -1e-30, 9.0 };
    float us[] = { -1.0f, 4.0f, -0.5f, 2.0f, 3.0f };
    long ks[] = { -3, 2, 17, -100000, 9 };
    for (unsigned f = 0; f < sizeof fns / sizeof fns[0]; f++)
        for (unsigned i = 0; i < 5; i++) {
            double r = fns[f](xs[i], us[i], ks[i]);
            printf("b%u %u -> %a\n", f + 1, i, r);
        }
    float pf = 1.5f;
    short ps = -3;
    unsigned char pc = 200;
    __builtin_printf("%f %d %d\n", pf, ps, pc);
    return 0;
}
