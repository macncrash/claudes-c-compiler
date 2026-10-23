/* Division and remainder by constants of values widened from 8- and 16-bit
 * types or masked to 16 bits, which use a small multiplier: every dividend
 * of each type, including the extremes and negative ones. */
#include <stdio.h>

typedef unsigned long long u64;

static u64 h;

static void mix(long v) {
    h = (h ^ (u64)v) * 0x100000001b3ULL;
}

__attribute__((noinline)) static void sc(signed char x) {
    mix(x / 3); mix(x % 3); mix(x / 6); mix(x % 6); mix(x / 7); mix(x % 7);
    mix(x / 10); mix(x % 10); mix(x / 100); mix(x % 100); mix(x / -6); mix(x % -6);
}

__attribute__((noinline)) static void uc(unsigned char x) {
    mix(x / 3); mix(x % 3); mix(x / 6); mix(x % 6); mix(x / 10); mix(x % 10);
    mix(x / 255); mix(x % 255); mix(x / 13u); mix(x % 13u);
}

__attribute__((noinline)) static void ss(short x) {
    mix(x / 3); mix(x % 3); mix(x / 7); mix(x % 7); mix(x / 10); mix(x % 10);
    mix(x / 641); mix(x % 641); mix(x / 1000); mix(x % 1000);
}

__attribute__((noinline)) static void us(unsigned short x) {
    mix(x / 3); mix(x % 3); mix(x / 7u); mix(x % 7u); mix(x / 10); mix(x % 10);
    mix(x / 641); mix(x % 641); mix(x / 60000); mix(x % 60000);
}

__attribute__((noinline)) static void masked(unsigned x, long y) {
    unsigned a = x & 0xfff;
    long b = y & 0xffff;
    mix(a / 9); mix(a % 9); mix(a / 100u); mix(a % 100u);
    mix(b / 6); mix(b % 6); mix(b / 12); mix(b % 12);
}

/* A board code as in a chess program: pc / 6 picks the colour, pc % 6 the
 * piece; -1 is an empty square. */
__attribute__((noinline)) static int piece(const signed char *board, int sq, const int *val) {
    int pc = board[sq];
    return val[pc / 6 + 1] * 10 + pc % 6;
}

int main(void) {
    for (int x = -128; x <= 127; x++)
        sc((signed char)x);
    printf("sc %llu\n", h);
    for (int x = 0; x <= 255; x++)
        uc((unsigned char)x);
    printf("uc %llu\n", h);
    for (int x = -32768; x <= 32767; x++)
        ss((short)x);
    printf("ss %llu\n", h);
    for (int x = 0; x <= 65535; x++)
        us((unsigned short)x);
    printf("us %llu\n", h);
    for (long x = 0; x < 70000; x += 7)
        masked((unsigned)(x * 2654435761u), x * -40503 - 7);
    printf("masked %llu\n", h);
    signed char board[64];
    static const int val[3] = { 0, 1, 2 };
    for (int i = 0; i < 64; i++)
        board[i] = (signed char)(i % 13 - 1);
    unsigned s = 0;
    for (int i = 0; i < 64; i++)
        s = s * 7 + (unsigned)piece(board, i, val);
    printf("piece %u\n", s);
    return 0;
}
