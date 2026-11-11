/* __int128 values whose low 64 bits are zero.
 *
 * As a condition (if, ?:, &&, ||, while) the value was tested on its low
 * register only, so 1 << 64 was false. And passes read 128-bit constants
 * through a 64-bit accessor: ((U)1 << 64) < (U)x became x > 0, and
 * (double)((U)1 << 64) could fold to 0.0. */
#include <stdio.h>

typedef unsigned __int128 U;
typedef __int128 I;

static void pr(const char *n, U v)
{
    printf("%s %016llx%016llx\n", n, (unsigned long long)(v >> 64), (unsigned long long)v);
}

__attribute__((noinline)) int t_cond(U a) { return a ? 1 : 2; }
__attribute__((noinline)) U t_sel(U a, U b, U c) { return a ? b : c; }
__attribute__((noinline)) int t_if(U a) { if (a) return 1; return 2; }
__attribute__((noinline)) int t_not(U a) { return !a; }
__attribute__((noinline)) int t_and(U a, int x) { return a && x; }
__attribute__((noinline)) int t_or(U a, int x) { return x || a; }
__attribute__((noinline)) int t_while(U a) { int n = 0; while (a) { a >>= 8; n++; } return n; }
__attribute__((noinline)) int t_for(U a) { int n = 0; for (; a; a <<= 1) n++; return n; }
__attribute__((noinline)) int t_do(U a) { int n = 0; do { n++; a >>= 1; } while (a); return n; }
__attribute__((noinline)) _Bool t_bool(U a) { return a; }
__attribute__((noinline)) int t_signed(I a) { return a ? 3 : 4; }
__attribute__((noinline)) U t_shift(U a, U x) { return (U)((I)(~a) >> ((a ? x : (U)0x70) & 127)); }

__attribute__((noinline)) int c_lt(unsigned long long x) { return ((U)1 << 64) < (U)x; }
__attribute__((noinline)) int c_gt(unsigned long long x) { return (U)x > ((U)1 << 64); }
__attribute__((noinline)) int c_le(unsigned long long x) { return (U)x <= ((U)1 << 64); }
__attribute__((noinline)) int c_ge(unsigned long long x) { return ((U)1 << 64) >= (U)x; }
__attribute__((noinline)) int c_eq(unsigned long long x) { return (U)x == ((U)1 << 64); }
__attribute__((noinline)) int c_ne1(unsigned long long x) { return (U)x != (((U)1 << 64) | 1); }
__attribute__((noinline)) int c_slt(long long y) { return (I)y < -((I)1 << 70); }
__attribute__((noinline)) U c_and(U a) { return a & ((U)1 << 64); }
__attribute__((noinline)) U c_mul(U a) { return a * ((U)1 << 64); }
__attribute__((noinline)) U c_add(U a) { return a + ((U)1 << 64); }
__attribute__((noinline)) double f_conv(void) { U k = (U)1 << 64; return (double)k; }
__attribute__((noinline)) double f_sconv(void) { I k = -((I)1 << 100); return (double)k; }

int main(void)
{
    U h = (U)1 << 64;
    U m = (U)1 << 127;
    printf("%d %d %d %d %d %d\n", t_cond(h), (int)t_sel(h, 5, 6), t_if(h), t_not(h), t_and(h, 1), t_or(h, 0));
    printf("%d %d %d %d %d\n", t_while(h), t_for(h), t_do(m), t_bool(h), t_signed((I)m));
    printf("%d %d %d %d\n", t_cond(0), t_if(m), t_and(m, 0), t_while(0));
    pr("shift", t_shift(h, 0));
    printf("%d %d %d %d %d %d %d\n", c_lt(1), c_gt(5), c_le(~0ULL), c_ge(0), c_eq(0), c_ne1(1), c_slt(-5));
    pr("and", c_and(((U)3 << 64) | 7));
    pr("mul", c_mul(0x1234));
    pr("add", c_add(~(U)0));
    printf("%.1f %.1f\n", f_conv(), f_sconv());
    return 0;
}
