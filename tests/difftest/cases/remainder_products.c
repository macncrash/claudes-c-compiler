/* Divisions of values bounded only by an unsigned remainder: `(r % n) *
 * (r % n) / n`, a skewed table index (computed in 64 bits once widened),
 * remainders of remainders, and the same over the full range of r,
 * including values whose remainder is the divisor less one. */
#include <stdio.h>

static const unsigned edge[] = {0, 1, 35, 36, 37, 1295, 1296, 65535, 65536, 2147483647u, 2147483648u, 4294967294u, 4294967295u};

__attribute__((noinline)) static unsigned skew(unsigned r, unsigned n) {
    return (r % n) * (r % n) / n;
}

__attribute__((noinline)) static unsigned long skew36(unsigned r) {
    unsigned long m = r % 36;
    return m * m / 36 + m * m % 7;
}

__attribute__((noinline)) static unsigned nested(unsigned long x) {
    unsigned a = (unsigned)(x % 1000);
    unsigned b = a % 37;
    return a / 9 + b * b / 5 + (unsigned)(x % 4294967291ul) / 1000003;
}

__attribute__((noinline)) static long signed_mix(int x) {
    unsigned u = (unsigned)x % 100;
    return (long)(u * u) / 7 - (long)(u / 3);
}

int main(void) {
    unsigned long h = 0;
    for (unsigned i = 0; i < sizeof edge / sizeof edge[0]; i++) {
        unsigned r = edge[i];
        h = h * 31 + skew(r, 36) + skew(r, 7) * 3 + skew36(r) * 5 + nested(r) + nested((unsigned long)r << 20) * 11
            + (unsigned long)signed_mix((int)r);
    }
    printf("edges %lu\n", h);
    unsigned x = 0x9e3779b9u;
    for (int i = 0; i < 200000; i++) {
        x ^= x << 13, x ^= x >> 17, x ^= x << 5;
        h = h * 131 + skew(x, 36) + skew36(x) + nested(x) + nested((unsigned long)x * 2654435761u)
            + (unsigned long)signed_mix((int)x);
    }
    printf("random %lu\n", h);
    return 0;
}
