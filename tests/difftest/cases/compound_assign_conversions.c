/* Compound assignment and ++/-- convert as C does. Found by the lvalue fuzzer:
 * - a narrow signed lvalue with an unsigned operand (c /= 200u, i /= 2ul,
 *   i %= n with a size_t n): it converts to int and then to the common
 *   type, sign-extending; ccc zero-extended it from its own width, so
 *   int i = -3; i /= 2ul gave 2147483646;
 * - a bit-field with a floating right-hand side (s.bf += 2.5): the double's
 *   bits went into integer arithmetic; and the operation's signedness was
 *   the storage type's, though a narrow unsigned bit-field promotes to int;
 * - the same on the right: an unsigned lvalue with a negative int operand
 *   (u /= -100) divides by 2^32 - 100; ccc extended -100 straight to 64
 *   bits and divided by 2^64 - 100;
 * - a _Bool target takes 1 for any nonzero result: b += 0.5, b += 256 and
 *   b += 0x100000000 stored 0 (narrowed to 8 bits before the test), and
 *   ++ on a _Bool bit-field holding 1 stored 0. */
#include <stdio.h>

struct S { _Bool b1 : 1; unsigned u3 : 3; int s5 : 5; unsigned u31 : 31; unsigned long long ux40 : 40; long long sx33 : 33; };
struct B { _Bool b; int i : 5; unsigned u : 3; };

__attribute__((noinline)) static void bitfields(struct S *p, double d, float f) {
    p->u3 += 2.5; p->s5 += 2.5; p->u31 += 2.5; p->ux40 += 2.5; p->sx33 += 2.5; p->b1 += 0.5;
    printf("a %d %u %d %u %llu %lld\n", p->b1, p->u3, p->s5, p->u31, (unsigned long long)p->ux40, (long long)p->sx33);
    p->u3 *= d; p->s5 -= d; p->u31 *= f; p->ux40 *= d; p->sx33 -= f; p->b1 *= d;
    printf("b %d %u %d %u %llu %lld\n", p->b1, p->u3, p->s5, p->u31, (unsigned long long)p->ux40, (long long)p->sx33);
    double r = (p->s5 += 1.25);
    double q = (p->ux40 /= 0.5);
    printf("c %g %g %d %llu\n", r, q, p->s5, (unsigned long long)p->ux40);
    p->b1 = 1; p->b1++; printf("d %d ", p->b1);
    int v = ++p->b1; printf("%d %d ", v, p->b1);
    p->b1 = 0; p->b1--; printf("%d ", p->b1);
    v = p->b1--; printf("%d %d\n", v, p->b1);
}

__attribute__((noinline)) static void bools(_Bool *pb, struct B *s, double d) {
    _Bool b = 0;
    b += 0.5; printf("%d ", b);
    b = 0; b -= 0.25; printf("%d ", b);
    b = 1; b *= 0.5; printf("%d ", b);
    b = 1; b *= d; printf("%d ", b);
    *pb = 0; *pb += d; printf("%d ", *pb);
    s->b = 0; s->b += 0.125; printf("%d ", s->b);
    b = 1; b -= 1; printf("%d ", b);
    b = 0; b += 256; printf("%d ", b);
    b = 0; b += 0x100000000LL; printf("%d\n", b);
    s->i = -3; s->i /= 2u; printf("%d ", s->i);
    s->u = 5; s->u /= -2; printf("%u ", s->u);
    s->i = -8; s->i >>= 1; printf("%d ", s->i);
    s->u = 7; s->u += 3000000000u; printf("%u ", s->u);
    s->i = 3; s->i -= 0.5f; printf("%d\n", s->i);
}

__attribute__((noinline)) static void narrow(signed char c0, short h0, int i0, long l0, unsigned u0) {
    signed char c; short h; int i; long l; unsigned u; unsigned char uc;
    c = c0; c /= 200u; printf("%d ", c);
    c = c0; c %= 7u; printf("%d ", c);
    c = c0; c /= 3ul; printf("%d ", c);
    c = c0; c >>= 1; printf("%d ", c);
    h = h0; h /= 1000u; printf("%d ", h);
    h = h0; h %= 9ul; printf("%d ", h);
    i = i0; i /= 2ul; printf("%d ", i);
    i = i0; i %= 1000000007ul; printf("%d ", i);
    i = i0; i /= sizeof(int); printf("%d ", i);
    i = i0; i /= 3u; printf("%d ", i);
    l = l0; l /= 3ul; printf("%ld ", l);
    u = u0; u /= c0; printf("%u ", u);
    uc = 200; uc /= c0; printf("%d ", uc);
    unsigned __int128 w = 10; w += i0; printf("%llu ", (unsigned long long)(w >> 64));
    __int128 sw = -5; sw /= 2ul; printf("%lld\n", (long long)sw);
}

__attribute__((noinline)) static void unsigned_lhs(unsigned u0, int n, short h, signed char c) {
    unsigned u;
    u = u0; u /= -100; printf("%u ", u);
    u = u0; u %= -7; printf("%u ", u);
    u = u0; u /= n; printf("%u ", u);
    u = u0; u %= n; printf("%u ", u);
    u = u0; u /= h; printf("%u ", u);
    u = u0; u /= c; printf("%u ", u);
    u = u0; u += n; printf("%u ", u);
    struct B b = {0, 0, 7};
    b.u /= n; printf("%u\n", b.u);
}

int main(void) {
    unsigned_lhs(4294967290u, -100, -2, -1);
    unsigned_lhs(4294967196u, -3, -30000, -128);
    unsigned_lhs(5u, 3, 2, 1);
    struct S s = {0};
    bitfields(&s, 1.5, 2.5f);
    struct S t = {1, 5, -3, 100, 1000, -7};
    bitfields(&t, 3.0, 0.5f);
    _Bool b;
    struct B sb;
    bools(&b, &sb, 0.0625);
    narrow(-10, -1234, -3, -7, 4000000000u);
    narrow(-128, -32768, -2147483647 - 1, -1, 7u);
    narrow(100, 999, 12345, 99, 0x80000000u);
    return 0;
}
