/* A field of a local whose address escapes, read and written in a loop
 * that makes no calls, is kept in a register through the loop and stored
 * back on every way out (SQLite's balance_nonroot gathering cells with
 * b.apCell[b.nCell] = ...; b.nCell++). Also loops that must leave it in
 * memory: a pointer of the field's type that may reach it, a call, a char
 * access; and exits into blocks other paths reach too, nested loops, a
 * loop that only stores, and char, double and pointer fields. */
#include <stdio.h>
#include <string.h>

typedef unsigned char u8;
typedef struct { u8 **apCell; int nCell; short pad; double sum; u8 tag; u8 *last; } CellArray;

static void __attribute__((noinline)) look(CellArray *b) {
    printf("n=%d tag=%d sum=%.2f last=%d\n", b->nCell, b->tag, b->sum, b->last ? (int)*b->last : -1);
}
static int __attribute__((noinline)) bump(int x) { return x + 3; }

static int __attribute__((noinline)) gather(u8 *data, const unsigned short *idx, int n, u8 **cells) {
    CellArray b;
    memset(&b, 0, sizeof b);
    b.apCell = cells;
    look(&b);
    const unsigned short *p = idx, *end = idx + n;
    while (p < end) {
        b.apCell[b.nCell] = data + (*p & 63);
        p++;
        b.nCell++;
    }
    look(&b);
    return b.nCell;
}

/* Several exits, one into a block another path reaches. */
static int __attribute__((noinline)) early_exits(int *v, int n, int stop) {
    CellArray b;
    memset(&b, 0, sizeof b);
    look(&b);
    int r = 0;
    if (stop < 0)
        goto out;
    for (int i = 0; i < n; i++) {
        b.nCell += v[i];
        if (v[i] == stop)
            goto out;
        if (b.nCell > 1000)
            break;
        b.sum += v[i] * 0.5;
    }
    r = 1;
out:
    look(&b);
    return r * 100000 + b.nCell;
}

/* A pointer of the field's type that may point at it: not promoted. */
static int __attribute__((noinline)) aliased(int *q, int n) {
    CellArray b;
    memset(&b, 0, sizeof b);
    look(&b);
    if (!q)
        q = &b.nCell;
    for (int i = 0; i < n; i++) {
        b.nCell++;
        *q += 2;
    }
    look(&b);
    return b.nCell;
}

/* A call in the loop: not promoted; a char store reaches the byte field. */
static int __attribute__((noinline)) with_call(int n) {
    CellArray b;
    memset(&b, 0, sizeof b);
    look(&b);
    u8 *raw = (u8 *)&b.tag;
    for (int i = 0; i < n; i++) {
        b.nCell = bump(b.nCell);
        b.tag++;
        if (i & 1)
            *raw ^= 0x40;
    }
    look(&b);
    return b.nCell + b.tag;
}

/* Nested loops, a store-only loop, double and pointer fields. */
static double __attribute__((noinline)) nested(u8 *data, int rows, int cols) {
    CellArray b;
    memset(&b, 0, sizeof b);
    look(&b);
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            b.sum += data[r * cols + c] * 0.25;
            b.last = &data[r * cols + c];
            b.nCell++;
        }
        b.tag = (u8)r;
    }
    for (int k = 0; k < cols; k++)
        b.pad = (short)(k * 7);
    look(&b);
    return b.sum + b.pad;
}

int main(void) {
    u8 data[64];
    for (int i = 0; i < 64; i++)
        data[i] = (u8)(i * 37 + 5);
    unsigned short idx[40];
    for (int i = 0; i < 40; i++)
        idx[i] = (unsigned short)(i * 2654435761u >> 7);
    u8 *cells[40];
    int g = gather(data, idx, 40, cells);
    unsigned h = 0;
    for (int i = 0; i < g; i++)
        h = h * 31 + *cells[i];
    printf("gather %d %u\n", g, h);
    printf("gather0 %d\n", gather(data, idx, 0, cells));
    int v[10] = { 5, 9, 300, 7, 2, 600, 1, 8, 4, 3 };
    printf("exits %d %d %d %d\n", early_exits(v, 10, 7), early_exits(v, 10, 99), early_exits(v, 3, 99), early_exits(v, 10, -1));
    int other = 0;
    printf("aliased %d %d\n", aliased(0, 5), aliased(&other, 5));
    printf("other %d\n", other);
    printf("call %d\n", with_call(9));
    printf("nested %.2f %.2f\n", nested(data, 4, 9), nested(data, 0, 3));
    return 0;
}
