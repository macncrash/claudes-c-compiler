/* Temporaries that hold several values in one function, only some of
 * which can share an argument register: sqlite3BtreeIndexMoveto keeps an
 * address in a temporary across branches while x0 is still a parameter,
 * then puts a cell count in the same temporary to pass in x0. Also values
 * merged from two paths, carried around loops, updated in place, and
 * copied into and out of argument registers around calls. */
#include <stdio.h>

struct page { unsigned char hdr; unsigned short n; unsigned short off[8]; unsigned char data[64]; };
struct cursor { int ix; struct page *pg; long key; };

static int __attribute__((noinline)) cmp_cell(int n, const unsigned char *cell, long key) {
    long v = n;
    for (int i = 0; i < n && i < 6; i++)
        v = v * 31 + cell[i];
    return v < key ? -1 : v > key;
}

/* A binary search like sqlite3BtreeIndexMoveto's. */
static int __attribute__((noinline)) moveto(struct cursor *cur, long key, int bias) {
    struct page *pg = cur->pg;
    int lo = 0, hi = pg->n - 1, c = 0;
    if (bias > 0 && pg->n > 2)
        lo = 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        const unsigned char *cell = pg->data + (pg->off[mid] & 63);
        int n = cell[0] & 7;
        if (cell + n > pg->data + 63)
            return -99;
        c = cmp_cell(n, cell + 1, key);
        if (c < 0)
            lo = mid + 1;
        else if (c > 0)
            hi = mid - 1;
        else {
            cur->ix = mid;
            return 0;
        }
    }
    cur->ix = lo;
    return c;
}

static long __attribute__((noinline)) twice_add(long a, long b) { return 2 * a + b; }

/* One temporary reused: a value merged from two paths, then a loop
 * accumulator, then an argument. */
static long __attribute__((noinline)) reuse(long p, long q, int sel) {
    long t = sel ? p * 3 : q - 7;
    long acc = t;
    for (int i = 0; i < 5; i++) {
        acc = acc * 2 + i;
        if (acc > 1000)
            acc -= p;
    }
    long r = twice_add(acc, q);
    long s = sel ? r : r + p;
    return twice_add(s, acc) ^ p;
}

/* A parameter read after a call result arrives in the same register. */
static long __attribute__((noinline)) after_call(long x, long y) {
    long a = twice_add(y, 1);
    long b = twice_add(x, a);
    return b - x + a;
}

int main(void) {
    struct page pg;
    pg.hdr = 1;
    pg.n = 8;
    for (int i = 0; i < 8; i++)
        pg.off[i] = (unsigned short)(i * 7 + 1);
    for (int i = 0; i < 64; i++)
        pg.data[i] = (unsigned char)(i * 37 + 11);
    struct cursor cur = { 0, &pg, 0 };
    long keys[] = { -5, 0, 1000, 100000, 50000000, 9000000000L };
    unsigned long h = 0;
    for (int i = 0; i < 6; i++)
        for (int b = -1; b <= 1; b++) {
            int c = moveto(&cur, keys[i], b);
            h = h * 131 + (unsigned)(c + 100) + (unsigned)cur.ix;
        }
    printf("moveto %lu\n", h);
    printf("reuse %ld %ld %ld\n", reuse(5, 9, 1), reuse(400, -3, 0), reuse(-8, 2000, 1));
    printf("after %ld %ld\n", after_call(3, 4), after_call(-100, 77));
    return 0;
}
