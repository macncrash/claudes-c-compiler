/* vdbeSorterCompareInt's shape: two big-endian integer records of the
 * same type compared byte by byte, a differing sign bit settling the
 * order (`res = (*v1 & 0x80) ? -1 : +1`), and the result negated for a
 * descending sort (`if (desc) res = -res`), the negation becoming a
 * conditional negate and the sign test a single test at its compare. */
#include <stdio.h>

static const unsigned char len_of[] = { 0, 1, 2, 3, 4, 6, 8 };

static int __attribute__((noinline)) cmp_int(const unsigned char *p1, const unsigned char *p2, int type, int desc) {
    int res = 0;
    int n = len_of[type];
    for (int i = 0; i < n; i++) {
        res = p1[i] - p2[i];
        if (res)
            break;
    }
    if (res && ((p1[0] ^ p2[0]) & 0x80))
        res = (p1[0] & 0x80) ? -1 : +1;
    if (desc)
        res = -res;
    return res;
}

static void put(unsigned char *p, long v, int n) {
    for (int i = n - 1; i >= 0; i--) {
        p[i] = (unsigned char)(v & 0xff);
        v >>= 8;
    }
}

int main(void) {
    long vals[] = { 0, 1, -1, 127, -128, 255, -256, 32767, -32768, 1000000, -1000000, 2147483647L, -2147483647L - 1 };
    int nv = (int)(sizeof vals / sizeof vals[0]);
    unsigned long h = 0;
    for (int type = 1; type <= 6; type++)
        for (int a = 0; a < nv; a++)
            for (int b = 0; b < nv; b++) {
                unsigned char p1[8], p2[8];
                int n = len_of[type];
                put(p1, vals[a], n);
                put(p2, vals[b], n);
                int asc = cmp_int(p1, p2, type, 0);
                int dsc = cmp_int(p1, p2, type, 1);
                int sa = (asc > 0) - (asc < 0), sd = (dsc > 0) - (dsc < 0);
                h = h * 3 + (unsigned long)(sa + 1);
                if (sd != -sa)
                    printf("mismatch %d %d %d\n", type, a, b);
            }
    printf("hash %lu\n", h);
    return 0;
}
