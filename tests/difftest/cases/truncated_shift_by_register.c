/* The low 32 bits of a 64-bit shift by a register count of 32 to 63 are
 * not a 32-bit shift by that count: lsl counts mod 64 in its x form and
 * mod 32 in its w form. The arm peephole made (uint32_t)(1ULL << n) a
 * 32-bit lsl, so n = 32 gave 1 instead of 0. */
#include <stdio.h>
#include <stdint.h>

__attribute__((noinline)) uint64_t k1(uint64_t a, uint64_t b)
{
    int16_t s16 = (int16_t)(a ^ b);
    uint64_t w = a;
    w |= ((uint64_t)(uint32_t)((uint64_t)(1) << (((uint64_t)s16 + (uint64_t)(0)) & 63)));
    return w;
}
__attribute__((noinline)) uint32_t k2(uint64_t x, unsigned n) { return (uint32_t)(x << (n & 63)); }
__attribute__((noinline)) uint32_t k3(uint64_t x, unsigned n) { return (uint32_t)(x >> (n & 63)); }
__attribute__((noinline)) uint32_t k4(int64_t x, unsigned n) { return (uint32_t)(x >> (n & 63)); }
__attribute__((noinline)) uint16_t k5(uint64_t x, unsigned n) { return (uint16_t)(x << (n & 63)); }
__attribute__((noinline)) uint8_t k6(uint64_t x, unsigned n) { return (uint8_t)(x << (n & 63)); }
__attribute__((noinline)) uint32_t k7(uint64_t x, unsigned n) { return (uint32_t)((x << (n & 63)) + 1); }
__attribute__((noinline)) uint32_t k8(uint64_t x) { return (uint32_t)(x << 33); }
__attribute__((noinline)) uint32_t k9(uint64_t x, unsigned n) { uint32_t r = 7; r ^= x << (n & 63); return r; }

int main(void)
{
    static const uint64_t v[] = { 0, 1, 3, 0xff, 0x80000001, 0xffffffffffffffffULL, 0x123456789abcdefULL };
    static const unsigned ns[] = { 0, 1, 7, 31, 32, 33, 40, 63, 64, 95 };
    unsigned long long h = 0;
    for (int i = 0; i < 7; i++) {
        h = h * 31 + k8(v[i]);
        for (int j = 0; j < 10; j++) {
            unsigned n = ns[j];
            h = h * 31 + k1(v[i], n) + k2(v[i], n) + k3(v[i], n) + k4((int64_t)v[i], n);
            h = h * 31 + k5(v[i], n) + k6(v[i], n) + k7(v[i], n) + k9(v[i], n);
            if (i == 1)
                printf("%u %u %u %u %u %u %u %u\n", k2(1, n), k3(v[6], n), k4(-8, n), k5(1, n), k6(1, n), k7(1, n),
                       k9(1, n), (unsigned)k1(n, 0));
        }
    }
    printf("%llx\n", h);
    return 0;
}
