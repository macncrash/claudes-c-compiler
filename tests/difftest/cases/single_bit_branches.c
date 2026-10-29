/* Branches on one bit of a value (`flags & 4`), which test the bit directly:
 * bits in both halves of int and long, inside an interpreter loop whose
 * dispatch is copied into its cases (sqlite3VdbeExec's MEM_ flag tests),
 * and a test that skips a block too long for a test-and-branch to cross. */
#include <stdio.h>

static int __attribute__((noinline)) low_bits(unsigned f) {
    int n = 0;
    if (f & 1) n += 1;
    if (f & 0x80) n += 10;
    if (!(f & 0x80000000u)) n += 100;
    if (f & 0x10000) n += 1000;
    return n;
}

static long __attribute__((noinline)) wide_bits(unsigned long f, long a) {
    if (f & (1ul << 32)) a += 3;
    if (!(f & (1ul << 63))) a *= 2;
    if (f & (1ul << 40)) return a - 1;
    if ((long)f & 2) a ^= 0x55;
    return a;
}

/* A bytecode loop: each case tests flag bits of its operand. */
static long __attribute__((noinline)) run(const unsigned char *code, const unsigned short *flags, int n) {
    long acc = 0;
    int pc = 0;
    while (pc < n) {
        unsigned short f = flags[pc];
        switch (code[pc++]) {
        case 0: if (f & 4) acc += 7; else acc -= 1; break;
        case 1: if (f & 1) acc *= 3; break;
        case 2: if (!(f & 0x200)) acc ^= f; break;
        case 3: if (f & 0x8000) acc -= f; else acc += 2 * f; break;
        case 4: acc += (f & 16) ? 5 : 9; break;
        case 5: if (f & 2) pc++; break;
        default: return acc;
        }
    }
    return acc;
}

/* The bit test before the long block must still reach past it. */
volatile int sink[16];
#define S1(k) sink[(k) & 15] += (k) * 3 + x;
#define S8(k) S1(k) S1(k + 1) S1(k + 2) S1(k + 3) S1(k + 4) S1(k + 5) S1(k + 6) S1(k + 7)
#define S64(k) S8(k) S8(k + 8) S8(k + 16) S8(k + 24) S8(k + 32) S8(k + 40) S8(k + 48) S8(k + 56)
#define S512(k) S64(k) S64(k + 64) S64(k + 128) S64(k + 192) S64(k + 256) S64(k + 320) S64(k + 384) S64(k + 448)

static int __attribute__((noinline)) far_skip(unsigned flags, int x) {
    if (flags & 8) {
        S512(0) S512(512) S512(1024) S512(1536)
    }
    return sink[3] + sink[12];
}

int main(void) {
    unsigned fs[] = { 0, 1, 0x81, 0x80000000u, 0x10081, 0xffffffffu, 0x7fffffffu };
    int h = 0;
    for (int i = 0; i < 7; i++)
        h = h * 7 + low_bits(fs[i]);
    printf("low %d\n", h);

    unsigned long ws[] = { 0, 1ul << 32, 1ul << 63, 1ul << 40, 2, 0xfffffffffffffffful, 0x100000002ul };
    long s = 0;
    for (int i = 0; i < 7; i++)
        s = s * 5 + wide_bits(ws[i], i + 10);
    printf("wide %ld\n", s);

    unsigned char code[64];
    unsigned short flags[64];
    for (int i = 0; i < 63; i++) {
        code[i] = (unsigned char)((i * 7 + 3) % 6);
        flags[i] = (unsigned short)(i * 2654435761u >> 11);
    }
    code[63] = 9;
    flags[63] = 0;
    printf("run %ld %ld\n", run(code, flags, 64), run(code + 5, flags + 9, 40));

    int a = far_skip(0, 5);
    int b = far_skip(8, 7);
    int c = far_skip(7, 9);
    printf("far %d %d %d\n", a, b, c);
    return 0;
}
