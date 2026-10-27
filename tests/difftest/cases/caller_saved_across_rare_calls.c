/* More values live across calls than there are callee-saved registers,
 * used often in a loop whose call is taken rarely: some are kept in
 * caller-saved registers saved around the call. The callee clobbers every
 * caller-saved register it can. Also a function with calls, a relaxed
 * atomic load and a 128-bit overflow check among its values. */
#include <stdio.h>
#include <stdint.h>

__attribute__((noinline)) static uint64_t churn(uint64_t x) {
    uint64_t a = x, b = x * 3, c = x ^ 0x5555, d = x + 7, e = x * x, f = ~x, g = x >> 3, h = x << 5;
    uint64_t i = a ^ b, j = c + d, k = e - f, l = g | h, m = a * d, n = b ^ g, o = c + h, p = e ^ i;
    for (int r = 0; r < 3; r++) {
        a += j; b ^= k; c += l; d ^= m; e += n; f ^= o; g += p; h ^= a;
        i += b; j ^= c; k += d; l ^= e; m += f; n ^= g; o += h; p ^= i;
    }
    return a ^ b ^ c ^ d ^ e ^ f ^ g ^ h ^ i ^ j ^ k ^ l ^ m ^ n ^ o ^ p;
}

__attribute__((noinline)) static uint64_t many(const uint32_t *in, int n) {
    uint64_t s0 = 1, s1 = 2, s2 = 3, s3 = 4, s4 = 5, s5 = 6, s6 = 7, s7 = 8;
    uint64_t s8 = 9, s9 = 10, s10 = 11, s11 = 12, s12 = 13, s13 = 14, s14 = 15;
    for (int i = 0; i < n; i++) {
        uint32_t v = in[i];
        s0 += v; s1 ^= v * 3; s2 += s0 >> 2; s3 ^= s1 << 1; s4 += v ^ s2; s5 ^= s3 + v;
        s6 += s4 * 5; s7 ^= s5 >> 1; s8 += s6 ^ s7; s9 ^= s8 + 1; s10 += s9 >> 3;
        s11 ^= s10 * 7; s12 += s11 ^ v; s13 ^= s12 + s0; s14 += s13 >> 4;
        if ((v & 63) == 0)
            s14 ^= churn(s0 + s7);
    }
    return s0 ^ s1 ^ s2 ^ s3 ^ s4 ^ s5 ^ s6 ^ s7 ^ s8 ^ s9 ^ s10 ^ s11 ^ s12 ^ s13 ^ s14;
}

static int stop_flag;

__attribute__((noinline)) static int64_t mixed(const int64_t *in, int n) {
    int64_t prod = 1;
    uint64_t acc = 0, a = 3, b = 5, c = 7;
    for (int i = 0; i < n; i++) {
        if (__atomic_load_n(&stop_flag, __ATOMIC_RELAXED))
            break;
        int64_t t;
        if (__builtin_mul_overflow(prod, in[i], &t))
            prod = in[i] | 1;
        else
            prod = t;
        a += (uint64_t)in[i] ^ b;
        b += a >> 2;
        c ^= a + b;
        if ((in[i] & 15) == 3)
            acc += churn(a ^ c);
        acc += (uint64_t)prod ^ a ^ b ^ c;
    }
    return (int64_t)acc;
}

int main(void) {
    static uint32_t in[5000];
    uint32_t x = 12345;
    for (int i = 0; i < 5000; i++) {
        x = x * 1103515245u + 12345u;
        in[i] = x >> 7;
    }
    printf("many %llu\n", (unsigned long long)many(in, 5000));
    static int64_t w[3000];
    for (int i = 0; i < 3000; i++)
        w[i] = (int64_t)in[i] * (i % 7 == 0 ? 1000003 : 3) - 2000;
    printf("mixed %lld\n", (long long)mixed(w, 3000));
    return 0;
}
