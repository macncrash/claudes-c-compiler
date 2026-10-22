/* Early exits that return constants through a shared return block (an
 * a || b || c chain, a phi of constants and computed values, then a cast to
 * the return type): each exit gets its own return, with the constant
 * converted to the return type, including negative and truncated ones. */
#include <stdio.h>

typedef unsigned long long u64;

static u64 tab[8][64];

__attribute__((noinline)) static int any_int(int sq, u64 a, u64 b, u64 c) {
    return (tab[0][sq] & a) || (tab[1][sq] & b) || (tab[2][sq] & c) || ((tab[3][sq] ^ a) & (b | c));
}

__attribute__((noinline)) static signed char sc_ret(int k, int x) {
    if (k == 1)
        return -1;
    if (k == 2)
        return (signed char)200;
    if (k == 3)
        return 127;
    return (signed char)(x * 3);
}

__attribute__((noinline)) static unsigned short us_ret(int k, int x) {
    if (k & 1)
        return (unsigned short)-2;
    if (k & 2)
        return 65535;
    if (k & 4)
        return 7;
    return (unsigned short)(x << 9);
}

__attribute__((noinline)) static long long ll_ret(int k, long long x) {
    if (k < 0)
        return -5000000000LL;
    if (k == 0)
        return 0;
    if (x > 100)
        return x - 100;
    return k > 10 ? 3 : x * k;
}

__attribute__((noinline)) static unsigned u_ret(unsigned k) {
    switch (k & 7) {
    case 0: return 11;
    case 1: return 0xfffffff0u;
    case 2: return k;
    case 5: return 11;
    default: break;
    }
    if (k > 1000)
        return 1;
    return k * 2;
}

__attribute__((noinline)) static _Bool b_ret(int a, int b) {
    if (a > b)
        return 1;
    if (a == -b)
        return 0;
    return (a ^ b) & 4;
}

int main(void) {
    for (int i = 0; i < 8; i++)
        for (int s = 0; s < 64; s++)
            tab[i][s] = (0x9e3779b97f4a7c15ULL * (u64)(i * 64 + s + 1)) >> (i + s % 7);
    int hits = 0;
    for (int s = 0; s < 64; s++)
        hits = hits * 3 + any_int(s, 1ULL << s, 0x0101010101010101ULL << (s & 7), ~0ULL << (s | 1));
    printf("any %d\n", hits);
    for (int k = -2; k < 14; k++)
        printf("%d %d %u %lld %u %d\n", k, sc_ret(k, k * 37), us_ret(k, k + 5), ll_ret(k, k * 31 - 40),
               u_ret((unsigned)k * 997u), b_ret(k, 5 - k));
    return 0;
}
