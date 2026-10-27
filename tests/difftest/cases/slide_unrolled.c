/* In-place array loops the vectorizer unrolls four vectors at a time,
 * with trip counts around and at multiples of that step (one that ends
 * exactly on it leaves no single vector and no scalar iteration), for 8-,
 * 16- and 32-bit elements, counted down and up. */
#include <stdint.h>
#include <stdio.h>

__attribute__((noinline)) static void slide16(uint16_t *p, unsigned n, unsigned w) {
    do {
        unsigned m = *--p;
        *p = (uint16_t)(m >= w ? m - w : 0);
    } while (--n);
}

__attribute__((noinline)) static void add32(uint32_t *p, unsigned n, uint32_t k) {
    do {
        *p = *p + k;
        p++;
    } while (--n);
}

__attribute__((noinline)) static void xor8(uint8_t *p, unsigned long n, unsigned k) {
    do {
        --p;
        *p = (uint8_t)(*p ^ k);
    } while (--n);
}

static uint16_t a16[300];
static uint32_t a32[300];
static uint8_t a8[300];

int main(void) {
    static const unsigned counts[] = {1, 7, 8, 9, 15, 16, 17, 31, 32, 33, 39, 40, 47, 48, 63, 64, 65, 96, 127, 128, 129, 200, 256};
    unsigned long h = 0;
    for (unsigned c = 0; c < sizeof counts / sizeof counts[0]; c++) {
        unsigned n = counts[c];
        for (int i = 0; i < 300; i++) {
            a16[i] = (uint16_t)(i * 2654435761u >> 7);
            a32[i] = (uint32_t)i * 40503u;
            a8[i] = (uint8_t)(i * 37);
        }
        slide16(a16 + 20 + n, n, 9000);
        add32(a32 + 20, n, 0x01010101u);
        xor8(a8 + 20 + n, n, 0x5a);
        /* Every element, so a write past either end shows. */
        for (int i = 0; i < 300; i++)
            h = h * 1000003 + a16[i] + a32[i] * 3 + a8[i] * 7;
        printf("%u: %lu\n", n, h);
    }
    return 0;
}
