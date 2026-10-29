/* Masked zero tests read by selects rather than branches, and selects of
 * 1 or -1: SQLite's vdbeSorterCompareInt orders two big-endian integers by
 * their first differing byte unless the sign bits differ. Also tests whose
 * select reads the sign or overflow flags, a mask in the upper half tested
 * at 32 bits, and a carry select that must keep its compare. */
#include <stdio.h>

static int __attribute__((noinline)) compare_be(const unsigned char *a, const unsigned char *b, int n) {
    int res = 0;
    for (int i = 0; i < n; i++) {
        if ((res = a[i] - b[i])) {
            if ((a[0] ^ b[0]) & 0x80)
                res = a[0] & 0x80 ? -1 : +1;
            break;
        }
    }
    return res;
}

static long __attribute__((noinline)) pick(unsigned long x, long a, long b) {
    long r = (x & 0x40) ? a : b;
    r += (x & 0x8000000000000000ul) ? 1 : -1;
    r += (int)(x & 0x7fffffff) < 0 ? 100 : 7;
    r += (unsigned)(x & 0xf0000000u) != 0;
    return r;
}

static int __attribute__((noinline)) sign_of(long v, unsigned mask) {
    return (int)((unsigned long)v & mask) ? (v < 0 ? -1 : 1) : 0;
}

static unsigned __attribute__((noinline)) carry_pick(unsigned x, unsigned y) {
    unsigned m = x & 0xff00;
    return m > y ? m : y + 1;
}

int main(void) {
    unsigned char k[][4] = {
        { 0x01, 0x02, 0x03, 0x04 }, { 0x01, 0x02, 0x03, 0x05 }, { 0x81, 0x00, 0x00, 0x00 },
        { 0x7f, 0xff, 0xff, 0xff }, { 0xff, 0xff, 0xff, 0xfe }, { 0x01, 0x02, 0x03, 0x04 },
    };
    unsigned long h = 0;
    for (int i = 0; i < 6; i++)
        for (int j = 0; j < 6; j++)
            h = h * 31 + (unsigned)compare_be(k[i], k[j], 4) + (unsigned)compare_be(k[i], k[j], 1 + (i + j) % 4);
    printf("compare %lu\n", h);

    unsigned long xs[] = { 0, 0x40, 0x80000040, 0x8000000000000000ul, 0xfffffffffffffffful, 0x10000000, 0x123456789abcdeful };
    long s = 0;
    for (int i = 0; i < 7; i++)
        s = s * 3 + pick(xs[i], i * 11, -i);
    printf("pick %ld\n", s);
    printf("sign %d %d %d %d\n", sign_of(-5, 0xff), sign_of(256, 0xff), sign_of(257, 0xff), sign_of(-256, 0xff));
    printf("carry %u %u %u\n", carry_pick(0x1234, 0x1000), carry_pick(0x1234, 0x2000), carry_pick(0xff, 0));
    return 0;
}
