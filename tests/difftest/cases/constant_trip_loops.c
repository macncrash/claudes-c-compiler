/* Loops that run a constant number of times, unrolled in full: zlib's
 * braided CRC (a table row and byte shift per k), counting down, `!=`
 * tests, unsigned counters that wrap, stores in the body, values read after
 * the loop (the counter and accumulators), a loop that never runs, and one
 * whose bound is only known at run time. */
#include <stdio.h>
#include <stdint.h>

static uint32_t tab[8][256];

__attribute__((noinline)) static uint32_t braid(const uint64_t *w, unsigned long blks) {
    uint32_t c0 = 0x12345678u, c1 = 0;
    while (--blks) {
        uint64_t w0 = c0 ^ w[0], w1 = c1 ^ w[1];
        w += 2;
        c0 = tab[0][w0 & 0xff];
        c1 = tab[0][w1 & 0xff];
        for (int k = 1; k < 8; k++) {
            c0 ^= tab[k][(w0 >> (k << 3)) & 0xff];
            c1 ^= tab[k][(w1 >> (k << 3)) & 0xff];
        }
    }
    return c0 ^ c1;
}

__attribute__((noinline)) static long down_and_after(long x, short *out) {
    long acc = x;
    int k;
    for (k = 5; k != 0; k--) {
        acc = acc * 3 + k;
        out[k] = (short)acc;
    }
    return acc + k * 1000 + out[3];
}

__attribute__((noinline)) static unsigned wrap_counter(unsigned x) {
    unsigned char c;
    unsigned s = x;
    for (c = 250; c != 3; c++)
        s = (s << 1 | s >> 31) ^ c;
    return s + c;
}

__attribute__((noinline)) static int never(int x) {
    for (int k = 4; k < 4; k++)
        x *= 7;
    return x;
}

__attribute__((noinline)) static int runtime_bound(int x, int n) {
    for (int k = 0; k < n; k++)
        x = x * 5 + k;
    return x;
}

__attribute__((noinline)) static uint64_t steps_of_three(uint64_t x) {
    uint64_t r = 0;
    for (unsigned i = 1; i <= 16; i += 3)
        r += (x >> i) & 0xf;
    return r;
}

int main(void) {
    for (unsigned k = 0; k < 8; k++)
        for (unsigned i = 0; i < 256; i++)
            tab[k][i] = (i + k * 977u) * 2654435761u ^ (i << k);
    static uint64_t words[2 * 400];
    uint64_t s = 0x9e3779b97f4a7c15u;
    for (unsigned i = 0; i < 800; i++) {
        s ^= s << 13, s ^= s >> 7, s ^= s << 17;
        words[i] = s;
    }
    printf("braid %08x %08x\n", (unsigned)braid(words, 400), (unsigned)braid(words + 6, 3));
    short out[6] = {0};
    long d = 0;
    for (long x = -3; x < 40; x += 7)
        d = d * 17 + down_and_after(x, out);
    printf("down %ld %d\n", d, out[1]);
    unsigned w = 0;
    for (unsigned x = 0; x < 50; x++)
        w = w * 31 + wrap_counter(x * 2246822519u);
    printf("wrap %u\n", w);
    printf("never %d\n", never(11));
    int r = 0;
    for (int n = 0; n < 12; n++)
        r = r * 3 + runtime_bound(n, n);
    printf("runtime %d\n", r);
    uint64_t t = 0;
    for (unsigned i = 0; i < 100; i++)
        t = t * 7 + steps_of_three(words[i]);
    printf("threes %llu\n", (unsigned long long)t);
    return 0;
}
