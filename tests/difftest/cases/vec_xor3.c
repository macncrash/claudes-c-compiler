/* Three-way vector Xors, which arm64 macOS computes in one eor3 when the
 * first Xor's result is read only by the second and its operands are
 * unchanged in between: three and four operands, a constant one, operands
 * loaded from memory in a loop, and the cases left as two eors (a shared
 * intermediate, xorshift's shifted chain, an operand rewritten in between,
 * 8-byte vectors). */
#include <stdio.h>

typedef unsigned u4 __attribute__((vector_size(16)));
typedef unsigned char u16 __attribute__((vector_size(16)));
typedef unsigned u2 __attribute__((vector_size(8)));

/* Three independent values: one eor3. */
__attribute__((noinline)) static u4 three(u4 a, u4 b, u4 c) { return a ^ b ^ c; }

/* Four: eor3 then eor (a folded Xor's reader is not folded again). */
__attribute__((noinline)) static u4 four(u4 a, u4 b, u4 c, u4 d) { return a ^ b ^ c ^ d; }

/* The inner Xor is used twice: no fold. */
__attribute__((noinline)) static u4 shared(u4 a, u4 b, u4 c) {
    u4 t = a ^ b;
    return (t ^ c) + t;
}

/* A constant third operand. */
__attribute__((noinline)) static u4 with_const(u4 a, u4 b) { return a ^ b ^ 0x5a5a5a5au; }

/* xorshift-style: each step's value feeds a shift too. */
__attribute__((noinline)) static u4 xorshift(u4 x) {
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return x;
}

/* An operand rewritten between the two Xors. */
__attribute__((noinline)) static u4 rewritten(u4 a, u4 b, u4 c) {
    u4 t = a ^ b;
    a = a + c;
    return t ^ a;
}

/* A loop mixing byte vectors. */
__attribute__((noinline)) static u16 mix(const u16 *v, int n) {
    u16 h = {0};
    for (int i = 0; i + 2 < n; i++)
        h = (h + v[i]) ^ v[i + 1] ^ v[i + 2];
    return h;
}

/* 8-byte vectors keep plain eor. */
__attribute__((noinline)) static u2 narrow(u2 a, u2 b, u2 c) { return a ^ b ^ c; }

static void show(const char *name, u4 v) {
    printf("%-10s %08x %08x %08x %08x\n", name, v[0], v[1], v[2], v[3]);
}

int main(void) {
    u4 a = {0x12345678, 0x9abcdef0, 0x0f0f0f0f, 0xdeadbeef};
    u4 b = {0x11111111, 0x22222222, 0x33333333, 0x44444444};
    u4 c = {0x80000001, 0x7fffffff, 0x01010101, 0xcafebabe};
    u4 d = {5, 6, 7, 8};
    show("three", three(a, b, c));
    show("four", four(a, b, c, d));
    show("shared", shared(a, b, c));
    show("const", with_const(a, b));
    show("xorshift", xorshift(a));
    show("rewritten", rewritten(a, b, c));
    u16 v[8];
    for (int i = 0; i < 8; i++)
        for (int j = 0; j < 16; j++)
            v[i][j] = (unsigned char)(i * 31 + j * 7);
    u16 h = mix(v, 8);
    printf("mix       ");
    for (int j = 0; j < 16; j++)
        printf("%02x", h[j]);
    printf("\n");
    u2 n = narrow((u2){1, 2}, (u2){3, 4}, (u2){5, 6});
    printf("narrow    %u %u\n", n[0], n[1]);
    return 0;
}
