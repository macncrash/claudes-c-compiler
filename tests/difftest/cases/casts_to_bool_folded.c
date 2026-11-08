/* A cast to _Bool tests against zero; it does not keep the low byte. Each value
 * three ways: folded where a constant is required, folded in a static
 * initializer, and at run time. */
#include <stdio.h>
#include <stdbool.h>

typedef _Bool flag;
#define NI __attribute__((noinline))

enum {
    E6 = (_Bool)6,
    E256 = (_Bool)256,
    EHALF = (_Bool)0.5,
    ENEGZ = (_Bool)-0.0,
    ECHAIN = (int)(_Bool)(unsigned char)256,
    ETYPEDEF = (flag)0x100000000ll,
    EBOOL = (bool)2 + (bool)2,
};

static const long s1 = 2147483647l ^ ((_Bool)(1549844286u % 9u));
static const long s2 = (_Bool)0x100000000ull * 10 + (_Bool)-1;
static const int s3 = (_Bool)0.25f + (_Bool)1e-300 * 2 + (_Bool)-0.0 * 4;
static const unsigned s4 = (unsigned)(_Bool)(-128 + 0) << 3;
static const int s5 = (flag)(char)256 + (flag)(short)65536 * 2;
static char sized[(_Bool)7 + (_Bool)8 + 1];
static int table[] = { (_Bool)3, (_Bool)0, (_Bool)-5, (_Bool)0x80000000u, (_Bool)0.0 };

static NI long at_run_time(volatile unsigned a, volatile double h, volatile long long w,
                           volatile double nz)
{
    return 2147483647l ^ ((_Bool)(a % 9u))
         + 10 * (long)((_Bool)h + (_Bool)nz * 2 + (flag)w * 4);
}

int main(void)
{
    printf("%d %d %d %d %d %d %d\n", E6, E256, EHALF, ENEGZ, ECHAIN, ETYPEDEF, EBOOL);
    printf("%lx %ld %d %u %d %zu\n", (unsigned long)s1, s2, s3, s4, s5, sizeof sized);
    for (unsigned i = 0; i < sizeof table / sizeof table[0]; i++)
        printf("%d ", table[i]);
    printf("\n%lx\n", (unsigned long)at_run_time(1549844286u, 0.5, 0x100000000ll, -0.0));
    return 0;
}
