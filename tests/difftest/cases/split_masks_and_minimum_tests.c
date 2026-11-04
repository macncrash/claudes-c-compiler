/* Constants no single AArch64 logical immediate holds but two do (two
 * runs of ones: SQLite's varint `x ^= 0x204000`), applied with xor, or
 * and and at 32 and 64 bits, signed and unsigned; and equality tests
 * against INT_MIN and INT64_MIN (sqlite3AbsInt32's saturation), which may
 * become tests of the overflow of x - 1. */
#include <stdio.h>
#include <limits.h>

static unsigned long long __attribute__((noinline)) masks64(unsigned long long x)
{
    unsigned long long a = x ^ 0x204000ULL;
    unsigned long long b = x | 0xf0000000fULL;
    unsigned long long c = x & ~0x30000c0ULL;
    return a * 3 + b * 5 + c * 7;
}

static unsigned __attribute__((noinline)) masks32(int x, unsigned u)
{
    int a = x ^ 0x204000;
    int b = x | 0x7000f;
    int c = x & ~0x81000;
    unsigned d = u ^ 0x80000001u;
    unsigned e = u & ~0x300c0u;
    /* (Signed results widened, then summed unsigned.) */
    long long wide = (long long)a + b + c;
    return (unsigned)a + (unsigned)b * 3 + (unsigned)c * 5 + (d >> 1) + (e & 0xffff) + (unsigned)(wide >> 7);
}

static int __attribute__((noinline)) abs32(int x)
{
    if (x >= 0)
        return x;
    if (x == INT_MIN)
        return INT_MAX;
    return -x;
}

static long long __attribute__((noinline)) abs64(long long x)
{
    if (x >= 0)
        return x;
    if (x != LLONG_MIN)
        return -x;
    return LLONG_MAX;
}

int main(void)
{
    static const long long xs[] = {0, 1, -1, 0x204000, 0x7fffffff, INT_MIN, (long long)INT_MIN + 1,
                                   LLONG_MIN, LLONG_MIN + 1, LLONG_MAX, 0x123456789abcdefLL, -0x204001};
    unsigned long h = 0;
    for (unsigned i = 0; i < sizeof xs / sizeof xs[0]; i++) {
        long long x = xs[i];
        h = h * 31 + masks64((unsigned long long)x);
        h = h * 31 + masks32((int)x, (unsigned)x);
        h = h * 31 + (unsigned)abs32((int)x);
        h = h * 31 + (unsigned long)abs64(x);
    }
    printf("%lu\n", h);
    return 0;
}
