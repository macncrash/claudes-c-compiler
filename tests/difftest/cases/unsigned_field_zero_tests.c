/* Signed tests against zero (`x > 0`, `x <= 0`, and `0 < x`, `0 >= x`) of
 * values that are never negative: unsigned char and short fields promoted
 * to int, masked and shifted values, and an int below a test that it is not
 * negative. They are tests for zero; each must still pick the right side
 * at 0, 1 and the largest value, and a value that can be negative must keep
 * its signed test. */
#include <stdio.h>

struct page { unsigned short n_cell; unsigned char leaf; unsigned char depth; int free; };

static int __attribute__((noinline)) cells(const struct page *p) {
    if (p->n_cell > 0)
        return p->n_cell * 3;
    return -1;
}

static int __attribute__((noinline)) empty(const struct page *p) {
    if (p->leaf <= 0)
        return 100;
    return p->leaf;
}

static int __attribute__((noinline)) reversed(const struct page *p) {
    int r = 0;
    if (0 < p->depth) r |= 1;
    if (0 >= p->n_cell) r |= 2;
    return r;
}

static int __attribute__((noinline)) masked(int x) {
    int m = x & 0x7f0;
    int s = (int)((unsigned)x >> 28);
    return (m > 0) * 10 + (s <= 0);
}

static int __attribute__((noinline)) guarded(int x) {
    if (x < 0)
        return -5;
    if (x > 0)
        return 1;
    return 0;
}

/* Can be negative: stays a signed test. */
static int __attribute__((noinline)) signed_free(const struct page *p) {
    return p->free > 0 ? 7 : p->free <= 0 ? 8 : 9;
}

int main(void) {
    static const unsigned short ns[] = { 0, 1, 2, 255, 256, 32767, 32768, 65535 };
    static const unsigned char bs[] = { 0, 1, 127, 128, 255 };
    static const int xs[] = { 0, 1, -1, 16, 0x7f0, 0x800, 0x10000000, -0x7fffffff - 1, 0x7fffffff };
    long sum = 0;
    for (unsigned i = 0; i < sizeof ns / sizeof ns[0]; i++) {
        for (unsigned j = 0; j < sizeof bs / sizeof bs[0]; j++) {
            struct page p = { ns[i], bs[j], bs[(j + i) % 5], (int)ns[i] - 300 };
            int c = cells(&p), e = empty(&p), r = reversed(&p), f = signed_free(&p);
            printf("n=%u leaf=%u depth=%u free=%d: %d %d %d %d\n",
                   p.n_cell, p.leaf, p.depth, p.free, c, e, r, f);
            sum += c + e + r + f;
        }
    }
    for (unsigned i = 0; i < sizeof xs / sizeof xs[0]; i++) {
        printf("x=%d: masked %d guarded %d\n", xs[i], masked(xs[i]), guarded(xs[i]));
        sum += masked(xs[i]) + guarded(xs[i]);
    }
    printf("sum %ld\n", sum);
    return 0;
}
