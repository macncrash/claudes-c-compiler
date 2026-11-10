/* Integer operations on runtime values, each once wrong:
 * - (int16_t) of a uint16_t is the same register, its high bits zero, and
 *   a right shift or compare narrowed to 16 bits read them: v >> 15 of
 *   (int16_t)0x8000 was 1, and (int16_t)0xfffe < 1 was false;
 * - (x << 12) << 22 on a uint32_t, made one 64-bit shift by 34 and then
 *   narrowed to 32 bits, shifted by 34 mod 32;
 * - c ? a : b takes the usual arithmetic conversion of the branches' C
 *   types: b ? b : (5u | 1) is unsigned int (an int literal's IR type was
 *   64 bits), c ? -1 : 0x80000000 is unsigned, and an unsigned bit-field
 *   of 5 bits is int;
 * - (int64_t) of a uint32_t divided by a constant used the signed 32-bit
 *   expansion, which read 0xff000200 as negative. */
#include <stdint.h>
#include <stdio.h>

__attribute__((noinline)) uint64_t shr_from_unsigned(uint64_t p)
{
    uint16_t a = (uint16_t)p;
    int16_t v = (int16_t)a;
    v >>= 15;
    return (uint64_t)v;
}

__attribute__((noinline)) uint64_t shr3_from_unsigned(uint64_t p)
{
    uint16_t a = (uint16_t)p;
    int16_t v = (int16_t)a;
    return (uint64_t)(int16_t)(v >> 3);
}

__attribute__((noinline)) uint64_t lt_from_unsigned(uint64_t pa, uint64_t pb)
{
    uint16_t a = (uint16_t)pa, b = (uint16_t)pb;
    return (uint64_t)(((int16_t)b) < ((int64_t)1 - (int64_t)(a & 0)));
}

__attribute__((noinline)) uint64_t shift_by_34(uint64_t pa, uint64_t pb)
{
    uint8_t a = (uint8_t)pa;
    int32_t b = (int32_t)pb;
    uint16_t v0 = (uint16_t)a;
    int64_t v2 = (int64_t)((int32_t)((uint32_t)(b / (int32_t)256) + (uint32_t)((int32_t)v0)));
    v2 |= (int64_t)((uint32_t)((uint32_t)v0 << 12) << 22);
    return (uint64_t)v2;
}

struct S { unsigned bf : 5; unsigned bf32 : 32; };

__attribute__((noinline)) uint64_t cond_unsigned(uint64_t p)
{
    int16_t b = (int16_t)p;
    return (uint64_t)(b ? b : ((uint32_t)65536U | 1));
}

__attribute__((noinline)) int64_t cond_literals(int c)
{
    return (int64_t)(c ? -1 : 0x80000000);
}

__attribute__((noinline)) int64_t cond_bitfields(int c, struct S s)
{
    return (int64_t)(c ? s.bf : -1) + (int64_t)(c ? s.bf32 : -1);
}

__attribute__((noinline)) int64_t div_widened_unsigned(uint32_t u)
{
    int64_t v = (int64_t)u;
    return v / 7 + (int64_t)u % 10;
}

int main(void)
{
    uint64_t v[] = { 0, 1, 2, 0x7fff, 0x8000, 0xfffe, 0xffff, 0x10000, 0xff000200u, 0xffffffffu,
                     0x7fffffffu, 0x123456789abcdef0ull };
    unsigned n = sizeof v / sizeof v[0];
    struct S s = { 3, 9 };
    for (unsigned i = 0; i < n; i++) {
        printf("%llx %llx %llx %llx %llx %lld\n", (unsigned long long)shr_from_unsigned(v[i]),
               (unsigned long long)shr3_from_unsigned(v[i]), (unsigned long long)lt_from_unsigned(0, v[i]),
               (unsigned long long)shift_by_34(v[i], v[(i + 3) % n]), (unsigned long long)cond_unsigned(v[i]),
               (long long)div_widened_unsigned((uint32_t)v[i]));
    }
    for (int c = 0; c < 2; c++)
        printf("%lld %lld\n", (long long)cond_literals(c), (long long)cond_bitfields(c, s));
    return 0;
}
