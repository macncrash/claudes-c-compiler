/* Compare-and-swap and fetch-and-op on 8- and 16-bit signed objects, found by
 * a single-threaded atomics fuzzer:
 * - the exclusive byte/halfword load zero-extends, but the expected value was
 *   compared in all 32 bits, so a negative expected value never matched and
 *   the swap never happened;
 * - the old value returned for a signed type was not sign-extended. */
#include <stdio.h>
#include <stdatomic.h>

__attribute__((noinline)) int cas_sc(signed char v)
{
    signed char a = v;
    signed char r = __sync_val_compare_and_swap(&a, v, (signed char)-2);
    return r * 1000 + a;
}

__attribute__((noinline)) int bcas_ss(short v)
{
    short a = v;
    int ok = __sync_bool_compare_and_swap(&a, v, (short)-300);
    return ok * 100000 + a;
}

__attribute__((noinline)) int cx_sc(signed char v)
{
    signed char a = v, e = v;
    int ok = __atomic_compare_exchange_n(&a, &e, (signed char)5, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return ok * 1000 + a;
}

__attribute__((noinline)) int c11_cx(_Atomic short *p, short e)
{
    int ok = atomic_compare_exchange_strong(p, &e, (short)-7);
    return ok * 100000 + e;
}

__attribute__((noinline)) long long old_values(signed char c, short s)
{
    signed char a = c;
    short b = s;
    long long r = __sync_fetch_and_add(&a, 1);
    r = r * 100000 + __atomic_fetch_or(&b, 1, __ATOMIC_RELAXED);
    r = r * 1000 + __atomic_exchange_n(&a, (signed char)-1, __ATOMIC_SEQ_CST);
    r = r * 1000 + __sync_val_compare_and_swap(&a, (signed char)-1, (signed char)3);
    return r;
}

int main(void)
{
    _Atomic short g = -32768;
    printf("%d %d %d\n", cas_sc(-61), cas_sc(-128), cas_sc(7));
    printf("%d %d\n", bcas_ss(-1), bcas_ss(-32768));
    printf("%d %d\n", cx_sc(-100), cx_sc(100));
    printf("%d", c11_cx(&g, -32768));
    printf(" %d", c11_cx(&g, -32768));
    printf(" %d\n", (int)g);
    printf("%lld %lld\n", old_values(-5, -1234), old_values(-128, -32768));
    return 0;
}
