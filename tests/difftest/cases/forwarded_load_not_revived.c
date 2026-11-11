/* AArch64 peephole: forward_held_slots turned the reload of a 128-bit
 * temporary into nothing (the register still held it), and the store to
 * its slot then went as stored over; narrow_load_for_copy, reading the
 * deleted reload's text with the `mov w0, w0` after it, put back
 * `ldr w0, [sp, #160]` of the slot nothing stored any more. b came out as
 * a stack address instead of 0. Found by an __int128 fuzzer. */
#include <stdio.h>

typedef unsigned __int128 U;
typedef __int128 I;

static U smd(U a, U b)
{
    I x = (I)a, y = (I)b;
    if (!y || (y == -1 && x == (I)((U)1 << 127)))
        return a;
    return (U)(x % y);
}

static int lz(U v)
{
    unsigned long long h = v >> 64, l = (unsigned long long)v;
    return h ? __builtin_clzll(h) : l ? 64 + __builtin_clzll(l) : 128;
}

static void pr(const char *n, U v)
{
    printf("%s %016llx%016llx\n", n, (unsigned long long)(v >> 64), (unsigned long long)v);
}

__attribute__((noinline)) void g(U a, U b, U c, unsigned long long x, long long y, int k)
{
    U t0 = (U)(I)k;
    b = t0 - (U)(unsigned long long)(U)(unsigned)(U)k;
    U t2 = smd((((U)lz((U)1) << 13) | ((U)lz((U)1) >> 115)), (U)127);
    pr("b", b);
    pr("t2", t2);
}

int main(void)
{
    g((((U)1 << 64) - 1), (((U)0x00000000ffffffffULL << 64) | 0xffffffff00000000ULL), (U)(I)-5,
      0x8000000000000000ULL, 0, 64);
    g(0, 0, 0, 0, 0, -7);
    return 0;
}
