/* An int field read as unsigned on the way to a join, in the shape of
 * sqlite3VdbeExec's OP_Column: `p2 = (u32)pOp->p2` on one path, a
 * different value on another, the sign-extending load then a 32-bit copy
 * into the join's register. Negative fields must come out as large
 * unsigned values, zero-extended. */
#include <stdio.h>

typedef unsigned int u32;

struct op {
    int p1;
    int p2;
    int p3;
};

static unsigned long __attribute__((noinline)) column(const struct op *o, const u32 *map, int moved, long *other)
{
    u32 p2;
    long d = o->p1;
    if (moved) {
        p2 = map[o->p3 & 3];
        d += 100;
    } else {
        p2 = (u32)o->p2;
    }
    *other = d + 1;
    return (unsigned long)p2 * 3 + (p2 >> 28);
}

int main(void)
{
    static const u32 map[4] = {7, 0x80000000u, 0xffffffffu, 0};
    static const struct op ops[] = {{1, 2, 3}, {-1, -2, -3}, {0, 0x7fffffff, 1}, {5, -2147483647 - 1, 2}, {9, -1, 0}};
    unsigned long h = 0;
    for (unsigned i = 0; i < sizeof ops / sizeof ops[0]; i++) {
        for (int moved = 0; moved < 2; moved++) {
            long other;
            h = h * 31 + column(&ops[i], map, moved, &other);
            h = h * 31 + (unsigned long)other;
        }
    }
    printf("%lu\n", h);
    return 0;
}
