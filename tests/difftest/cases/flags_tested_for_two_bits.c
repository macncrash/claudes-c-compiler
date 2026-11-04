/* Flags tested against masks of two far-apart bits that no logical
 * immediate encodes (`pMem->flags & (MEM_Agg|MEM_Dyn)` in sqlite3VdbeExec),
 * branching on either outcome. A test may become one per bit, so each of
 * neither, one, the other and both set must go the same way, with sign bits
 * and neighbouring bits set too. */
#include <stdio.h>

static unsigned long hits;

static void __attribute__((noinline)) note(unsigned long v)
{
    hits = hits * 31 + v;
}

static void __attribute__((noinline)) if_any_u16(unsigned short f)
{
    if (f & 0x9000)
        note(1);
}

static void __attribute__((noinline)) if_none_u16(unsigned short f)
{
    if (!(f & 0x9000))
        note(2);
}

static int __attribute__((noinline)) either(unsigned f, int a, int b)
{
    if (f & 0x24)
        a += b * 3;
    return a - 1;
}

static int __attribute__((noinline)) neither_int(int f, int a)
{
    if ((f & 0x80000002) == 0)
        return a + 7;
    return a;
}

static long __attribute__((noinline)) wide_mask(unsigned long f, long a)
{
    /* One bit in each half stays a mask. */
    if (f & 0x100000004ul)
        a += 5;
    return a;
}

static long __attribute__((noinline)) both_paths_read_source(unsigned f, long a)
{
    if (f & 0x8200)
        return a + f;
    return a - f;
}

int main(void)
{
    static const unsigned long bits[] = {0, 1, 2, 4, 0x20, 0x24, 0x200, 0x1000, 0x8000, 0x9000, 0x8200, 0x7000, 0x6fff,
                                         0xffff, 0x80000000ul, 0x80000002ul, 0x7ffffffdul, 0xffffffffful, 0x100000000ul,
                                         0x100000004ul, ~0ul, ~4ul};
    for (unsigned i = 0; i < sizeof bits / sizeof bits[0]; i++) {
        unsigned long f = bits[i];
        if_any_u16((unsigned short)f);
        if_none_u16((unsigned short)f);
        note((unsigned long)either((unsigned)f, 10, 4));
        note((unsigned long)neither_int((int)f, 3));
        note((unsigned long)wide_mask(f, 1));
        note((unsigned long)both_paths_read_source((unsigned)f, 1000));
    }
    printf("%lu\n", hits);
    return 0;
}
