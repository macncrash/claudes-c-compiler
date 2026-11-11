/* A value set before a computed goto and also on a fallthrough path into
 * the target label. The arm peephole's argument-copy coalescing built its
 * CFG without the br's edges, took the label for entered by fallthrough
 * only, and renamed the value's register to x0 across it: jumping straight
 * to the label read the jump address instead of the value. */
#include <stdio.h>

static int tr;
__attribute__((noinline)) static int T(int v) { tr = tr * 31 + v + 1; return v; }
static volatile int vv = 10;

__attribute__((noinline)) int g1(int s)
{
    int h = 0;
    static void *const t[] = { &&A, &&B };
    goto *t[s];
A:
    h = T(10);
B:
    return h + 3;
}

__attribute__((noinline)) int g2(int s)
{
    int h = 0;
    static void *const t[] = { &&A, &&B };
    goto *t[s];
A:
    h = vv;
B:
    return h + 3;
}

__attribute__((noinline)) int g3(int s, int x)
{
    int h = x * 7;
    static void *const t[] = { &&A, &&B };
    goto *t[s];
A:
    h = T(10);
B:
    return h + 3;
}

__attribute__((noinline)) int g4(int s, int x)
{
    int h = x;
    void *p = s ? &&B : &&A;
    goto *p;
A:
    h = T(h + 10);
B:
    return h + 3;
}

__attribute__((noinline)) int g5(int s)
{
    int h = 0;
    {
        static void *const t4[] = { &&C4_0, &&C4_1 };
        goto *t4[s % 2];
    C4_0:
        {
            static void *const t5[] = { &&C5_0, &&C5_1 };
            (void)t5;
        C5_0:
            h = T(10);
        C5_1:
            h = T(h + 20);
        }
    C4_1:
        h = T(h + 3);
    }
    return T(h);
}

/* Phi elimination put the copies for the computed goto's edge back to L
 * before the br, so they also overwrote y's value from L on the paths to
 * A and B, which still read it once (y - 1) + 1 had folded to y. */
__attribute__((noinline)) unsigned g6(int s)
{
    unsigned short y = 0;
    switch (s) {
    default:
        {
            unsigned g = 0;
        L:
            T(0);
            y -= 1;
            if (++g < 1) goto L;
        }
    }
    static void *const t[] = { &&A, &&B, &&C, &&L };
    goto *t[s & 1];
A:
    for (unsigned l = 0; l < 2; l++) {
    }
B:
    if (1) {
        y += 1;
        T(0);
    }
C:
    T(0);
    return y;
}

int main(void)
{
    for (int s = 0; s < 2; s++) {
        tr = 0;
        int a = g1(s), b = g2(s), c = g3(s, 5), d = g4(s, 5), e = g5(s);
        unsigned f = g6(s);
        printf("%d %d %d %d %d %u %d\n", a, b, c, d, e, f, tr);
    }
    return 0;
}
