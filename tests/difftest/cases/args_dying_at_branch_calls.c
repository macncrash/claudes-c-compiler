/* Parameters whose last use on one path is a call's arguments (SQLite's
 * btreeParseCellPtr ends its overflow branch with a call taking pPage,
 * pCell, pInfo), while blocks laid out after that call (the varint
 * decoder's fast paths) still read them. Plus a value live across the same
 * kind of call on another path, and records written through a pointer the
 * call also gets. */
#include <stdio.h>

typedef struct { unsigned long key; unsigned size; unsigned short local, total; } Info;
typedef struct { unsigned short maxLocal, minLocal; unsigned calls; } Page;

static void __attribute__((noinline)) adjust(Page *pg, const unsigned char *cell, Info *info) {
    pg->calls++;
    info->local = (unsigned short)(pg->minLocal + (info->size - pg->minLocal) % 13 + (cell[0] & 3));
    info->total = (unsigned short)(info->local + 4);
}

static void __attribute__((noinline)) parse(Page *pg, const unsigned char *cell, Info *info) {
    const unsigned char *p = cell;
    unsigned n = *p;
    if (n >= 0x80) {
        const unsigned char *end = cell + 8;
        n &= 0x7f;
        do {
            n = (n << 7) | (*++p & 0x7f);
        } while (*p >= 0x80 && p < end);
    }
    p++;
    unsigned long key = *p++;
    if (key >= 0x80) {
        key &= 0x7f;
        for (int i = 0; i < 7 && *p >= 0x80; i++)
            key = (key << 7) | (*p++ & 0x7f);
        key = (key << 7) | *p++;
    }
    info->key = key;
    info->size = n;
    if (n <= pg->maxLocal) {
        unsigned short t = (unsigned short)(p - cell + n);
        info->total = t < 4 ? 4 : t;
        info->local = (unsigned short)n;
    } else {
        adjust(pg, cell, info);
    }
}

static unsigned long __attribute__((noinline)) walk(Page *pg, const unsigned char *buf, int n, unsigned long seed) {
    Info info;
    unsigned long h = seed;
    for (int i = 0; i < n; i++) {
        parse(pg, buf + 3 * i, &info);
        h = h * 31 + info.key + info.size * 7 + info.local * 3 + info.total;
        if (info.size > 200)
            h ^= (unsigned long)pg->calls << 5; /* h lives across the next call */
    }
    return h;
}

int main(void) {
    static unsigned char buf[3 * 600 + 16];
    unsigned s = 99;
    for (unsigned i = 0; i < sizeof buf; i++) {
        s = s * 1664525u + 1013904223u;
        buf[i] = (unsigned char)(s >> 24);
    }
    Page pg = { 120, 23, 0 };
    printf("walk %lx\n", walk(&pg, buf, 600, 7));
    printf("calls %u\n", pg.calls);
    pg.maxLocal = 30;
    printf("walk %lx\n", walk(&pg, buf, 600, 11));
    printf("calls %u\n", pg.calls);
    return 0;
}
