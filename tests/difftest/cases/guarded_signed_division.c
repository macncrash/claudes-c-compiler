/* Signed divisions and remainders by constants of values a dominating
 * test keeps non-negative, in the shape of SQLite's record serial types:
 * a signed char loaded, tested `< 12` (widened to int), and then
 * `(serial_type - 12) / 2` taken at a wider type. Under the test the
 * division may round as an unsigned one; without it, or under an unsigned
 * test of the widened value, negative values must still round toward
 * zero. */
#include <stdio.h>

static int __attribute__((noinline)) blob_len(const signed char *p)
{
    int t = p[1];
    if (t < 12)
        return -1;
    long n = (t - 12) / 2;
    return (int)(n + (t - 12) % 4 + (t - 12) / 3 + (t - 12) % 10);
}

static int __attribute__((noinline)) unguarded(const signed char *p)
{
    int t = p[1];
    return (t - 12) / 2 + (t - 12) % 4 + (t - 12) / 3;
}

static int __attribute__((noinline)) unsigned_test(const signed char *p)
{
    int t = p[1];
    if ((unsigned)t < 12u)
        return -1;
    return (t - 12) / 2 + (t - 12) % 4;
}

/* A restart that re-reads the type, as vdbeRecordCompareString does. */
static int __attribute__((noinline)) restart(const signed char *p, int extra)
{
    int t = p[0];
again:
    if (t < 12) {
        if (t < 0 && extra-- > 0) {
            t = (unsigned char)p[1] + 12;
            goto again;
        }
        return t;
    }
    return (t - 12) / 2 - (t - 12) % 2;
}

int main(void)
{
    static const signed char v[] = {0, 11, 12, 13, 14, 27, 100, 127, -1, -12, -13, -128, 5, 64};
    unsigned long h = 0;
    for (unsigned i = 0; i + 1 < sizeof v; i++) {
        h = h * 31 + (unsigned)blob_len(v + i);
        h = h * 31 + (unsigned)unguarded(v + i);
        h = h * 31 + (unsigned)unsigned_test(v + i);
        h = h * 31 + (unsigned)restart(v + i, 1);
        h = h * 31 + (unsigned)restart(v + i, 0);
    }
    printf("%lu\n", h);
    return 0;
}
