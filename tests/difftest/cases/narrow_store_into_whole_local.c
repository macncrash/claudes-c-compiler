/* A local read whole after a narrower store into it: a union member
 * (u.q = v; u.c = c; return u.q), or the first member of a struct copied
 * whole (x = *p; x.c = 9; *p = x, the copies made 8-byte loads and stores
 * and the member's address folded to the local's). mem2reg promoted such a
 * local to one value, the last store's, so the byte stood for the whole: a
 * 47-bit field after it read 0, the union returned c. Locals accessed at
 * two sizes now stay in memory. */
#include <stdio.h>
#include <string.h>
struct S0 { unsigned char m0; unsigned long long b1 : 47; };
struct S1 { unsigned short h; unsigned char c; unsigned char d; unsigned w; };
union U { unsigned long long q; unsigned char c; unsigned short h; unsigned i; };
struct S0 g;
struct S1 g1;

__attribute__((noinline)) unsigned long long un(unsigned long long v, unsigned char c)
{
    union U u;
    u.q = v;
    u.c = c;
    return u.q;
}
__attribute__((noinline)) unsigned long long un2(unsigned long long v, unsigned short h)
{
    union U u;
    u.q = v;
    u.h = h;
    unsigned r = u.i;
    u.c = 1;
    return u.q + r;
}
__attribute__((noinline)) void cp(struct S0 *p) { struct S0 x = *p; x.m0 = 9; *p = x; }
__attribute__((noinline)) void glob(void) { struct S0 x = g; x.m0 = 3; g = x; }
__attribute__((noinline)) void glob_loop(int n)
{
    for (int i = 0; i < n; i++) {
        struct S0 x = g;
        x.m0 += 1;
        g = x;
    }
}
__attribute__((noinline)) void s1(struct S1 *p, int n)
{
    struct S1 x = *p;
    for (int i = 0; i < n; i++) {
        x.h += 3;
        x.d ^= 0x5a;
    }
    *p = x;
}
__attribute__((noinline)) unsigned long long pun(struct S1 *p)
{
    struct S1 x = *p;
    x.c = 0xee;
    unsigned long long q;
    memcpy(&q, &x, 8);
    return q;
}

int main(void)
{
    printf("%llx %llx\n", un(0x1122334455667788ULL, 0xab), un2(0x1122334455667788ULL, 0xbeef));
    g.m0 = 1;
    g.b1 = 0x123456789aULL;
    glob();
    printf("%x %llx\n", g.m0, (unsigned long long)g.b1);
    cp(&g);
    printf("%x %llx\n", g.m0, (unsigned long long)g.b1);
    glob_loop(5);
    printf("%x %llx\n", g.m0, (unsigned long long)g.b1);
    struct S1 v = { 0x1234, 0x56, 0x78, 0x9abcdef0 };
    s1(&v, 3);
    printf("%x %x %x %x\n", v.h, v.c, v.d, v.w);
    printf("%llx\n", pun(&v));
    return 0;
}
