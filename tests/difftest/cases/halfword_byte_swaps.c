/* Big-endian halfword reads (SQLite's get2byte over a cell pointer array),
 * masked and used as offsets, and 16-bit byte swaps of values whose upper
 * bits are not zero: a truncated word, a sum, a sign-extended halfword. */
#include <stdio.h>
#include <string.h>

#define get2byte(p) ((unsigned)((p)[0] << 8 | (p)[1]))

static unsigned char page[4096];

static unsigned __attribute__((noinline)) walk_cells(const unsigned char *pg, unsigned mask, int n) {
    unsigned sum = 0;
    for (int i = 0; i < n; i++) {
        unsigned off = mask & get2byte(&pg[8 + 2 * i]);
        sum = sum * 31 + pg[off] + get2byte(&pg[off + 1]);
    }
    return sum;
}

static unsigned short __attribute__((noinline)) load_be16(const unsigned short *p) {
    unsigned short v;
    memcpy(&v, p, 2);
    return __builtin_bswap16(v);
}

static unsigned __attribute__((noinline)) swap_low(unsigned x) {
    return __builtin_bswap16((unsigned short)x);
}

static unsigned __attribute__((noinline)) swap_sum(unsigned short a, unsigned short b) {
    return __builtin_bswap16((unsigned short)(a + b));
}

static int __attribute__((noinline)) swap_signed(short s) {
    return (short)__builtin_bswap16((unsigned short)s);
}

int main(void) {
    for (int i = 0; i < 4096; i++)
        page[i] = (unsigned char)(i * 37 + (i >> 3));
    for (int i = 0; i < 100; i++) {
        unsigned off = 200 + i * 37;
        page[8 + 2 * i] = (unsigned char)(off >> 8);
        page[9 + 2 * i] = (unsigned char)off;
    }
    printf("walk %u %u\n", walk_cells(page, 0xfff, 100), walk_cells(page, 0x7ff, 100));
    unsigned short words[4] = { 0x1234, 0xff00, 0x00ff, 0xabcd };
    unsigned acc = 0;
    for (int i = 0; i < 4; i++)
        acc = acc * 65599 + load_be16(&words[i]);
    printf("be16 %u\n", acc);
    printf("low %x %x %x\n", swap_low(0xdeadbeefu), swap_low(0x12345678u), swap_low(0xffff0001u));
    printf("sum %x %x\n", swap_sum(0xffff, 2), swap_sum(0x8000, 0x8000));
    printf("signed %d %d %d\n", swap_signed(-2), swap_signed(0x7f80), swap_signed(-32768));
    return 0;
}
