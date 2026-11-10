/* - __builtin_signbit reads a value's bits by storing it and loading an
 *   integer; mem2reg made that a Copy, which converts the value, so the
 *   sign of a constant -0.0 was lost (0 instead of 1);
 * - a statement expression's type was the type its last expression has in
 *   the enclosing scope: in `float n = (... ({ double n = 1e308; n; }) ...)`
 *   the inner n was taken for the outer float;
 * - __typeof__ of a builtin call (__builtin_fabs(y)) was unresolved and
 *   made an int, and sizeof(__builtin_inff()) was 8. */
#include <stdio.h>
#include <string.h>

static unsigned fb(float f) { unsigned b; memcpy(&b, &f, 4); return b; }
static unsigned long long db(double d) { unsigned long long b; memcpy(&b, &d, 8); return b; }

__attribute__((noinline)) int s1(void) { return __builtin_signbit(-0.0); }
__attribute__((noinline)) int s2(void) { double n = -0.0; return __builtin_signbit(n); }
__attribute__((noinline)) int s3(void) { return __builtin_signbit(({ double n = -0.0; n; })); }
__attribute__((noinline)) int s4(void) { return __builtin_signbit((double)-0.0f); }
__attribute__((noinline)) int s5(void) { return __builtin_signbit(- ((double)0)); }
__attribute__((noinline)) int s6(void) { return __builtin_signbitf(-0.0f); }
__attribute__((noinline)) int s7(void) { return __builtin_signbit(__builtin_inf()); }
__attribute__((noinline)) int s8(double z) { return __builtin_signbit(z != z ? 1 : z); }

__attribute__((noinline)) double shadow(float u)
{
    return (double)(__builtin_copysignf(u - u,
        ({ float n_ = (__builtin_signbit(({ double n_ = 1e308; n_ != n_ ? 1 : n_; })) ? -0.0f : u); n_ != n_ ? 1 : n_; }))
        / __builtin_sqrtf(0.5f));
}

__attribute__((noinline)) float typeof_builtin(double y)
{
    __typeof__(__builtin_fabs(y)) a = 0.5;
    __typeof__(__builtin_inff()) b = 1.5f;
    return (float)a + b + (float)sizeof(__typeof__(__builtin_fminf(1.0f, 2.0f)));
}

int main(void)
{
    printf("%d %d %d %d %d %d %d %d\n", s1(), s2(), s3(), s4(), s5(), s6(), s7(), s8(-0.0));
    printf("%llx %llx\n", db(shadow(0.0f)), db(shadow(-3.0f)));
    printf("%x %zu %zu %zu\n", fb(typeof_builtin(2.0)), sizeof(__builtin_inff()), sizeof(__builtin_inf()),
           sizeof(__typeof__(__builtin_fabs(1.0))));
    return 0;
}
