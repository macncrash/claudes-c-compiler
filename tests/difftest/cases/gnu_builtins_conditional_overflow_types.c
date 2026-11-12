/* GNU extension bugs found by a builtin/extension fuzzer:
 * - x ?: y did not take the usual arithmetic conversions: (-1) ?: u with
 *   u unsigned was a 64-bit -1, not 4294967295 (also when folded);
 * - __builtin_types_compatible_p(typeof(({ ...; v + 1; })), T) did not see
 *   the statement expression's own locals and compared against int;
 * - __builtin_bswap16 had type unsigned int (sizeof 4), not uint16_t;
 * - __builtin_*_overflow with an unsigned result as wide as the operands
 *   called LONG_MAX + 1 into unsigned long long an overflow, and 64-bit
 *   operands into a narrower result were computed in (wrapping) i64;
 * - a constant cast from a 32-bit unsigned to __int128 zero-extended from
 *   64 bits (0xffffffff held as -1 became 2^64 - 1). */
#include <stdio.h>
#include <limits.h>

__attribute__((noinline)) void cond(int c, unsigned d)
{
    unsigned long long a = (unsigned long long)((-1) ?: (d));
    unsigned long long b = (unsigned long long)(c ?: d);
    unsigned long long e = (unsigned long long)(0 ?: d);
    long long f = (long long)((-1) ?: (long long)d);
    printf("cond %llx %llx %llx %lld %zu\n", a, b, e, f, sizeof((-1) ?: d));
}

__attribute__((noinline)) void compat(void)
{
    int a = __builtin_types_compatible_p(__typeof__(({ __typeof__(1ll) v = 1; v + 1; })), long long);
    int b = __builtin_types_compatible_p(__typeof__(({ long long v = 1; (__typeof__(v))1; })), long long);
    int c = __builtin_types_compatible_p(__typeof__(({ short v = 1; v; })), short);
    __auto_type w = __builtin_bswap16(0x1234);
    printf("compat %d %d %d %zu %x %zu %zu\n", a, b, c, sizeof w, w, sizeof __builtin_bswap32(1), sizeof __builtin_bswap64(1));
}

__attribute__((noinline)) void ovf(long x, unsigned short y, long long z, unsigned v)
{
    unsigned long long r1;
    _Bool o1 = __builtin_add_overflow(x, y, &r1);
    short r2;
    _Bool o2 = __builtin_mul_overflow(z, (int)y, &r2);
    signed char r3;
    _Bool o3 = __builtin_sub_overflow((unsigned long)z, (unsigned char)y, &r3);
    unsigned r4;
    _Bool o4 = __builtin_add_overflow(x, v, &r4);
    unsigned long long r5;
    _Bool o5 = __builtin_sub_overflow(1L, (unsigned)__builtin_bswap32((unsigned)-1), &r5);
    printf("ovf %d %llx | %d %d | %d %d | %d %x | %d %llx\n", o1, r1, o2, r2, o3, r3, o4, r4, o5, r5);
}

int main(void)
{
    cond(-1, 5);
    cond(0, 7);
    compat();
    ovf(LONG_MAX, 1, LLONG_MAX, 3);
    ovf(-5, 3, 0x10001, 0xffffffffu);
    ovf(LONG_MIN, 65535, -1, 1);
    ovf(100, 200, 2, 4000000000u);
    return 0;
}
