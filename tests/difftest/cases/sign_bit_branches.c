/* Branches on the sign of an int or long: sqlite3BtreeIndexMoveto's
 * `if (c < 0) lo = idx + 1; else if (c > 0) hi = idx - 1;` binary search
 * over a comparison result, a count tested against zero after a call, and
 * signs read again by a later comparison or select. */
#include <stdio.h>

static int __attribute__((noinline)) compare(long a, long b) {
    return a < b ? -1 : a > b;
}

static int __attribute__((noinline)) search(const long *v, int n, long key, int *steps) {
    int lo = 0, hi = n - 1;
    *steps = 0;
    while (lo <= hi) {
        int idx = (lo + hi) >> 1;
        int c = compare(v[idx], key);
        ++*steps;
        if (c < 0)
            lo = idx + 1;
        else if (c > 0)
            hi = idx - 1;
        else
            return idx;
    }
    return -1 - lo;
}

static long __attribute__((noinline)) classify(long x) {
    long r = 0;
    if (x >= 0)
        r += 1;
    if ((int)x < 0)
        r += 10;
    if (x < 0)
        r += 100;
    else if (x == 0)
        r += 1000;
    return r + (x < 0 ? -x : x / 3);
}

static int __attribute__((noinline)) count_negative(const int *v, int n) {
    int c = 0;
    for (int i = 0; i < n; i++)
        if (v[i] < 0)
            c += i;
        else
            c -= 1;
    return c;
}

int main(void) {
    long v[9] = { -1000000000000L, -50, -3, 0, 2, 7, 900, 4096, 1L << 40 };
    long keys[] = { -1000000000000L, -51, -3, -2, 0, 3, 7, 4096, 5000, 1L << 40, (1L << 40) + 1 };
    for (int k = 0; k < 11; k++) {
        int steps;
        int r = search(v, 9, keys[k], &steps);
        printf("search %ld -> %d in %d\n", keys[k], r, steps);
    }
    long xs[] = { 0, 1, -1, 2147483647L, -2147483648L, 2147483648L, -9223372036854775807L - 1, 9223372036854775807L, 4294967295L };
    for (int k = 0; k < 9; k++)
        printf("classify %ld %ld\n", xs[k], k == 6 ? 0L : classify(xs[k]));
    int w[] = { 5, -1, 0, -2147483647 - 1, 2147483647, -7, 3, -100 };
    printf("negatives %d %d\n", count_negative(w, 8), count_negative(w + 3, 2));
    return 0;
}
