/* Values live across calls that only a rare path reads, in the shape of
 * getPageNormal: the common path keeps several values across a call, and
 * the rare one adds more, including a value joined from two paths (a
 * phi). Saving another register on every call costs more than keeping
 * these on the stack, which must not change what the rare path computes. */
#include <stdio.h>

typedef unsigned long ul;

static ul __attribute__((noinline)) step(ul v, ul w)
{
    return v * 5 + w;
}

static ul __attribute__((noinline)) fetch(ul a, ul b, ul c, ul d, ul e, int rare)
{
    ul r = step(a, b);
    r = step(r, c) + d;
    if ((rare & 1) && (rare & 2) && (rare & 4) && (rare & 8) && (rare & 16) && (rare & 32) && (rare & 64)) {
        ul x = step(e, 1);
        ul y = step(x, r);
        ul sum = 0;
        sum = step(sum, x);
        if (sum & 1)
            y += step(y, sum);
        return step(x, y) + sum + a + b + c + d + e;
    }
    return r + step(e, a) + b;
}

int main(void)
{
    ul h = 0;
    for (int i = 0; i < 64; i++)
        h = h * 31 + fetch((ul)i, (ul)i * 3, 7, (ul)i ^ 5, ~(ul)i, i % 16 == 3 ? 127 : i);
    printf("%lu\n", h);
    return 0;
}
