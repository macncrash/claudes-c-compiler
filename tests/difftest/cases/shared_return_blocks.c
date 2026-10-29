/* Paths that meet at one return block: a u16 result masked once at the end
 * (SQLite's cellSizePtrIdxLeaf, a varint's fast and slow paths), epilogues
 * restoring callee-saved registers reached from several branches, early
 * returns from loops, and calls in return position next to them. */
#include <stdio.h>
#include <string.h>

struct page { unsigned short max_local, min_local; unsigned usable; };

static unsigned short __attribute__((noinline)) cell_size(const struct page *p, const unsigned char *cell) {
    const unsigned char *q = cell;
    unsigned n = *q;
    if (n >= 0x80) {
        const unsigned char *end = q + 8;
        n &= 0x7f;
        do {
            n = (n << 7) | (*++q & 0x7f);
        } while (*q >= 0x80 && q < end);
    }
    q++;
    unsigned size;
    if (n <= p->max_local) {
        size = n + (unsigned)(q - cell);
        if (size < 4)
            size = 4;
    } else {
        unsigned min = p->min_local;
        size = min + (n - min) % (p->usable - 4);
        if (size > p->max_local)
            size = min;
        size += 4 + (unsigned short)(q - cell);
    }
    return (unsigned short)size;
}

static long __attribute__((noinline)) helper(long x) { return x * 3 + 1; }

/* Several values kept across calls, so the epilogue restores registers. */
static long __attribute__((noinline)) many_exits(const long *v, int n, long key) {
    long acc = 0, prod = 1;
    for (int i = 0; i < n; i++) {
        if (v[i] == key)
            return acc + helper(prod);
        if (v[i] < 0)
            return -helper(acc - prod);
        acc += helper(v[i]);
        prod = prod * 7 % 1000003;
        if (acc > 1000000)
            break;
    }
    if (acc & 1)
        return helper(acc);
    return acc ^ prod;
}

static signed char __attribute__((noinline)) classify(int c) {
    signed char r;
    if (c < 0)
        r = -1;
    else if (c < 10)
        r = (signed char)c;
    else if (c < 100)
        r = (signed char)(c / 10 + 10);
    else
        return (signed char)helper(c);
    return (signed char)(r * 2);
}

int main(void) {
    struct page p = { 60, 20, 512 };
    unsigned char cells[][9] = {
        { 3 }, { 0 }, { 59 }, { 0x81, 0x00 }, { 0x81, 0x7f }, { 0xff, 0xff, 0x01 },
        { 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x05 }, { 0x83, 0x60 },
    };
    unsigned sum = 0;
    for (unsigned i = 0; i < sizeof cells / sizeof cells[0]; i++) {
        unsigned short s = cell_size(&p, cells[i]);
        printf("%u ", s);
        sum = sum * 31 + s;
    }
    printf("| %u\n", sum);

    long v[] = { 5, 9, 12, 40, 7, -3, 100 };
    printf("%ld %ld %ld %ld %ld\n", many_exits(v, 7, 12), many_exits(v, 7, 99), many_exits(v, 5, 99),
           many_exits(v, 0, 1), many_exits(v + 1, 3, 1));
    long big[64];
    for (int i = 0; i < 64; i++)
        big[i] = 100000 + i;
    printf("%ld\n", many_exits(big, 64, -1));

    unsigned t = 0;
    for (int c = -20; c < 300; c += 7)
        t = t * 5 + (unsigned)classify(c);
    printf("%u\n", t);
    return 0;
}
