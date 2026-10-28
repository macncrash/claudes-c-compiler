/* Calls whose result a function returns (SQLite's btreeParseCellPtr ends
 * with btreeParseCellAdjustSizeForOverflow(pPage, pCell, pInfo)), in the
 * shapes where branching to the callee would be wrong or right: a 32-bit
 * result truncated from a 64-bit callee, a 64-bit result that must extend
 * a 32-bit one, a pointer to a local passed on, stack arguments, a variadic
 * callee, an indirect call, struct and double results, and values in
 * callee-saved registers up to the call. */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct { long a, b; } Pair;
typedef struct { long v[6]; } Big;

static unsigned __attribute__((noinline)) all_ones(void) { return 0xffffffffu; }
static long __attribute__((noinline)) wide(long x) { return (long)((unsigned long)x * 0x100000001ul + 5); }
static long __attribute__((noinline)) as_long(void) { return (int)all_ones(); }
static int __attribute__((noinline)) low(long x) { return (int)wide(x); }

static int __attribute__((noinline)) sum(const int *p, int n) {
    int s = 0;
    for (int i = 0; i < n; i++)
        s += p[i] * (i + 1);
    return s;
}
static int __attribute__((noinline)) scratch(int k) {
    volatile int junk[32];
    for (int i = 0; i < 32; i++)
        junk[i] = i * k;
    return junk[k & 31];
}
static int __attribute__((noinline)) local_sum(int n) {
    int a[4] = { n, n + 1, n + 2, n + 3 };
    scratch(n);
    return sum(a, 4);
}

static long __attribute__((noinline)) ten(long a, long b, long c, long d, long e, long f, long g, long h, long i, long j) {
    return a + 2 * b + 3 * c + 4 * d + 5 * e + 6 * f + 7 * g + 8 * h + 9 * i + 10 * j;
}
static long __attribute__((noinline)) pass_ten(long x) { return ten(x, x + 1, x + 2, x + 3, x + 4, x + 5, x + 6, x + 7, x + 8, x + 9); }

static long __attribute__((noinline)) vsum(int n, ...) {
    va_list ap;
    va_start(ap, n);
    long s = 0;
    for (int i = 0; i < n; i++)
        s = s * 3 + va_arg(ap, long);
    va_end(ap);
    return s;
}
static long __attribute__((noinline)) pass_var(long x) { return vsum(3, x, x * 2, x * 3); }
static long __attribute__((noinline)) pass_none(void) { return vsum(0); }

static int add1(int x) { return x + 1; }
static int dbl(int x) { return x * 2; }
static int (*const ops[2])(int) = { add1, dbl };
static int __attribute__((noinline)) apply(int which, int x) { return ops[which & 1](x); }

static Pair __attribute__((noinline)) mkpair(long a) { Pair p = { a, -a }; return p; }
static Pair __attribute__((noinline)) pass_pair(long a) { return mkpair(a + 1); }
static Big __attribute__((noinline)) mkbig(long a) { Big b; for (int i = 0; i < 6; i++) b.v[i] = a * i; return b; }
static Big __attribute__((noinline)) pass_big(long a) { return mkbig(a - 1); }
static double __attribute__((noinline)) half(double d) { return d / 2; }
static double __attribute__((noinline)) pass_half(double d) { return half(d + 1); }

static long __attribute__((noinline)) keep(long a, long b, long c) {
    long t = wide(a) ^ b; /* a, b, c live across this call */
    return ten(t, a, b, c, a ^ c, b ^ c, 1, 2, 3, 4) > 0 ? wide(t + c) : wide(b - c);
}

int main(void) {
    printf("as_long %ld low %d\n", as_long(), low(7));
    printf("local %d\n", local_sum(5));
    printf("ten %ld var %ld none %ld\n", pass_ten(3), pass_var(4), pass_none());
    printf("apply %d %d\n", apply(0, 20), apply(1, 20));
    Pair p = pass_pair(9);
    Big b = pass_big(4);
    printf("pair %ld %ld big %ld %ld\n", p.a, p.b, b.v[1], b.v[5]);
    printf("half %.3f\n", pass_half(2.5));
    printf("keep %ld %ld\n", keep(3, 5, 7), keep(-3, 5, 70));
    return 0;
}
