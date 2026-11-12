/* A compound assignment of a floating value to an unsigned __int128
 * (y *= 0.75, y += 2.75f) converted y to the floating type and back
 * through unsigned 64-bit: the value was truncated and the result was 0.
 * Found by a conversion fuzzer. */
#include <stdio.h>

__attribute__((noinline)) unsigned __int128 idu(unsigned __int128 x) { return x; }
__attribute__((noinline)) __int128 ids(__int128 x) { return x; }
__attribute__((noinline)) double idd(double x) { return x; }
__attribute__((noinline)) float idf(float x) { return x; }
__attribute__((noinline)) long double idl(long double x) { return x; }

static void p(const char *n, unsigned __int128 v)
{
    printf("%s %llx %llx\n", n, (unsigned long long)(v >> 64), (unsigned long long)v);
}

int main(void)
{
    unsigned __int128 a = idu(30); a *= idd(0.75); p("a", a);
    unsigned __int128 b = idu(1); b += idf(2.75f); p("b", b);
    unsigned __int128 c = idu((unsigned __int128)1 << 100); c *= idd(0.5); p("c", c);
    unsigned __int128 d = idu(((unsigned __int128)1 << 127) + 12345); d -= idl(1.0L); p("d", d);
    unsigned __int128 e = idu(7); e /= idf(2.0f); p("e", e);
    __int128 f = ids(-5); f += idd(2.5); p("f", (unsigned __int128)f);
    unsigned __int128 g = idu(10);
    g = g * idd(0.5);
    p("g", g);
    return 0;
}
