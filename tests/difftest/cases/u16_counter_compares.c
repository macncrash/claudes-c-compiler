/* A u16 field bumped, stored back and compared with another count
 * (sqlite3BtreeNext's `if (++pCur->ix >= pPage->nCell)`): the compare
 * zero-extends the halfword itself instead of masking it first, the
 * operands swapped when the bumped value comes first. Run across the
 * 65535 -> 0 wrap, byte counters too, and every condition direction. */
#include <stdio.h>

struct cur { unsigned short ix; unsigned short nCell; unsigned char b; unsigned char nb; };

static int __attribute__((noinline)) next_ge(struct cur *c) {
    if (++c->ix >= c->nCell)
        return 1;
    return 0;
}

static int __attribute__((noinline)) next_lt(struct cur *c) {
    if (++c->ix < c->nCell)
        return 1;
    return 0;
}

static int __attribute__((noinline)) count_gt(struct cur *c) {
    c->ix = (unsigned short)(c->ix + 1);
    return c->nCell > c->ix ? 7 : 9;
}

static int __attribute__((noinline)) byte_next(struct cur *c) {
    if (++c->b == c->nb)
        return 3;
    return (unsigned char)(c->b + 1) <= c->nb ? 4 : 5;
}

int main(void) {
    unsigned short starts[] = { 0, 1, 99, 100, 65534, 65535 };
    unsigned short cells[] = { 0, 1, 100, 101, 65535 };
    unsigned h = 0;
    for (int a = 0; a < 6; a++)
        for (int b = 0; b < 5; b++) {
            struct cur c = { starts[a], cells[b], (unsigned char)starts[a], (unsigned char)cells[b] };
            int r1 = next_ge(&c);
            int r2 = next_lt(&c);
            int r3 = count_gt(&c);
            int r4 = byte_next(&c);
            h = h * 31 + (unsigned)(r1 + 2 * r2 + 4 * r3 + 64 * r4) + c.ix + c.b;
            if (a == 5)
                printf("%u/%u: %d %d %d %d ix=%u b=%u\n", starts[a], cells[b], r1, r2, r3, r4, c.ix, c.b);
        }
    printf("hash %u\n", h);
    return 0;
}
