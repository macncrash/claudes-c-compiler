/* Values clamped at zero (`if( n<0 ) n = 0` in sqlite3VdbeExec), at 32
 * and 64 bits, from above and below, and used at 64 bits afterwards: a
 * clamp may become a bit clear by the sign, which must leave the same
 * number as the select it replaces. */
#include <stdio.h>
#include <limits.h>

static long __attribute__((noinline)) max0_int(int n, long base)
{
    if (n < 0)
        n = 0;
    return base + n;
}

static long long __attribute__((noinline)) max0_long(long long n)
{
    return n < 0 ? 0 : n;
}

static long long __attribute__((noinline)) min0_long(long long n)
{
    return n < 0 ? n : 0;
}

static long __attribute__((noinline)) min0_int(int n, long base)
{
    int m = n < 0 ? n : 0;
    return base + m;
}

int main(void)
{
    static const long long xs[] = {0, 1, -1, 7, -7, INT_MAX, INT_MIN, (long long)INT_MAX + 1, (long long)INT_MIN - 1,
                                   LLONG_MAX, LLONG_MIN, 0x100000000LL, -0x100000000LL};
    unsigned long h = 0;
    for (unsigned i = 0; i < sizeof xs / sizeof xs[0]; i++) {
        h = h * 31 + (unsigned long)max0_int((int)xs[i], 1000);
        h = h * 31 + (unsigned long)max0_long(xs[i]);
        h = h * 31 + (unsigned long)min0_long(xs[i]);
        h = h * 31 + (unsigned long)min0_int((int)xs[i], -1000);
    }
    printf("%lu\n", h);
    return 0;
}
