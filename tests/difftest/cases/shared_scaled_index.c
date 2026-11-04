/* One index scaled once and used by several accesses, in the shape of
 * sqlite3VdbeExec's OP_Column: `aOffset[i]` read and `&aType[i]` written
 * through in a loop, both from the same `i * 4`, and an index loaded
 * through itself. The scaled value may become each access's own shifted
 * index. */
#include <stdio.h>

typedef unsigned int u32;
typedef unsigned short u16;

struct cursor {
    u16 nField;
    u16 nHdrParsed;
    u32 aType[16];
};

static unsigned long __attribute__((noinline)) column(struct cursor *c, const u32 *aOffset, const unsigned char *hdr, int n)
{
    u32 i = c->nHdrParsed;
    u32 off = aOffset[i];
    u32 *t = &c->aType[i];
    for (int k = 0; k < n && i + k < 16; k++) {
        t[k] = hdr[k];
        off += hdr[k] < 12 ? hdr[k] : (hdr[k] - 12) / 2;
    }
    return off;
}

static long __attribute__((noinline)) chase(const int *next, long i, int steps)
{
    long sum = 0;
    while (steps-- > 0) {
        i = next[i];
        sum += i;
    }
    return sum;
}

int main(void)
{
    static const u32 aOffset[16] = {3, 9, 27, 81, 243, 729, 2187, 6561, 1, 2, 4, 8, 16, 32, 64, 128};
    static const unsigned char hdr[8] = {1, 7, 12, 13, 40, 255, 0, 11};
    static const int next[8] = {3, 0, 5, 7, 1, 6, 2, 4};
    unsigned long h = 0;
    for (u16 p = 0; p < 16; p++) {
        struct cursor c = {16, p, {0}};
        h = h * 31 + column(&c, aOffset, hdr, (int)(p % 9));
        for (int k = 0; k < 16; k++)
            h = h * 31 + c.aType[k];
    }
    for (long s = 0; s < 8; s++)
        h = h * 31 + (unsigned long)chase(next, s, (int)s + 3);
    printf("%lu\n", h);
    return 0;
}
