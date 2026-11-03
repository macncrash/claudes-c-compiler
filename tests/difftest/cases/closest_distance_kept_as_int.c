/* The closest of a list of big-endian page numbers to a target, in the
 * shape of SQLite's allocateBtreePage: each distance an int made from an
 * unsigned difference, its absolute value (INT_MIN saturated) compared
 * against the best so far and kept with a select. The distances only ever
 * reach compares and selects, so their bits above 32 must not matter. */
#include <stdio.h>

typedef unsigned char u8;
typedef unsigned int u32;

static u32 get4(const u8 *p) { return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3]; }

static int abs32(int x)
{
    if (x >= 0)
        return x;
    if (x == (int)0x80000000)
        return 0x7fffffff;
    return -x;
}

static int __attribute__((noinline)) seven(void) { return 7; }

static u32 __attribute__((noinline)) closest(const u8 *a, u32 k, u32 nearby, int *best)
{
    u32 i, c = 0;
    int dist = abs32((int)(get4(a) - nearby));
    for (i = 1; i < k; i++) {
        int d2 = abs32((int)(get4(&a[i * 4]) - nearby));
        if (d2 < dist) {
            c = i;
            dist = d2;
        }
    }
    *best = dist;
    return c;
}

/* A call's int result compared and kept as the smaller. */
static int __attribute__((noinline)) min_call(int m)
{
    int r = seven() - m;
    return r < m ? r : m;
}

int main(void)
{
    static const u32 pages[][6] = {
        {10, 20, 30, 40, 50, 60},
        {0x80000000u, 0x7fffffffu, 0xffffffffu, 0, 1, 2},
        {100, 0x80000064u, 99, 101, 0xffffff9cu, 5},
        {0x7fffffffu, 0x80000000u, 0x80000001u, 0x7ffffffeu, 3, 0xfffffffeu},
    };
    static const u32 targets[] = {0, 1, 35, 100, 0x80000000u, 0x7fffffffu, 0xffffffffu, 0x80000064u};
    u8 buf[24];
    unsigned long h = 0;
    for (unsigned r = 0; r < sizeof pages / sizeof pages[0]; r++) {
        for (int j = 0; j < 6; j++) {
            buf[4 * j] = (u8)(pages[r][j] >> 24);
            buf[4 * j + 1] = (u8)(pages[r][j] >> 16);
            buf[4 * j + 2] = (u8)(pages[r][j] >> 8);
            buf[4 * j + 3] = (u8)pages[r][j];
        }
        for (unsigned t = 0; t < sizeof targets / sizeof targets[0]; t++) {
            for (u32 k = 1; k <= 6; k++) {
                int best;
                u32 c = closest(buf, k, targets[t], &best);
                h = h * 31 + c;
                h = h * 31 + (u32)best;
            }
        }
    }
    for (int m = -3; m < 12; m++)
        h = h * 31 + (unsigned)min_call(m);
    printf("%lu\n", h);
    return 0;
}
