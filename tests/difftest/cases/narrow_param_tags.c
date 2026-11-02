/* Narrow parameters widened into 64-bit operations whose results are
 * stored narrower (a JSONB-style header byte: type | size << 4), next to
 * ones whose high bits are read: stored wider, shifted right, compared,
 * or divided, which must keep the extension. */
#include <stdio.h>
#include <stdint.h>

static uint8_t buf[64];
static uint16_t wide[16];

static int __attribute__((noinline)) header(uint8_t type, uint64_t size, int at) {
    if (size <= 11) {
        buf[at] = type | (size << 4);
        return at + 1;
    }
    buf[at] = type | 0xc0;
    buf[at + 1] = (uint8_t)(size + type * 3);
    return at + 2;
}

static void __attribute__((noinline)) halves(uint16_t h, int8_t s, uint64_t k, int at) {
    buf[at] = (uint8_t)(h ^ k);
    buf[at + 1] = (uint8_t)((s * 5) + k);
    wide[at % 16] = (uint16_t)(s | (k << 8));
}

static uint64_t __attribute__((noinline)) high_read(uint8_t t, int8_t s, uint64_t k) {
    uint64_t a = (uint64_t)t | (k << 8);
    uint64_t b = (uint64_t)(int64_t)s + k;
    wide[0] = (uint16_t)a;
    return (a >> 4) + b / 3 + (b > 1000) + ((uint64_t)t << 40);
}

int main(void) {
    int at = 0;
    at = header(0x5, 3, at);
    at = header(0xb, 11, at);
    at = header(0x7, 200, at);
    at = header(0xff, 12, at);
    halves(0xabcd, -7, 0x1234567890, 20);
    halves(0xffff, 127, 1, 23);
    halves(0, -128, 0xff, 27);
    unsigned h = 0;
    for (int i = 0; i < 40; i++)
        h = h * 33 + buf[i];
    for (int i = 0; i < 16; i++)
        h = h * 31 + wide[i];
    printf("bytes %d %u\n", at, h);
    printf("high %llu %llu %u\n", (unsigned long long)high_read(0xfe, -3, 77),
           (unsigned long long)high_read(1, 100, 0xffffffffffull), wide[0]);
    return 0;
}
