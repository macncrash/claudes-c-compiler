/* __builtin_{add,sub,mul}_overflow: the flag is set when the infinitely
 * precise result does not fit the result type. Found by the integer
 * builtin fuzzer:
 * - __builtin_umul_overflow and __builtin_smul_overflow were taken as the
 *   long forms (their name ends in "l_overflow"), computed in 64 bits, and
 *   never overflowed for int values;
 * - with an unsigned __int128 operand beside a signed one or a signed
 *   result, a 128-bit multiply, or a 64-bit unsigned product into a mixed
 *   result, the exact result needs more than 128 bits, and ccc computed in
 *   a signed 128-bit or the result type and misjudged it. */
#include <stdio.h>

typedef unsigned long long UX;
typedef unsigned __int128 U8;
typedef __int128 S8;
#define U128(hi, lo) (((U8)(hi) << 64) | (UX)(lo))

static void p128(const char *tag, int o, U8 v) { printf("%s %d %016llx%016llx\n", tag, o, (UX)(v >> 64), (UX)v); }

__attribute__((noinline)) static int um(unsigned a, unsigned b, unsigned *r) { return __builtin_umul_overflow(a, b, r); }
__attribute__((noinline)) static int sm(int a, int b, int *r) { return __builtin_smul_overflow(a, b, r); }
__attribute__((noinline)) static int uml(unsigned long a, unsigned long b, unsigned long *r) { return __builtin_umull_overflow(a, b, r); }
__attribute__((noinline)) static int sml(long a, long b, long *r) { return __builtin_smull_overflow(a, b, r); }

int main(void) {
    unsigned ur; int sr; unsigned long ulr; long slr;
    int o;
    o = um(4294967295u, 578u, &ur); printf("um %d %u\n", o, ur);
    o = um(671129118u, 65535u, &ur); printf("um %d %u\n", o, ur);
    o = um(65536u, 65536u, &ur); printf("um %d %u\n", o, ur);
    o = um(65535u, 65537u, &ur); printf("um %d %u\n", o, ur);
    o = sm(89, 2147483646, &sr); printf("sm %d %d\n", o, sr);
    o = sm(-1, -2147483647 - 1, &sr); printf("sm %d %d\n", o, sr);
    o = sm(46341, 46341, &sr); printf("sm %d %d\n", o, sr);
    o = sm(46340, -46340, &sr); printf("sm %d %d\n", o, sr);
    o = uml(4294967296ul, 4294967296ul, &ulr); printf("uml %d %lu\n", o, ulr);
    o = sml(-1, -9223372036854775807L - 1, &slr); printf("sml %d %ld\n", o, slr);

    volatile U8 umax = U128(~0ULL, ~0ULL), one = 1, u64max = ~0ULL;
    volatile S8 smin = (S8)U128(1ULL << 63, 0), smax = (S8)U128(~0ULL >> 1, ~0ULL);
    volatile signed char sc = -64;
    volatile int si = -16;
    volatile long long sll = -9223372036854775807LL - 1;
    volatile unsigned long ul = ~0UL;
    U8 ru; S8 rs; long rl; unsigned long rul; short rh; unsigned char ruc; long long rll;

    o = __builtin_sub_overflow(one, sc, &rul); printf("a %d %lu\n", o, rul);        /* 65 fits */
    o = __builtin_add_overflow(umax, -1, &ru); p128("b", o, ru);                   /* 2^128 - 2 fits */
    o = __builtin_sub_overflow(umax, 255, &rll); printf("c %d %lld\n", o, rll);
    o = __builtin_mul_overflow(one, si, &ur); printf("d %d %u\n", o, ur);           /* -16 */
    o = __builtin_mul_overflow(smin, 4294967296ul, &rl); printf("e %d %ld\n", o, rl);
    o = __builtin_add_overflow(umax, smin, &rs); p128("f", o, (U8)rs);             /* 2^127 - 1 fits */
    o = __builtin_add_overflow(umax, smax, &ru); p128("g", o, ru);
    o = __builtin_sub_overflow(smin, umax, &rs); p128("h", o, (U8)rs);
    o = __builtin_mul_overflow(smax, smax, &ruc); printf("i %d %u\n", o, ruc);
    o = __builtin_mul_overflow(smin, sll, &rs); p128("j", o, (U8)rs);
    o = __builtin_mul_overflow(sll, sll, &ru); p128("k", o, ru);                   /* 2^126 fits */
    o = __builtin_mul_overflow(sll, sll, &rs); p128("l", o, (U8)rs);
    o = __builtin_mul_overflow(ul, ul, &ru); p128("m", o, ru);                     /* (2^64-1)^2 fits */
    o = __builtin_mul_overflow(ul, u64max, &rh); printf("n %d %d\n", o, rh);
    o = __builtin_mul_overflow(umax, -1, &rs); p128("o", o, (U8)rs);               /* -(2^128-1) */
    o = __builtin_mul_overflow(u64max + 1, u64max, &ru); p128("p", o, ru);
    o = __builtin_mul_overflow(u64max + 1, u64max + 1, &ru); p128("q", o, ru);
    o = __builtin_sub_overflow((U8)0, smin, &ru); p128("r", o, ru);               /* 2^127 fits */
    o = __builtin_sub_overflow((U8)0, smin, &rs); p128("s", o, (U8)rs);
    o = __builtin_mul_overflow(smin, -1, &rs); p128("t", o, (U8)rs);
    o = __builtin_mul_overflow(smin, 1, &rs); p128("u", o, (U8)rs);
    o = __builtin_mul_overflow(smin, (U8)1, &rs); p128("v", o, (U8)rs);
    o = __builtin_mul_overflow(smin, (U8)1, &ru); p128("w", o, ru);
    return 0;
}
