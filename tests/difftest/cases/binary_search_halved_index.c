/* A binary search in the shape of sqlite3BtreeIndexMoveto: the int midpoint
 * (lwr + upr) >> 1, set before the loop and again at its end, widened as an
 * index into a table of 16-bit big-endian offsets. The bounds run negative
 * too, around the middle of the table, so the halved sum is negative. */
#include <stdio.h>

static unsigned short tab[2001];

static int __attribute__((noinline)) search(const unsigned short *mid, int lwr, int upr, unsigned key)
{
    int idx = (lwr + upr) >> 1;
    int steps = 0;
    for (;;) {
        unsigned short raw = mid[idx];
        unsigned v = (unsigned short)((raw >> 8) | (raw << 8));
        steps++;
        if (v < key)
            lwr = idx + 1;
        else if (v > key)
            upr = idx - 1;
        else
            return idx * 64 + steps;
        if (lwr > upr)
            return -(lwr * 64 + steps);
        idx = (lwr + upr) >> 1;
    }
}

int main(void)
{
    for (int i = 0; i < 2001; i++) {
        unsigned v = (unsigned)i * 7 + 3;
        tab[i] = (unsigned short)((v >> 8) | (v << 8));
    }
    unsigned long h = 0;
    for (unsigned key = 0; key < 14100; key += 13) {
        h = h * 33 + (unsigned long)search(tab + 1000, -1000, 1000, key);
        h ^= (unsigned long)search(tab, 0, 2000, key) + (h >> 17);
    }
    printf("%lu\n", h);
    printf("%d %d %d\n", search(tab + 1000, -1000, 1000, 3), search(tab + 1000, -1000, 1000, 7003),
           search(tab + 1000, -1000, 1000, 5));
    return 0;
}
