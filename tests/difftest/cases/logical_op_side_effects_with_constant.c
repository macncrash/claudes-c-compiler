/* x || 1 is 1 and x && 0 is 0, but x is still evaluated: the constant
 * folder returned the constant whenever the right operand decided it, and
 * dropped the left operand's side effects (an increment, a call). */
#include <stdio.h>

static int cnt;
__attribute__((noinline)) static int f(void) { return ++cnt; }

__attribute__((noinline)) long k1(int *q) { return (*q)++ || 1; }
__attribute__((noinline)) long k2(int *q) { return (*q)++ && 0; }
__attribute__((noinline)) long k3(void) { return f() || 1; }
__attribute__((noinline)) long k4(void) { return f() && 0; }
__attribute__((noinline)) long k5(void) { return (f(), 1); }
__attribute__((noinline)) long k6(void) { return f() * 0; }
__attribute__((noinline)) long k7(void) { return f() & 0; }
__attribute__((noinline)) long k8(void) { return f() ? 3 : 3; }
__attribute__((noinline)) long k9(void) { return (f() || 1) ? 4 : 5; }
__attribute__((noinline)) long k10(void) { if (f() && 0) return 6; return 7; }
__attribute__((noinline)) long k11(void) { int r = 0; while (f() && 0) r++; return r; }
__attribute__((noinline)) long k12(void) { return !(f() || 1); }
__attribute__((noinline)) long k13(void) { return (f() >= 0 || 1) + (f() < 0 && 0); }
__attribute__((noinline)) long k14(void) { return sizeof(f() || 1) + (1 || f()) + (0 && f()); }
__attribute__((noinline)) long k15(int q, int s)
{
    /* the shape the fuzzer found: a ?: whose condition is (x++ & 1 || 1) */
    long r = (q++ & 1 || 1) ? s : 1 ? -128 : 7;
    return r * 100 + q;
}

int main(void)
{
    int q = 0;
    long r[16];
    r[0] = k1(&q); r[1] = k2(&q); r[2] = k3(); r[3] = k4(); r[4] = k5(); r[5] = k6(); r[6] = k7(); r[7] = k8();
    r[8] = k9(); r[9] = k10(); r[10] = k11(); r[11] = k12(); r[12] = k13(); r[13] = k14(); r[14] = k15(4, 5);
    for (int i = 0; i < 15; i++) printf("%ld ", r[i]);
    printf("| q=%d cnt=%d\n", q, cnt);
    return 0;
}
