/* Words loaded next to the work on the previous one (paired into one load,
 * but not across a store to the second word or when the loaded register is
 * still read); a table of eight rows of 1 KB indexed by each byte (rows at
 * offsets an add immediate cannot hold); 4-byte structs copied into a table
 * in a loop with many other values live. */
#include <stdio.h>
#include <stdint.h>

static uint32_t rows[8][256];

__attribute__((noinline)) static void braid(const uint64_t *w, int n, uint32_t out[5]) {
    uint64_t c0 = out[0], c1 = out[1], c2 = out[2], c3 = out[3], c4 = out[4];
    for (int i = 0; i < n; i++, w += 5) {
        uint64_t w0 = c0 ^ w[0], w1 = c1 ^ w[1], w2 = c2 ^ w[2], w3 = c3 ^ w[3], w4 = c4 ^ w[4];
        c0 = c1 = c2 = c3 = c4 = 0;
        for (int k = 0; k < 8; k++) {
            c0 ^= rows[k][(w0 >> (8 * k)) & 0xff];
            c1 ^= rows[k][(w1 >> (8 * k)) & 0xff];
            c2 ^= rows[k][(w2 >> (8 * k)) & 0xff];
            c3 ^= rows[k][(w3 >> (8 * k)) & 0xff];
            c4 ^= rows[k][(w4 >> (8 * k)) & 0xff];
        }
    }
    out[0] = (uint32_t)c0; out[1] = (uint32_t)c1; out[2] = (uint32_t)c2; out[3] = (uint32_t)c3; out[4] = (uint32_t)c4;
}

/* The second word is stored between the two loads. */
__attribute__((noinline)) static long store_between(long *p, long v) {
    long a = p[0];
    long t = a * 3;
    p[1] = v + t;
    long b = p[1];
    return a + b * 7;
}

/* The first load's register is read after the second load would go. */
__attribute__((noinline)) static long reused(const long *p, long x) {
    long a = p[2];
    long s = a + x;
    long b = p[3];
    return s * 5 + b + a;
}

typedef struct { unsigned char op, bits; unsigned short val; } code;

__attribute__((noinline)) static unsigned fill(code *table, unsigned root, const unsigned short *lens, unsigned n) {
    unsigned huff = 0, used = 0, drop = 0, sum = 0;
    for (unsigned sym = 0; sym < n; sym++) {
        unsigned len = lens[sym] & 7;
        if (len == 0)
            continue;
        code here = { (unsigned char)(len > 4 ? 96 : 0), (unsigned char)(len - drop), (unsigned short)(sym * 3) };
        unsigned incr = 1u << (len - drop), fill = 1u << root;
        unsigned base = huff >> drop;
        do {
            fill -= incr;
            table[(base + fill) & ((1u << root) - 1)] = here;
        } while (fill != 0);
        huff = (huff + incr * 5 + sym) & ((1u << root) - 1);
        used += incr;
        sum = sum * 31 + here.val + used;
        drop = len > 3 ? 1 : 0;
    }
    return sum ^ huff ^ used;
}

int main(void) {
    for (int k = 0; k < 8; k++)
        for (int b = 0; b < 256; b++)
            rows[k][b] = (uint32_t)(b * 2654435761u) ^ (uint32_t)(k * 0x9e3779b9u) ^ (uint32_t)(b << k);
    static uint64_t words[200];
    for (int i = 0; i < 200; i++)
        words[i] = (uint64_t)i * 0x9e3779b97f4a7c15ull ^ ((uint64_t)i << 40);
    uint32_t out[5] = { 1, 2, 3, 4, 5 };
    braid(words, 40, out);
    printf("braid %08x %08x %08x %08x %08x\n", out[0], out[1], out[2], out[3], out[4]);

    long p[4] = { 10, 20, 30, 40 };
    long st = store_between(p, 5);
    printf("store %ld %ld\n", st, p[1]);
    printf("reused %ld\n", reused(p, 9));

    static code table[1 << 9];
    static unsigned short lens[300];
    for (int i = 0; i < 300; i++)
        lens[i] = (unsigned short)(i * 7 % 13);
    unsigned s = fill(table, 9, lens, 300);
    unsigned h = 0;
    for (int i = 0; i < (1 << 9); i++)
        h = h * 33 + table[i].op + table[i].bits * 7u + table[i].val * 131u;
    printf("fill %u %u\n", s, h);
    return 0;
}
