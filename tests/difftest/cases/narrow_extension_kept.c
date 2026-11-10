/* Extensions that a narrowing or elision dropped, each once wrong:
 * - a char product narrowed to an 8-bit multiply leaves the register
 *   above 8 bits unspecified, where the truncating cast had extended it,
 *   and !v of the promoted local compared the register with 0;
 * - an int64_t division by INT64_MAX multiplies the dividend widened to
 *   128 bits, and that widening of an int8_t was taken for one its
 *   register already held, with a high half of 0;
 * - (uint32_t)b | 0 for a signed char b, narrowed to a 32-bit or that
 *   simplify then removed, left b sign-extended to 64 bits;
 * - x % 1 was masked with an `and #0`, which no logical immediate holds;
 * - (int32_t)a of a uint32_t parameter, tested by a select: the truncation
 *   was dropped as read only in its low bits, through a cast that is the
 *   register as it is, and the select tested all 64;
 * - a uint64_t division by 0xffffffff of a sign-extended char used the
 *   32-bit expansion, as if a char's register were zero above 32 bits. */
#include <stdint.h>
#include <stdio.h>

__attribute__((noinline)) uint64_t char_product(uint64_t pa, uint64_t pb)
{
    uint32_t a = (uint32_t)pa;
    int64_t b = (int64_t)pb;
    int8_t v0 = (int8_t)(((uint32_t)a << 24) - ((int16_t)32767 / (int32_t)255));
    v0 *= (int8_t)(((int64_t)((uint64_t)(uint16_t)65535 * (uint64_t)b)) / (int64_t)256LL);
    return (uint64_t)(!v0) | ((uint64_t)(v0 == 0) << 1);
}

__attribute__((noinline)) uint64_t div_by_int64_max(uint64_t pb)
{
    int64_t v0 = (int64_t)(int8_t)pb;
    v0 %= (int64_t)9223372036854775807LL;
    v0 >>= 27;
    return (uint64_t)(v0 / (uint32_t)16U);
}

__attribute__((noinline)) uint64_t quot_by_int64_max(uint64_t pb)
{
    int64_t v0 = (int64_t)(int16_t)pb;
    v0 /= (int64_t)9223372036854775807LL;
    return (uint64_t)v0;
}

__attribute__((noinline)) uint64_t unsigned_or_zero(uint64_t pa, uint64_t pb)
{
    int8_t a = (int8_t)pa, b = (int8_t)pb;
    int64_t v0 = (int64_t)((uint32_t)0U >> (b & 31));
    uint16_t v1 = (uint16_t)((uint64_t)((int64_t)((uint64_t)v0 * (uint64_t)v0)) << 57);
    uint64_t v2 = (uint64_t)a;
    v2 *= (uint64_t)(((uint32_t)b) | (v1 ? (uint8_t)255 : v0));
    return v2 >> 62;
}

__attribute__((noinline)) uint64_t rem_one(uint64_t pa, volatile uint64_t one)
{
    uint16_t b = (uint16_t)one;
    return (pa + 3) % one + (uint64_t)(b + pa) % ((uint64_t)((uint32_t)((b - b) >> 2) * (uint32_t)pa) | 1);
}

__attribute__((noinline)) uint64_t select_on_int(uint64_t pa, uint64_t pb)
{
    uint32_t a = (uint32_t)pa;
    uint16_t b = (uint16_t)pb;
    int16_t v0 = (int16_t)(((int64_t)((uint64_t)(((int64_t)b) % (int64_t)-32768LL) - (uint64_t)(((int32_t)a) ? (uint32_t)1000U : a))) | (uint64_t)16ULL);
    v0 -= (int16_t)((a - a) ^ ((uint8_t)255 < (uint64_t)490501ULL));
    return (uint64_t)((uint8_t)v0) ^ ((uint64_t)v0 << 1);
}

__attribute__((noinline)) uint64_t char_by_u32_max(uint64_t pa, uint64_t pb)
{
    int16_t a = (int16_t)pa;
    int8_t b = (int8_t)pb;
    int64_t v0 = (int64_t)((!(int8_t)127) ^ b);
    return (uint64_t)((uint16_t)((uint64_t)(v0 / (uint64_t)4294967295ULL) % ((uint64_t)((int8_t)a) | 1)));
}

int main(void)
{
    uint64_t v[] = { 0, 1, 2, 7, 0x7f, 0x80, 0xff, 0x100, 0x8000, 0xfff9, 0x12345678, 0xfffffffffffffff9ull,
                     0x8000000000000000ull, 0x7fffffffffffffffull };
    unsigned n = sizeof v / sizeof v[0];
    for (unsigned i = 0; i < n; i++)
        for (unsigned j = 0; j < n; j += 3)
            printf("%llx %llx %llx %llx %llx\n", (unsigned long long)char_product(v[i], v[j]),
                   (unsigned long long)div_by_int64_max(v[j]), (unsigned long long)quot_by_int64_max(v[j]),
                   (unsigned long long)unsigned_or_zero(v[i], v[j]), (unsigned long long)rem_one(v[i], 1));
    for (unsigned i = 0; i < n; i++)
        for (unsigned j = 0; j < n; j++)
            printf("%llx %llx\n", (unsigned long long)select_on_int(v[i] << 32 | v[j], v[j]),
                   (unsigned long long)char_by_u32_max(v[i], v[j]));
    return 0;
}
