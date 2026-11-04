/* A jump through a table of 24-byte records, `op = &ops[op->next - 1]` in
 * the shape of sqlite3VdbeExec, with enough values live across calls that
 * the table base stays on the stack and is reloaded between the multiply
 * and the add. The base points into the middle of the table, so int
 * indices go negative and must be sign-extended before scaling. */
#include <stdio.h>

typedef unsigned long ul;

struct op {
    int kind;
    int next;
    ul arg;
    ul pad;
};

static ul __attribute__((noinline)) touch(ul v, ul w)
{
    return v * 3 + w;
}

static ul __attribute__((noinline)) run(const struct op *base, int start, ul a, ul b, ul c, ul d, ul e, ul f, ul g,
                                        ul h, ul i, ul j, ul k, ul l)
{
    const struct op *op = &base[start - 1];
    ul acc = 0;
    int steps = 0;
    while (op->kind && steps++ < 40) {
        acc = touch(acc, op->arg);
        a = touch(a, b); b = touch(b, c); c = touch(c, d); d = touch(d, e); e = touch(e, f); f = touch(f, g);
        g = touch(g, h); h = touch(h, i); i = touch(i, j); j = touch(j, k); k = touch(k, l); l = touch(l, acc);
        if (op->kind == 2)
            op = &base[op->next + 3 - 1];
        else
            op = &base[op->next - 1];
    }
    return acc + a + b + c + d + e + f + g + h + i + j + k + l + (ul)steps;
}

int main(void)
{
    static const struct op table[] = {
        {1, -1, 11, 0}, {1, 0, 5, 0}, {2, -2, 7, 0}, {1, 3, 100, 0}, {1, -2, 1, 0}, {2, -4, 9, 0}, {0, 0, 0, 0}, {1, 4, 3, 0},
    };
    ul hash = 0;
    for (int s = -2; s <= 5; s++)
        hash = hash * 31 + run(table + 3, s, (ul)s, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12);
    printf("%lu\n", hash);
    return 0;
}
