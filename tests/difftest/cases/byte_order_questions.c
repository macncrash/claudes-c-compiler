/* The ways portable C asks which way round the machine is, and code that looks
 * like them and is not: a union read at another width, where the bytes are
 * only sometimes known. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define NI __attribute__((noinline))

static unsigned little_lz4(void)
{
    const union { uint32_t u; uint8_t c[4]; } one = { 1 };
    return one.c[0];
}

static int little_int(void)
{
    int x = 1;
    return *(char *)&x;
}

static int little_struct(void)
{
    union { struct { uint8_t a, b; } s; uint16_t w; } u;
    u.w = 0x0102;
    return u.s.a * 10 + u.s.b;
}

static NI unsigned count_bytes(uint64_t v)
{
    return little_lz4() ? (unsigned)__builtin_ctzll(v) >> 3 : (unsigned)__builtin_clzll(v) >> 3;
}

/* Bytes read back at every width and offset. */
static NI uint64_t widths(void)
{
    union { uint64_t q; uint32_t d[2]; uint16_t w[4]; uint8_t b[8]; int8_t s[8]; int16_t t[4]; } u;
    u.q = 0x8899aabbccddeeffull;
    uint64_t h = u.d[0] ^ ((uint64_t)u.d[1] << 13);
    h = h * 31 + u.w[1];
    h = h * 31 + u.w[3];
    h = h * 31 + u.b[0];
    h = h * 31 + u.b[7];
    h = h * 31 + (uint64_t)(int64_t)u.s[3];
    h = h * 31 + (uint64_t)(int64_t)u.t[2];
    return h;
}

/* A later store of part of it, constant or not. */
static NI uint32_t partly(uint8_t x)
{
    union { uint32_t u; uint8_t c[4]; } v;
    v.u = 0x11223344;
    v.c[1] = 0xee;
    uint32_t first = v.u;
    v.c[2] = x;
    return first ^ v.u ^ v.c[0];
}

/* Written in one branch or the other: nothing known after the join. */
static NI uint32_t branches(int f)
{
    union { uint32_t u; uint8_t c[4]; } v;
    if (f)
        v.u = 0x01020304;
    else
        v.u = 0x0a0b0c0d;
    return v.c[1];
}

/* Rewritten in a loop. */
static NI uint32_t in_a_loop(int n)
{
    union { uint32_t u; uint8_t c[4]; } v;
    uint32_t t = 0;
    v.u = 7;
    for (int i = 0; i < n; i++) {
        t += v.c[0];
        v.u = v.u * 3 + (uint32_t)i;
    }
    return t + v.c[1];
}

/* Its address leaves, so something else may change it. */
static NI void poke(uint8_t *p) { p[1] = 0x77; }

static NI uint32_t escapes(void)
{
    union { uint32_t u; uint8_t c[4]; } v;
    v.u = 0x11223344;
    poke(v.c);
    return v.c[1] + v.u;
}

/* Through a pointer to it, and a copy of it. */
static NI uint32_t by_pointer(int i)
{
    union { uint32_t u; uint8_t c[4]; } v, w;
    uint8_t *p = v.c;
    v.u = 0x11223344;
    p[i] = 0x99;
    memcpy(&w, &v, sizeof w);
    return w.c[0] + (uint32_t)v.c[3];
}

/* Floats are bytes too, here left to memory. */
static NI uint32_t float_bits(void)
{
    union { float f; uint32_t u; uint8_t c[4]; } v;
    v.f = 1.5f;
    return v.u + v.c[3];
}

static NI uint32_t volatile_one(void)
{
    volatile union { uint32_t u; uint8_t c[4]; } v;
    v.u = 0x01020304;
    return v.c[0];
}

/* Bits of a bit-field share their bytes. */
static NI int bitfields(void)
{
    union { struct { unsigned a : 3, b : 5, c : 8; } f; uint16_t w; } v;
    v.w = 0xabcd;
    return v.f.a * 1000 + v.f.b * 10 + v.f.c;
}

int main(void)
{
    printf("%u %d %d\n", little_lz4(), little_int(), little_struct());
    printf("%u %u %u\n", count_bytes(0x100), count_bytes(0x10000000000ull), count_bytes(1));
    printf("%llu\n", (unsigned long long)widths());
    printf("%u %u\n", partly(5), partly(0x22));
    printf("%u %u\n", branches(1), branches(0));
    printf("%u %u\n", in_a_loop(0), in_a_loop(9));
    printf("%u\n", escapes());
    printf("%u %u\n", by_pointer(0), by_pointer(3));
    printf("%u %u %d\n", float_bits(), volatile_one(), bitfields());
    return 0;
}
