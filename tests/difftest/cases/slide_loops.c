/* Loops that rewrite each element of an array in place, counted down to
 * zero (zlib's slide_hash and relatives): a saturating subtract of a value
 * below, at and above the element's range, wrapping adds and subtracts,
 * bitwise ops, a multiply, an operand on the left, both directions, and
 * trip counts around the vector width. Also fields of one struct read
 * around stores to its other fields. */
#include <stdint.h>
#include <stdio.h>

__attribute__((noinline)) static void slide16(uint16_t *p, unsigned n, unsigned w) {
    do {
        unsigned m = *--p;
        *p = (uint16_t)(m >= w ? m - w : 0);
    } while (--n);
}

__attribute__((noinline)) static void slide32(uint32_t *p, unsigned n, uint32_t w) {
    do {
        uint32_t m = *--p;
        *p = m > w ? m - w : 0;
    } while (--n);
}

__attribute__((noinline)) static void slide8(uint8_t *p, unsigned long n, unsigned long w) {
    do {
        unsigned m = *--p;
        *p = (uint8_t)(m < w ? 0 : m - w);
    } while (--n);
}

__attribute__((noinline)) static void add16(uint16_t *p, unsigned n, int k) {
    do {
        *p = (uint16_t)(*p + k);
        p++;
    } while (--n);
}

__attribute__((noinline)) static void rsub8(int8_t *p, unsigned n, int k) {
    do {
        *p = (int8_t)(k - *p);
        p++;
    } while (--n);
}

__attribute__((noinline)) static void xor32(uint32_t *p, unsigned n, uint32_t k) {
    do {
        *--p ^= k;
    } while (--n);
}

__attribute__((noinline)) static void mul16(int16_t *p, unsigned n, short k) {
    do {
        *p = (int16_t)(*p * k);
        p++;
    } while (--n);
}

__attribute__((noinline)) static void and64(uint64_t *p, unsigned long n, uint64_t k) {
    do {
        *p &= k;
        p++;
    } while (--n);
}

static uint32_t seed = 12345;
static uint32_t rnd(void) {
    seed = seed * 1103515245u + 12345u;
    return seed >> 8;
}

static uint32_t hash(const void *v, unsigned bytes) {
    const uint8_t *b = v;
    uint32_t h = 2166136261u;
    for (unsigned i = 0; i < bytes; i++)
        h = (h ^ b[i]) * 16777619u;
    return h;
}

struct bits {
    uint16_t buf;
    int valid;
    unsigned pending;
    uint8_t *out;
};

/* send_bits from zlib's trees.c: bi_valid is read around stores to
 * bi_buf, and the output bytes may land anywhere. */
__attribute__((noinline)) static void send(struct bits *s, int value, int length) {
    if (s->valid > 16 - length) {
        s->buf |= (uint16_t)(value << s->valid);
        s->out[s->pending++] = (uint8_t)(s->buf & 0xff);
        s->out[s->pending++] = (uint8_t)(s->buf >> 8);
        s->buf = (uint16_t)((uint16_t)value >> (16 - s->valid));
        s->valid += length - 16;
    } else {
        s->buf |= (uint16_t)(value << s->valid);
        s->valid += length;
    }
}

int main(void) {
    static const unsigned counts[] = {1, 2, 7, 8, 9, 15, 16, 17, 31, 33, 100};
    for (unsigned c = 0; c < sizeof counts / sizeof counts[0]; c++) {
        unsigned n = counts[c];
        uint16_t a16[128];
        uint32_t a32[128];
        uint8_t a8[128];
        int8_t s8[128];
        int16_t s16[128];
        uint64_t a64[128];
        for (unsigned i = 0; i < 128; i++) {
            a16[i] = (uint16_t)rnd();
            a32[i] = rnd() * 3u;
            a8[i] = (uint8_t)rnd();
            s8[i] = (int8_t)rnd();
            s16[i] = (int16_t)rnd();
            a64[i] = ((uint64_t)rnd() << 32) | rnd();
        }
        slide16(a16 + 120, n, 30000);
        slide16(a16 + 110, n, 65535);
        slide16(a16 + 105, n, 70000);
        slide16(a16 + 100, n, 0);
        slide32(a32 + 128, n, 0x7fffff);
        slide8(a8 + 128, n, 100);
        slide8(a8 + 120, n, 300);
        add16(a16 + 3, n, 40000);
        add16(a16 + 1, n, -7);
        rsub8(s8 + 2, n, 100);
        xor32(a32 + 101, n, 0xdeadbeef);
        mul16(s16 + 5, n, -3);
        and64(a64 + 4, n, 0xf0f0f0f00f0f0f0full);
        printf("n=%u %08x %08x %08x %08x %08x %08x\n", n, hash(a16, sizeof a16), hash(a32, sizeof a32),
               hash(a8, sizeof a8), hash(s8, sizeof s8), hash(s16, sizeof s16), hash(a64, sizeof a64));
    }

    uint8_t out[64];
    struct bits s = {0, 0, 0, out};
    for (int i = 0; i < 20; i++)
        send(&s, (int)(rnd() & 0x1ff), 9);
    printf("bits %u %d %04x %08x\n", s.pending, s.valid, s.buf, hash(out, s.pending));

    /* A struct whose bytes are written through a pointer into it. */
    struct bits t = {0, 3, 0, (uint8_t *)&t.valid};
    send(&t, 0x1ff, 15);
    send(&t, 0x155, 9);
    printf("self %u %d %04x\n", t.pending, t.valid, t.buf);
    return 0;
}
