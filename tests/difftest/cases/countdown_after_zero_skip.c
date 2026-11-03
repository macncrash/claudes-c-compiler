/* Count-down loops whose counter is known positive only along the edge
 * into the loop: zlib's gen_codes skips a zero length before bi_reverse's
 * `while (--len > 0)`, and SQLite's groupConcatStep counts down from a
 * value checked positive. Each latch test `n > 0` may become `n != 0`
 * only where the counter cannot start at zero or below; the unguarded
 * loops here start at 0 and at negative counts. */
#include <stdio.h>

static unsigned __attribute__((noinline)) reverse(unsigned code, int len)
{
    unsigned res = 0;
    do {
        res |= code & 1;
        code >>= 1, res <<= 1;
    } while (--len > 0);
    return res >> 1;
}

static unsigned long __attribute__((noinline)) codes(const unsigned short *lens, int n)
{
    unsigned long h = 0;
    for (int i = 0; i < n; i++) {
        int len = lens[i];
        if (len == 0)
            continue;
        h = h * 31 + reverse((unsigned)i * 2654435761u, len);
    }
    return h;
}

/* Guarded by `k > 0` on the edge in, counted down to zero. */
static long __attribute__((noinline)) skip_positive(const unsigned char *z, long long k)
{
    const unsigned char *s = z;
    if (k > 0) {
        do {
            if (*z == 0)
                break;
            z++;
        } while (--k > 0);
    }
    return (z - s) * 1000 + (long)k;
}

/* No guard: `k` may be 0 or negative on entry, so `--k > 0` must stay a
 * signed test. */
static long __attribute__((noinline)) skip_any(const unsigned char *z, long long k)
{
    long steps = 0;
    do {
        steps++;
        if (*z)
            z++;
    } while (--k > 0);
    return steps * 1000 + (long)k;
}

int main(void)
{
    static const unsigned short lens[] = {0, 1, 2, 3, 0, 7, 8, 15, 0, 16, 5, 0};
    static const unsigned char text[] = "abcdefghij";
    static const long long ks[] = {-5, -1, 0, 1, 2, 9, 10, 11, 40, -9223372036854775807LL - 1};
    unsigned long h = codes(lens, (int)(sizeof lens / sizeof lens[0]));
    for (unsigned i = 0; i < sizeof ks / sizeof ks[0]; i++) {
        h = h * 31 + (unsigned long)skip_positive(text, ks[i]);
        if (ks[i] > -10 && ks[i] < 100)
            h = h * 31 + (unsigned long)skip_any(text, ks[i]);
    }
    printf("%lu\n", h);
    return 0;
}
