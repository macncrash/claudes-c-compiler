/* Valid programs that are long or deep, all well inside what the compiler
 * takes: a sum of 4,096 terms, parentheses and blocks nested by macros, and a
 * range designator over 100,000 elements. The sum took minutes when constant
 * evaluation looked for sizeof at every level of the expression. */
#include <stdio.h>

#define X2(e) e + e
#define X4(e) X2(e) + X2(e)
#define X16(e) X4(e) + X4(e) + X4(e) + X4(e)
#define X256(e) X16(e) + X16(e) + X16(e) + X16(e) + X16(e) + X16(e) + X16(e) + X16(e) \
              + X16(e) + X16(e) + X16(e) + X16(e) + X16(e) + X16(e) + X16(e) + X16(e)
#define X4096(e) X256(e) + X256(e) + X256(e) + X256(e) + X256(e) + X256(e) + X256(e) + X256(e) \
               + X256(e) + X256(e) + X256(e) + X256(e) + X256(e) + X256(e) + X256(e) + X256(e)

/* Nesting by repeated substitution: each level wraps the last. */
#define P1(e) (e + 1)
#define P4(e) P1(P1(P1(P1(e))))
#define P16(e) P4(P4(P4(P4(e))))
#define P256(e) P16(P16(P16(P16(e))))

#define B1(s) { s }
#define B4(s) B1(B1(B1(B1(s))))
#define B16(s) B4(B4(B4(B4(s))))
#define B256(s) B16(B16(B16(B16(s))))

static long __attribute__((noinline)) long_sum(long a)
{
    return X4096(a);
}

/* sizeof deep in a long sum still has to be worked out by the lowerer. */
static const int unsized[] = { 1, 2, 3, 4, 5 };

static long __attribute__((noinline)) long_sum_with_sizeof(long a)
{
    return X256(a) + sizeof unsized / sizeof unsized[0] + X16(a);
}

static long constant_sum = X4096(3);

static long __attribute__((noinline)) deep_parens(long a)
{
    return P256(a);
}

static long __attribute__((noinline)) deep_blocks(long a)
{
    B256(a += 5;)
    return a;
}

static int ranged[100000] = { [10 ... 99990] = 7 };

int main(void)
{
    long t = 0;
    for (int i = 0; i < 100000; i++)
        t += ranged[i];
    printf("%ld %ld %ld\n", long_sum(3), long_sum_with_sizeof(2), constant_sum);
    printf("%ld %ld %ld\n", deep_parens(9), deep_blocks(1), t);
    return 0;
}
