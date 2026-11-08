/* Integer conversions where expressions are folded: each folded in a static
 * initializer, in an enum, and computed at run time from volatile copies. */
#include <stdio.h>

#define NI __attribute__((noinline))

/* Each line: one expression three ways. */
#define CHECK(T, EXPR, RUN) do {                                          \
        static const T s = (EXPR);                                        \
        enum { E = (EXPR) != 0 };                                         \
        T r = (RUN);                                                      \
        printf("%-44s %llx %llx %d\n", #EXPR, (unsigned long long)s,      \
               (unsigned long long)r, (int)E);                            \
    } while (0)

int main(void)
{
    volatile unsigned u1 = 1338556541u, u4 = 4000000000u, nine = 9u, big = 1549844286u;
    volatile int one = 1, m1 = -1, i200 = 200, i2 = 2, i0 = 0;
    volatile long l7 = 7, lbig = 5653603213087223958l;
    volatile long long ll256 = 256ll, ll1 = 1ll, ll32767 = 32767ll;
    volatile unsigned long ul0 = 0ul, ulbig = 11795276161944446212ul;

    /* -x and ~x of unsigned int wrap at 32 bits. */
    CHECK(long long, -1338556541u, -u1);
    CHECK(long long, ~4000000000u, ~u4);
    CHECK(long long, 1 ? -1338556541u : 1ll, one ? -u1 : ll1);
    /* Narrow unsigned types promote to int, signed. */
    CHECK(int, (unsigned char)200 > (signed char)-1, (unsigned char)i200 > (signed char)m1);
    CHECK(long, 7l ^ ~(unsigned char)255, l7 ^ ~(unsigned char)(i200 + 55));
    CHECK(int, (unsigned short)65535 < -1, (unsigned short)(i200 * 0 + 65535) < m1);
    /* An unsigned operand at least as wide as the signed one wins. */
    CHECK(int, 0ul <= ~1, ul0 <= ~one);
    CHECK(int, (5653603213087223958l ^ 2147483647) <= (11795276161944446212ul ^ 256ll),
          (lbig ^ 2147483647) <= (ulbig ^ ll256));
    CHECK(int, -1ll < 0ul, (m1 + 0ll) < ul0);
    CHECK(int, -1l < 0u, (m1 + 0l) < (unsigned)i0);
    /* A conditional's value has the type both branches convert to. */
    CHECK(int, (1 ? ((signed char)2147483647 >> 26) : 2147483647u) <= 32767ll,
          (one ? ((signed char)(m1) >> 26) : 2147483647u) <= ll32767);
    CHECK(long long, 0 ? 1ll : 4000000000u, i0 ? ll1 : u4);
    CHECK(long long, 1 ? -1 : 0u, one ? m1 : (unsigned)i0);
    /* Casts to _Bool test against zero. */
    CHECK(long, 2147483647l ^ (_Bool)(1549844286u % 9u), 2147483647l ^ (_Bool)(big % nine));
    CHECK(int, (_Bool)256 + (_Bool)0.5 + (_Bool)-0.0, (_Bool)(i200 + 56) + (_Bool)(one * 0.5) + (_Bool)(m1 * 0.0));
    CHECK(int, (_Bool)2 * 10 + (int)(_Bool)(unsigned char)256, (_Bool)i2 * 10 + (int)(_Bool)(unsigned char)(i200 + 56));
    return 0;
}
