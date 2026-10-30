/* A flag bit tested into a u8 bool (getPageNormal's `noContent = (flags &
 * PAGER_GET_NOCONTENT)!=0`) is one bit extract, and a byte that is
 * already narrow (a low nibble, a bool, a loaded byte) needs no second
 * mask. Every bit position, both widths, and values with high bits set. */
#include <stdio.h>

typedef unsigned char u8;
typedef unsigned long long u64;

static volatile int sink;

static int __attribute__((noinline)) take(u8 b, int x) { return b ? x + 1 : x - 1; }

static int __attribute__((noinline)) bit0(int flags, int x) {
    u8 no_content = (flags & 1) != 0;
    sink = x;
    if (x == 0 || no_content)
        return 7;
    return take(no_content, x);
}

static int __attribute__((noinline)) bit5(unsigned flags, int x) {
    u8 b = (flags & 0x20) != 0;
    return take(b, x) + b;
}

static int __attribute__((noinline)) bit40(u64 flags, int x) {
    u8 b = (flags & (1ULL << 40)) != 0;
    return take(b, x) * 2 + b;
}

static int __attribute__((noinline)) bit31(unsigned flags) {
    u8 b = (flags & 0x80000000u) != 0;
    return b * 3;
}

static int __attribute__((noinline)) nibble(const u8 *z, int i) {
    u8 t = z[i] & 15;
    return (u8)(t - 7) <= 3 ? t * 5 : t;
}

static int __attribute__((noinline)) low_byte(const unsigned short *p) {
    u8 lo = (u8)(*p & 0x7f);
    return lo + (*p >> 8);
}

int main(void) {
    unsigned vals[] = { 0, 1, 2, 0x20, 0x21, 0x7fffffff, 0x80000000u, 0xffffffffu, 0xdeadbeefu, 0x12345678u };
    u64 big[] = { 0, 1ULL << 40, (1ULL << 40) - 1, ~0ULL, 0x0123456789abcdefULL };
    u8 bytes[] = { 0, 7, 8, 9, 10, 11, 0x17, 0x1a, 0xf9, 0xfe, 0xff };
    unsigned short halves[] = { 0, 0x7f, 0x80, 0xff, 0x1234, 0xffff };
    unsigned h = 0;
    for (int a = 0; a < 10; a++)
        for (int x = -1; x <= 2; x++) {
            h = h * 31 + (unsigned)bit0((int)vals[a], x);
            h = h * 31 + (unsigned)bit5(vals[a], x);
            h = h * 31 + (unsigned)bit31(vals[a]);
        }
    for (int a = 0; a < 5; a++)
        for (int x = -1; x <= 2; x++)
            h = h * 31 + (unsigned)bit40(big[a], x);
    for (int i = 0; i < 11; i++)
        h = h * 31 + (unsigned)nibble(bytes, i);
    for (int i = 0; i < 6; i++)
        h = h * 31 + (unsigned)low_byte(&halves[i]);
    printf("%d %d %d %d %d %d\n", bit0(1, 5), bit5(0x20, 5), bit40(1ULL << 40, 5), bit31(0x80000000u), nibble(bytes, 5), low_byte(&halves[4]));
    printf("hash %u\n", h);
    return 0;
}
