/* 64-bit loads whose value is only kept as 32 bits: an int or long field
 * passed as an unsigned argument (`ldrsw x3, [x0]; mov w3, w3` in
 * getOverflowPage, saveCursorKey's (u32)pCur->nKey), an int operand read
 * through a struct and used as an unsigned index (sqlite3VdbeExec's
 * pOp->p2), and int locals spilled across calls. Values with the upper
 * halves set check that only the low halves were wanted. */
#include <stdio.h>

struct op { unsigned char code; signed char p4type; unsigned short p5; int p1, p2, p3; long wide; };

static unsigned __attribute__((noinline)) take(unsigned a, unsigned b, unsigned c, unsigned d) {
    return a * 3 + b * 5 + c * 7 + d;
}

static unsigned __attribute__((noinline)) pass_fields(const struct op *o, const long *n) {
    return take((unsigned)o->p1, (unsigned)o->wide, (unsigned)n[1], (unsigned)o->p3);
}

static unsigned __attribute__((noinline)) index_ops(const struct op *ops, int n, const unsigned *table) {
    unsigned h = 0;
    for (int i = 0; i < n; i++) {
        unsigned k = (unsigned)ops[i].p2;
        h = h * 33 + table[k & 15] + (unsigned)ops[i].p3 + k;
    }
    return h;
}

static unsigned __attribute__((noinline)) step(unsigned x) { return x * 5 + 1; }

/* Enough live values across calls that some are spilled. */
static unsigned long __attribute__((noinline)) pressure(int a, int b, int c, int d, const int *v) {
    unsigned s0 = (unsigned)(a + v[0]), s1 = (unsigned)(b + v[1]), s2 = (unsigned)(c + v[2]), s3 = (unsigned)(d + v[3]);
    unsigned s4 = (unsigned)a * (unsigned)b, s5 = (unsigned)c * (unsigned)d, s6 = (unsigned)(a - d), s7 = (unsigned)(b - c);
    unsigned s8 = (unsigned)(v[4] ^ a), s9 = (unsigned)(v[5] ^ b), s10 = (unsigned)(v[6] ^ c), s11 = (unsigned)(v[7] ^ d);
    for (int i = 0; i < 6; i++) {
        s0 = step(s0 + s4);
        s1 = step(s1 + s5) ^ s8;
        s2 = step(s2 + s6) + s9;
        s3 = step(s3 + s7) - s10;
        s4 += (s11 + 1) >> 3;
        s11 = step(s11) & 0xffff;
    }
    return (unsigned long)s0 + s1 + s2 + s3 + s4 + s5 + s6 + s7 + s8 + s9 + s10 + s11;
}

int main(void) {
    struct op ops[12];
    for (int i = 0; i < 12; i++) {
        ops[i].code = (unsigned char)i;
        ops[i].p4type = (signed char)-i;
        ops[i].p5 = (unsigned short)(i * 1000);
        ops[i].p1 = -i;
        ops[i].p2 = i * 7 - 40;
        ops[i].p3 = i * i;
        ops[i].wide = -1L - i * 0x100000001L;
    }
    unsigned table[16];
    for (int i = 0; i < 16; i++)
        table[i] = (unsigned)(i * 2654435761u);
    printf("index %u\n", index_ops(ops, 12, table));

    long ls[] = { 0x123456789abcdefL, -1L, 0x7fffffff00000001L, -0x100000000L, 42 };
    printf("fields %u %u %u\n", pass_fields(&ops[3], ls), pass_fields(&ops[11], ls + 1), pass_fields(&ops[0], ls + 2));

    int v[8] = { 3, -7, 11, 100000, -5, 9, 12345, -2 };
    printf("pressure %lu %lu\n", pressure(1, 2, 3, 4, v), pressure(-100, 7, 65536, -3, v));
    return 0;
}
