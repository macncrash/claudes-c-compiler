/* Loads of neighbouring words become load pairs, the higher word read
 * first too (sqlite3BtreeIndexMoveto reads pPage->aDataEnd, then
 * aCellIdx below it), and from stack slots; a store between, which may
 * write the second word, keeps them apart. */
#include <stdio.h>

struct page { long pad; const unsigned char *aCellIdx; const unsigned char *aDataEnd; unsigned short mask; };

static long __attribute__((noinline)) span(const struct page *p, int i) {
    const unsigned char *end = p->aDataEnd;
    unsigned m = p->mask;
    const unsigned char *idx = p->aCellIdx;
    return (end - idx) + (long)((idx[i] << 8 | idx[i + 1]) & m);
}

static long __attribute__((noinline)) store_between(long *w, long v) {
    long hi = w[1];
    w[0] = v;
    long lo = w[0];
    return hi * 1000 + lo;
}

static long __attribute__((noinline)) aliased(long *w, long *q, long v) {
    long a = w[1];
    *q = v;
    long b = w[0];
    return a * 1000 + b;
}

static long __attribute__((noinline)) spilled(long a, long b, long c) {
    volatile long t[2];
    t[0] = a * b;
    t[1] = b - c;
    long y = t[1], x = t[0];
    return x * 3 + y;
}

int main(void) {
    unsigned char data[64];
    for (int i = 0; i < 64; i++)
        data[i] = (unsigned char)(i * 13 + 5);
    struct page p = { 0, data + 4, data + 60, 0x3ff };
    long s = 0;
    for (int i = 0; i < 20; i++)
        s = s * 7 + span(&p, i);
    printf("span %ld\n", s);
    long w[2] = { 11, 22 };
    printf("store_between %ld\n", store_between(w, 33));
    long z[2] = { 1, 2 };
    printf("aliased same %ld\n", aliased(z, &z[0], 9));
    long other = 0;
    printf("aliased other %ld\n", aliased(z, &other, 8));
    printf("spilled %ld %ld\n", spilled(3, 4, 5), spilled(-6, 7, -8));
    return 0;
}
