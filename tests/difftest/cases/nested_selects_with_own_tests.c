/* A select whose arms are themselves selects on other tests, computed
 * between the outer compare and its use: sqlite3VdbeExec's
 * `n = a > b ? (c < 0 ? 0 : c) : d` shape, and clamps that pick between
 * two clamped values. */
#include <stdio.h>

static long __attribute__((noinline)) pick(int a, int b, long c, long d) {
    long n = a > b ? (c < 0 ? 0 : c) : d;
    return n;
}

static int __attribute__((noinline)) clamp2(int x, int lo, int hi, int alt) {
    int big = x > hi;
    int y = alt < lo ? lo : alt;
    return big ? y : x;
}

static unsigned long __attribute__((noinline)) mix(unsigned long u, long s, unsigned long v) {
    unsigned long r = u >= v ? (s < 0 ? (unsigned long)-s : (unsigned long)s) : v - u;
    return r * 3 + (s > 0);
}

int main(void) {
    int as[] = { -5, 0, 7, 2147483647 };
    long cs[] = { -3, 0, 9, -9223372036854775807L };
    unsigned long sum = 0;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            for (int k = 0; k < 4; k++) {
                long r = pick(as[i], as[j], cs[k], 1000 + k);
                sum = sum * 31 + (unsigned long)r;
                printf("%ld ", r);
            }
    printf("\nsum %lu\n", sum);
    for (int x = -3; x <= 12; x += 3)
        printf("clamp %d %d %d\n", x, clamp2(x, 0, 6, x - 5), clamp2(x, -1, 2, 99));
    unsigned long us[] = { 0, 5, 18446744073709551615UL };
    long ss[] = { -4, 0, 4 };
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            printf("mix %lu\n", mix(us[i], ss[j], 5));
    return 0;
}
