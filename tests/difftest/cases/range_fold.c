/* Comparisons decided by value ranges under dominating branch conditions,
 * run over boundary inputs: ones a range proves (arithmetic that cannot wrap,
 * masks, remainders, shifts, extensions, phis, a signed test read unsigned)
 * and ones it must leave alone (wrapping arithmetic, truncation, a signed
 * reinterpretation of an unsigned test, values compared with values). */
#include <limits.h>
#include <stdio.h>

#define FN(name, T, body) __attribute__((noinline)) static int name(T x) { body }

/* Decided. */
FN(i_dec_wrap, int, if (x <= 0 || (unsigned long)(unsigned)(x - 1) < UINT_MAX) return 1; return 0;)
FN(i_pos_sub, int, if (x > 0) return 2 + ((unsigned)(x - 1) < UINT_MAX); return 0;)
FN(i_nonneg_sub, int, if (x >= 0) return 2 + (x - 1 < INT_MAX); return 0;)
FN(i_mask, int, return (x & 0xff) <= 255;)
FN(i_small_sq, int, if (x > -5 && x < 5) return 2 + (x * x < 25); return 0;)
FN(i_shr_pos, int, if (x >= 0) return 2 + (x >> 31 == 0) + 2 * (x >> 4 <= INT_MAX >> 4); return 0;)
FN(i_shr_neg, int, if (x < 0) return 2 + (x >> 31 == -1); return 0;)
FN(i_sext, int, if (x < 100) return 2 + ((long)x < 100); return 0;)
FN(i_ne, int, if (x != 5) return 2 + (x == 5); return 0;)
FN(i_nested, int, if (x > 0) { if (x < 10) return 2 + (x - 1 < 9) + 2 * (x != 0); return 1; } return 0;)
FN(i_phi, int, { int y = x > 3 ? 3 : 7; return y < 8; })
FN(i_rem, int, if (x >= 0) return 2 + (x % 10 < 10) + 2 * (x / 10 <= INT_MAX / 10); return 0;)
FN(u_dec, unsigned, if (x >= 1) return 2 + (x - 1 != UINT_MAX); return 0;)
FN(u_rem, unsigned, return (x % 10 < 10) + 2 * (x / 16 <= 0x0fffffffu);)
FN(u_small_int, unsigned, if (x < 100) return 2 + ((int)x >= 0); return 0;)
FN(u_signed_test, unsigned, if ((int)x >= 0) return 2 + (x <= 0x7fffffffu); return 0;)
FN(u_signed_neg, unsigned, if ((int)x < 0) return 2 + (x > 0x7fffffffu); return 0;)
FN(u_or, unsigned, if (x < 16) return 2 + ((x | 3) < 16) + 2 * ((x ^ 9) <= 15); return 0;)
FN(i_select, int, { int y = x & 1 ? 10 : 20; return y > 9; })

/* Not decided: the answer depends on x. */
FN(u_wrap, unsigned, if (x <= 5) return 2 + (x - 6 > 100); return 0;)
FN(u_sub_vs_x, unsigned, if (x > 5) return 2 + (x - 6 < x); return 0;)
FN(i_unsigned_test, int, if ((unsigned)x > 10) return 2 + (x > 10); return 0;)
FN(l_trunc, long, if (x > 0) return 2 + ((int)x > 0); return 0;)
FN(c_reinterp, signed char, if (x < 0) return 2 + ((unsigned char)x >= 200); return 0;)
FN(u_big_mul, unsigned, if (x > 2) return 2 + (x * 3 > 6); return 0;)
FN(i_bound, int, if (x > 0) return 2 + (x - 1 < 100); return 0;)
FN(l_sum, long, if (x >= 0) return 2 + ((unsigned long)x + 5 >= 5); return 0;)
FN(u_shift, unsigned, if (x >= 2) return 2 + ((x << 3) >= 16); return 0;)

static const int ints[] = { INT_MIN, INT_MIN + 1, -100, -6, -5, -1, 0, 1, 2, 3, 4, 5, 9, 10, 11,
                            99, 100, 101, 255, 256, INT_MAX - 1, INT_MAX };
static const unsigned uns[] = { 0, 1, 2, 5, 6, 9, 10, 11, 15, 16, 99, 100, 0x7ffffffe, 0x7fffffff,
                                0x80000000u, 0x80000001u, 0x55555556u, 0xfffffffeu, 0xffffffffu };
static const long longs[] = { 0, 1, -1, 5, 0x7fffffffL, 0x80000000L, 0x100000000L, 0x100000001L,
                              LONG_MAX, LONG_MIN, -0x100000000L };
static const signed char chars[] = { -128, -127, -57, -56, -55, -1, 0, 1, 127 };

#define N(a) (int)(sizeof a / sizeof a[0])
#define RUN(fn, arr) do { printf("%-16s", #fn); for (int i = 0; i < N(arr); i++) printf("%d", fn(arr[i])); printf("\n"); } while (0)

int main(void) {
    RUN(i_dec_wrap, ints);
    RUN(i_pos_sub, ints);
    RUN(i_nonneg_sub, ints);
    RUN(i_mask, ints);
    RUN(i_small_sq, ints);
    RUN(i_shr_pos, ints);
    RUN(i_shr_neg, ints);
    RUN(i_sext, ints);
    RUN(i_ne, ints);
    RUN(i_nested, ints);
    RUN(i_phi, ints);
    RUN(i_rem, ints);
    RUN(u_dec, uns);
    RUN(u_rem, uns);
    RUN(u_small_int, uns);
    RUN(u_signed_test, uns);
    RUN(u_signed_neg, uns);
    RUN(u_or, uns);
    RUN(i_select, ints);
    RUN(u_wrap, uns);
    RUN(u_sub_vs_x, uns);
    RUN(i_unsigned_test, ints);
    RUN(l_trunc, longs);
    RUN(c_reinterp, chars);
    RUN(u_big_mul, uns);
    RUN(i_bound, ints);
    RUN(l_sum, longs);
    RUN(u_shift, uns);
    return 0;
}
