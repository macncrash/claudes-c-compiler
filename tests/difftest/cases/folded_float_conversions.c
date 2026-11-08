/* Floating-point conversions where expressions are folded, and the run-time
 * conversion of a narrow signed value cast to unsigned: each folded in a static
 * initializer and computed from volatile copies, printed exactly with %a. */
#include <stdio.h>

#define NI __attribute__((noinline))

#define CHECK(T, EXPR, RUN) do {                                          \
        static const T s = (EXPR);                                        \
        T r = (RUN);                                                      \
        printf("%-40s %a %a\n", #EXPR, (double)s, (double)r);             \
    } while (0)

static NI double unsigned_of_short(volatile short *s) { return (double)(unsigned)*s; }
static NI float unsigned_of_char(volatile signed char *c) { return (float)(unsigned)*c; }
static NI double unsigned_short_of_short(volatile short *s) { return (double)(unsigned short)*s; }
static NI double unsigned_long_of_int(volatile int *i) { return (double)(unsigned long)*i; }
static NI float unsigned_char_of_int(volatile int *i) { return (float)(unsigned char)*i; }

int main(void)
{
    volatile unsigned long umax = 18446744073709551615ul, top = 9223372036854775808ul;
    volatile long l62 = 4611686018427387904l, l53 = 9007199254740993l;
    volatile unsigned u3 = 3000000000u;
    volatile int one = 1, i7 = 7, m9 = -9, i16m = 16777217;
    volatile float f3 = -3.0f, tiny = 1.000000013351432e-10f, fh = 0.5f;
    volatile double dm = -65504.0, d2 = -2.0, d1 = 1.0;

    /* Whole 64-bit values converted, not their low 32 bits. */
    CHECK(float, (float)9223372036854775808ul, (float)top);
    CHECK(float, (float)4611686018427387904l, (float)l62);
    CHECK(double, (double)18446744073709551615ul, (double)umax);
    CHECK(float, (float)16777217, (float)i16m);
    /* An integer operand converts to the other's type, by its signedness. */
    CHECK(double, 9007199254740993l + -3.0f, l53 + f3);
    CHECK(double, 9223372036854775808ul / -2.0, top / d2);
    CHECK(double, 3000000000u * 1.0f, u3 * (float)one);
    CHECK(int, 18446744073709551615ul > 1.0, umax > d1);
    CHECK(double, -16777217.0 + 18446744073709551615ul, -16777217.0 + umax);
    /* A conditional's value has its type: integer and float branches widen. */
    CHECK(double, 1ul ? 18446744073709551615ul : -65504.0, one ? umax : dm);
    CHECK(double, 7l + (0.30000001192092896f ? 1.000000013351432e-10f : 10000000000.0),
          i7 + (fh ? tiny : 10000000000.0));
    CHECK(double, ((int)7.25f) / (-7.25 ? 65504.0f : 4294967296.0), i7 / (one ? 65504.0f : 4294967296.0));

    /* At run time: a sign-extended narrow value, cast unsigned, then converted. */
    volatile short s = -9;
    volatile signed char c = -1;
    volatile int i = -2, j = 300;
    printf("%a %a %a %a %a\n", unsigned_of_short(&s), (double)unsigned_of_char(&c),
           unsigned_short_of_short(&s), unsigned_long_of_int(&i), (double)unsigned_char_of_int(&j));
    printf("%d\n", 4294967296.0f < (unsigned)(short)(3l * f3) + m9 * 0);
    return 0;
}
