/* Range checks, x >= L && x <= H and x < L || x > H, that become one
 * unsigned compare of x - L: on chars, ints and longs, signed and unsigned,
 * with strict and non-strict bounds, bounds at the ends of the type, ranges
 * of negative numbers, single points, empty ranges (not folded), mixed
 * signedness (not folded), and the non-short-circuit & and | forms. */
#include <limits.h>
#include <stdio.h>

__attribute__((noinline)) static int lower(const char *s, char *out) {
    int n = 0;
    for (; *s; s++) {
        char c = *s;
        if (c >= 'A' && c <= 'Z')
            c = (char)(c - 'A' + 'a');
        if (c >= 'a' && c <= 'z')
            n++;
        *out++ = c;
    }
    *out = 0;
    return n;
}

__attribute__((noinline)) static unsigned classes(int x) {
    unsigned m = 0;
    m |= (unsigned)(x >= -10 && x <= -3) << 0;
    m |= (unsigned)(x > -2 && x < 2) << 1;
    m |= (unsigned)(x < 100 || x > 200) << 2;
    m |= (unsigned)(x <= 5 || x >= 7) << 3;
    m |= (unsigned)(x >= INT_MIN && x <= -1) << 4;
    m |= (unsigned)(x >= 0 && x <= INT_MAX) << 5;
    m |= (unsigned)(x >= 9 && x <= 9) << 6;
    m |= (unsigned)(x >= 9 && x <= 8) << 7;      /* empty */
    m |= (unsigned)((x >= 48) & (x <= 57)) << 8;
    m |= (unsigned)((x < 48) | (x > 57)) << 9;
    m |= (unsigned)(x > INT_MIN && x < INT_MAX) << 10;
    m |= (unsigned)(x <= 57 && x >= 48) << 11;   /* bounds swapped */
    return m;
}

__attribute__((noinline)) static unsigned uclasses(unsigned x) {
    unsigned m = 0;
    m |= (unsigned)(x >= 10 && x <= 20) << 0;
    m |= (unsigned)(x >= 0x80000000u && x <= 0xfffffff0u) << 1;
    m |= (unsigned)(x < 5 || x > 4000000000u) << 2;
    m |= (unsigned)(x > 0 && x < UINT_MAX) << 3;
    m |= (unsigned)((int)x >= 10 && x <= 20) << 4; /* mixed signedness */
    return m;
}

__attribute__((noinline)) static unsigned lclasses(long x, unsigned long u) {
    unsigned m = 0;
    m |= (unsigned)(x >= -5000000000L && x <= 5000000000L) << 0;
    m |= (unsigned)(x < LONG_MIN + 3 || x > LONG_MAX - 3) << 1;
    m |= (unsigned)(u >= 0x8000000000000000ul && u <= 0x8000000000000010ul) << 2;
    m |= (unsigned)(u < 3 || u > 0xfffffffffffffff0ul) << 3;
    m |= (unsigned)(x >= -1 && x <= 1) << 4;
    return m;
}

__attribute__((noinline)) static int digits(const unsigned char *s) {
    int v = 0;
    for (; *s >= '0' && *s <= '9'; s++)
        v = v * 10 + (*s - '0');
    return v;
}

int main(void) {
    char buf[64];
    int n = lower("Hello, World! ABC xyz [@`{]", buf);
    printf("lower: %d '%s'\n", n, buf);

    static const int ints[] = {
        INT_MIN, INT_MIN + 1, -11, -10, -3, -2, -1, 0, 1, 2, 5, 6, 7, 8, 9, 10,
        47, 48, 57, 58, 99, 100, 150, 200, 201, INT_MAX - 1, INT_MAX,
    };
    for (unsigned i = 0; i < sizeof ints / sizeof ints[0]; i++)
        printf("classes(%d) = %03x\n", ints[i], classes(ints[i]));

    static const unsigned uints[] = {
        0, 1, 4, 5, 9, 10, 20, 21, 0x7fffffffu, 0x80000000u, 0xfffffff0u,
        0xfffffff1u, 4000000000u, 4000000001u, UINT_MAX - 1, UINT_MAX,
    };
    for (unsigned i = 0; i < sizeof uints / sizeof uints[0]; i++)
        printf("uclasses(%u) = %02x\n", uints[i], uclasses(uints[i]));

    static const long longs[] = {
        LONG_MIN, LONG_MIN + 2, LONG_MIN + 3, -5000000001L, -5000000000L, -2, -1, 0, 1, 2,
        5000000000L, 5000000001L, LONG_MAX - 3, LONG_MAX - 2, LONG_MAX,
    };
    static const unsigned long ulongs[] = {
        0, 2, 3, 0x7ffffffffffffffful, 0x8000000000000000ul, 0x8000000000000010ul,
        0x8000000000000011ul, 0xfffffffffffffff0ul, 0xfffffffffffffff1ul, ~0ul,
    };
    for (unsigned i = 0; i < sizeof longs / sizeof longs[0]; i++)
        printf("lclasses(%ld) = %02x\n", longs[i], lclasses(longs[i], 0));
    for (unsigned i = 0; i < sizeof ulongs / sizeof ulongs[0]; i++)
        printf("lclasses(u %lu) = %02x\n", ulongs[i], lclasses(0, ulongs[i]));

    printf("digits: %d %d %d\n", digits((const unsigned char *)"12345x"),
           digits((const unsigned char *)"/9"), digits((const unsigned char *)"907:1"));
    return 0;
}
