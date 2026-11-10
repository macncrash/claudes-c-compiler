/* A 64-bit and/or/xor of extended 32-bit values was done as a 32-bit op,
 * which leaves the high half zero. That is the result only when every
 * operand had a zero high half: not for sign-extended values (the xor of
 * two has the sign in its high half), not for a sign- and a zero-extended
 * one, and not with a negative constant (zext(x) | -1). Found as a known
 * zero (a uint16_t shifted right 18) xored with a sign-extended int. */
#include <stdint.h>
#include <stdio.h>

__attribute__((noinline)) uint64_t xor_known_zero(uint64_t pa, uint64_t pb)
{
    uint16_t b = (uint16_t)pb;
    uint16_t z = (uint16_t)pa;
    z >>= 18;
    return (uint64_t)(int32_t)(-(uint32_t)b) ^ ((uint64_t)z << 1);
}

__attribute__((noinline)) uint64_t xor_signed(int32_t a, int32_t b)
{
    return (uint64_t)(int64_t)a ^ (uint64_t)(int64_t)b;
}

__attribute__((noinline)) int64_t or_signed(int32_t a, int32_t b)
{
    return (int64_t)a | (int64_t)b;
}

__attribute__((noinline)) uint64_t xor_mixed(int32_t a, uint32_t b)
{
    return (uint64_t)(int64_t)a ^ (uint64_t)b;
}

__attribute__((noinline)) uint64_t and_mixed(int8_t a, uint32_t b)
{
    return (uint64_t)a & ((uint64_t)b | 0x80000000u);
}

__attribute__((noinline)) uint64_t or_minus_one(uint32_t a)
{
    return (uint64_t)a | (uint64_t)-1;
}

__attribute__((noinline)) uint64_t xor_minus_two(uint32_t a)
{
    return (uint64_t)a ^ (uint64_t)-2;
}

__attribute__((noinline)) uint64_t xor_unsigned(uint32_t a, uint32_t b)
{
    return (uint64_t)a ^ (uint64_t)b;
}

int main(void)
{
    int32_t v[] = { 0, 1, -1, 3, -3, 0x7fffffff, -0x7fffffff - 1, 0x12345678, -0x12345678 };
    unsigned n = sizeof v / sizeof v[0];
    for (unsigned i = 0; i < n; i++) {
        printf("%llx\n", (unsigned long long)xor_known_zero(0, (uint64_t)v[i]));
        for (unsigned j = 0; j < n; j += 2) {
            printf("%llx %llx %llx %llx %llx\n",
                   (unsigned long long)xor_signed(v[i], v[j]), (unsigned long long)or_signed(v[i], v[j]),
                   (unsigned long long)xor_mixed(v[i], (uint32_t)v[j]),
                   (unsigned long long)and_mixed((int8_t)v[i], (uint32_t)v[j]),
                   (unsigned long long)xor_unsigned((uint32_t)v[i], (uint32_t)v[j]));
        }
        printf("%llx %llx\n", (unsigned long long)or_minus_one((uint32_t)v[i]),
               (unsigned long long)xor_minus_two((uint32_t)v[i]));
    }
    return 0;
}
