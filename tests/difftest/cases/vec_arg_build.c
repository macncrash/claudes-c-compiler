/* Vector arguments built straight into their argument registers on
 * AArch64, instead of in another register and copied there before the
 * call: built lane by lane or by vector ops, 8 and 16 bytes, integer and
 * floating point, several to one call, still used after the call, and
 * calls nested in arguments and in loops. */
#include <stdio.h>

typedef unsigned u4 __attribute__((vector_size(16)));
typedef unsigned u2 __attribute__((vector_size(8)));
typedef float f4 __attribute__((vector_size(16)));
typedef double d2 __attribute__((vector_size(16)));
typedef unsigned char u8x8 __attribute__((vector_size(8)));

typedef unsigned long long u64;
static u64 h;

__attribute__((noinline)) static void mix(const void *p, unsigned n) {
    const unsigned char *b = p;
    for (unsigned i = 0; i < n; i++)
        h = (h ^ b[i]) * 0x100000001b3ULL;
}

__attribute__((noinline)) static u2 step(u2 acc, u2 v, unsigned k) {
    return acc * 3u + (v ^ k);
}

__attribute__((noinline)) static u4 comb(u4 a, u4 b, u4 c) {
    return a * 5u + (b ^ c);
}

__attribute__((noinline)) static f4 fma4(f4 a, f4 b, float s) {
    return a * s + b;
}

__attribute__((noinline)) static d2 dsum(d2 a, int i, d2 b) {
    return a + b * (double)i;
}

__attribute__((noinline)) static u8x8 bytes(u8x8 a, u8x8 b) {
    return (a >> 1) + (b >> 2);
}

int main(void) {
    u2 acc = { 1, 2 };
    for (unsigned i = 0; i < 50; i++)
        acc = step(acc, (u2){ i, -i }, i & 7);
    mix(&acc, sizeof acc);

    u4 x = { 1, 2, 3, 4 };
    for (unsigned i = 0; i < 20; i++) {
        u4 a = { i, i * 3, i ^ 5, ~i };
        u4 b = x ^ (u4){ i, i, i, i };
        x = comb(a, b, (u4){ i + 1, i + 2, i + 3, i + 4 });
        mix(&a, sizeof a);
        mix(&b, sizeof b);
    }
    mix(&x, sizeof x);

    f4 f = { 0.5f, 1.5f, -2.0f, 3.0f };
    for (int i = 0; i < 10; i++) {
        f4 g = { (float)i, 1.0f, (float)-i, 0.25f };
        f = fma4(fma4(g, f, 0.5f), g * 2.0f, 0.75f);
    }
    mix(&f, sizeof f);

    d2 d = { 1.0, -1.0 };
    for (int i = 0; i < 10; i++)
        d = dsum((d2){ i * 0.5, i * 0.25 }, i, d * 0.5);
    mix(&d, sizeof d);

    u8x8 p = { 1, 2, 3, 4, 250, 251, 252, 253 };
    for (int i = 0; i < 6; i++) {
        u8x8 q = p + (unsigned char)i;
        p = bytes(q, (u8x8){ i, 2 * i, 3 * i, 4 * i, 5, 6, 7, 8 });
        mix(&q, sizeof q);
    }
    mix(&p, sizeof p);

    printf("%llu\n", h);
    return 0;
}
