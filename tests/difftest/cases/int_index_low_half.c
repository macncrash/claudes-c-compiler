/* Signed int values kept sign-extended in a register and read whole across
 * a loop's back edge: as an array index (negative ones too), compared with
 * a long, multiplied into a long. The reads may take the low half with a
 * sign extension instead, and the sxtw after each 32-bit update then goes.
 * With values that cross zero and reach INT_MIN/INT_MAX. */
#include <limits.h>
#include <stdio.h>

static long table[64];

__attribute__((noinline)) static int fill(long *out, const unsigned short *src, int n, int start) {
    int j = start;
    for (const unsigned short *p = src; p < src + n; p++) {
        out[j] = *p * 3;
        j++;
    }
    return j;
}

__attribute__((noinline)) static long walk(const long *mid, int from, int to) {
    long sum = 0;
    for (int i = from; i != to; i++) {
        sum += mid[i] * (i + 1);
    }
    return sum;
}

__attribute__((noinline)) static long count_below(long limit, int start, int step, int n) {
    long hits = 0;
    int v = start;
    for (int k = 0; k < n; k++) {
        if (limit < v) {
            hits++;
        }
        v += step;
    }
    return hits * 1000 + v;
}

__attribute__((noinline)) static long dot(const int *a, int n, int scale) {
    long acc = 0;
    for (int i = 0; i < n; i++) {
        int x = a[i] - 5;
        acc += (long)x * scale;
    }
    return acc;
}

int main(void) {
    unsigned short src[20];
    for (int i = 0; i < 20; i++) {
        src[i] = (unsigned short)(i * 977 + 13);
    }
    int end = fill(table, src, 20, 7);
    printf("%d %ld %ld\n", end, table[7], table[26]);
    for (int i = 0; i < 64; i++) {
        table[i] = i * 7 - 100;
    }
    printf("%ld %ld\n", walk(table + 32, -20, 20), walk(table + 32, -32, 31));
    printf("%ld %ld\n", count_below(-3, -10, 1, 20), count_below(5, INT_MAX - 3, 1, 3));
    printf("%ld\n", count_below(0, INT_MIN + 2, -1, 1));
    int vals[9] = { 0, 5, -7, INT_MAX, INT_MIN + 5, 12, -1, 100000, -100000 };
    printf("%ld %ld\n", dot(vals, 9, 3), dot(vals, 9, -INT_MAX));
    return 0;
}
