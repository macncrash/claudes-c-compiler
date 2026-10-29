/* Shifts of sign-extended ints and extensions of values known
 * nonnegative: the midpoint (lo + hi) >> 1 of sqlite3BtreeIndexMoveto's
 * search, with negative bounds too; ints scaled by a shift as 64-bit
 * values; halfword and byte loads, masks and logical shifts used as
 * signed 64-bit values, and their signed counterparts that must keep the
 * extension. */
#include <stdio.h>

static unsigned long __attribute__((noinline)) midpoints(int lo, int hi) {
    unsigned long s = 0;
    while (lo <= hi) {
        long mid = (lo + hi) >> 1;
        s = s * 3 + (unsigned long)mid;
        if (mid & 1)
            hi = (int)mid - 1;
        else
            lo = (int)mid + 1;
    }
    return s;
}

static long __attribute__((noinline)) scaled(int a, int b) {
    long x = (long)(a + b) * 8;
    long y = (long)(a - b) * 2147483648L;
    long z = (long)(a ^ b) >> 5;
    return x + y + z;
}

struct rec { unsigned short u16; short s16; unsigned char u8; signed char s8; int i; unsigned u; };

static long __attribute__((noinline)) extend(const struct rec *r) {
    long a = (int)r->u16;
    long b = (int)r->s16;
    long c = (int)r->u8;
    long d = (int)r->s8;
    long e = (int)(r->i & 0x7fffffff);
    long f = (int)(r->u >> 1);
    long g = (int)(r->u & 0x80000000u);
    long h = (int)(r->u >> 0);
    return ((((((a * 3 + b) * 5 + c) * 7 + d) * 11 + e) * 13 + f) * 17 + g) * 19 + h;
}

int main(void) {
    printf("mid %lu %lu %lu %lu\n", midpoints(0, 100), midpoints(-100, -3), midpoints(-1000000000, 1000000000),
           midpoints(1073741000, 1073741600));
    printf("scaled %ld %ld %ld\n", scaled(5, 7), scaled(-100000, 3), scaled(1000000000, -1000000000));
    struct rec rs[] = {
        { 65535, -1, 255, -1, -1, 0xffffffffu },
        { 32768, -32768, 128, -128, -2147483647 - 1, 0x80000000u },
        { 1, 1, 1, 1, 1, 1 },
    };
    for (int i = 0; i < 3; i++)
        printf("extend %ld\n", extend(&rs[i]));
    return 0;
}
