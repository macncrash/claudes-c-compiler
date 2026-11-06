/* Rotates written with two shifts become one instruction, and code that merely
 * looks like a rotate must not. The trap is a 32-bit shift or'd with a 64-bit
 * one: (uint32_t)x << 25 | x >> 7 compiles to the same two instructions as a
 * 32-bit rotate, and differs from one when x has upper bits. */
#include <stdio.h>
#include <stdint.h>

#define NI __attribute__((noinline))

static NI uint32_t rotr32_7(uint32_t w) { return (w >> 7) | (w << 25); }
static NI uint32_t rotl32_13(uint32_t w) { return (w << 13) | (w >> 19); }
static NI uint64_t rotr64_51(uint64_t w) { return (w >> 51) | (w << 13); }
static NI uint64_t rotl64_1(uint64_t w) { return (w << 1) | (w >> 63); }
static NI uint32_t rot_plus(uint32_t w) { return (w >> 8) + (w << 24); }
static NI uint32_t rot_xor(uint32_t w) { return (w >> 9) ^ (w << 23); }
static NI uint32_t rot_after_add(uint32_t a, uint32_t b) { uint32_t t = a + b; return (t >> 16) | (t << 16); }
static NI uint32_t rot_var(uint32_t w, unsigned c) { return (w >> c) | (w << (32 - c)); }

/* Not rotates. */
static NI uint64_t mixed_widths(uint64_t x) { return (uint32_t)((uint32_t)x << 25) | (x >> 7); }
static NI uint64_t mixed_widths2(uint64_t x) { return (uint64_t)((uint32_t)x << 7) | (x >> 25); }
static NI uint32_t wrong_sum(uint32_t w) { return (w >> 7) | (w << 24); }
static NI uint64_t wrong_sum64(uint64_t w) { return (w >> 51) | (w << 14); }
static NI uint32_t two_values(uint32_t a, uint32_t b) { return (a >> 7) | (b << 25); }
static NI int32_t arithmetic(int32_t w) { return (w >> 7) | (int32_t)((uint32_t)w << 25); }
static NI uint16_t narrow(uint16_t w) { return (uint16_t)((w >> 3) | (w << 13)); }
static NI uint8_t narrower(uint8_t w) { return (uint8_t)((w >> 3) | (w << 5)); }

/* The shifted value is wanted as well. */
static NI uint32_t temp_used(uint32_t w, uint32_t *out)
{
    uint32_t t = w << 25;
    *out = t;
    return (w >> 7) | t;
}

/* A rotate of a value that came from memory, a load being 32 bits. */
static NI uint32_t from_memory(const uint32_t *p) { return (*p >> 11) | (*p << 21); }

/* A rotate in a loop, the way a hash has it. */
static NI uint32_t mix(const uint32_t *v, int n)
{
    uint32_t a = 0x6a09e667, b = 0xbb67ae85;
    for (int i = 0; i < n; i++) {
        a += b + v[i];
        b ^= a;
        b = (b >> 16) | (b << 16);
        a += b;
        b = (b >> 12) | (b << 20);
    }
    return a ^ b;
}

int main(void)
{
    static const uint64_t values[] = { 0, 1, 0x80000000u, 0xffffffffu, 0x123456789abcdef0ull, 0xffffffff00000000ull, 0xdeadbeefcafef00dull, ~0ull };
    uint32_t data[9];
    for (unsigned i = 0; i < 8; i++) {
        uint64_t x = values[i];
        uint32_t w = (uint32_t)x, out = 0;
        data[i] = w;
        printf("%08x %08x %016llx %016llx %08x %08x %08x %08x\n", rotr32_7(w), rotl32_13(w), (unsigned long long)rotr64_51(x),
               (unsigned long long)rotl64_1(x), rot_plus(w), rot_xor(w), rot_after_add(w, 12345), rot_var(w, 5 + i));
        printf("%016llx %016llx %08x %016llx %08x %08x %04x %02x\n", (unsigned long long)mixed_widths(x), (unsigned long long)mixed_widths2(x),
               wrong_sum(w), (unsigned long long)wrong_sum64(x), two_values(w, w + 1), (uint32_t)arithmetic((int32_t)w), narrow((uint16_t)w), narrower((uint8_t)w));
        uint32_t r = temp_used(w, &out);
        printf("%08x %08x %08x\n", r, out, from_memory(&w));
    }
    printf("%08x\n", mix(data, 8));
    return 0;
}
