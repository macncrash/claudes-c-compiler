/* A quotient and remainder by one 64-bit constant share one multiply-high
 * (sqlite3Int64ToText's x%100 then x/=100), whose magic is built once
 * outside the loop; the same 128-bit extensions and truncations written
 * twice in user code are computed once. Digit loops unsigned and signed,
 * several divisors in one loop, and __int128 arithmetic around the limits. */
#include <stdio.h>
#include <limits.h>

typedef unsigned long long u64;
typedef long long i64;
typedef unsigned __int128 u128;
typedef __int128 i128;

static int __attribute__((noinline)) digits100(u64 x, char *out) {
    char buf[24];
    int i = 23;
    buf[i] = 0;
    while (x >= 10) {
        int kk = (int)(x % 100) * 2;
        buf[--i] = (char)('0' + kk / 2 % 10);
        buf[--i] = (char)('0' + kk / 20);
        x /= 100;
    }
    if (x)
        buf[--i] = (char)('0' + x);
    int n = 0;
    while (buf[i])
        out[n++] = buf[i++];
    out[n] = 0;
    return n;
}

static i64 __attribute__((noinline)) signed_digit_sum(i64 v) {
    i64 s = 0;
    while (v != 0) {
        s += v % 10;
        v /= 10;
    }
    return s;
}

static u64 __attribute__((noinline)) mixed(u64 x) {
    u64 acc = 0;
    for (int k = 0; k < 6 && x; k++) {
        acc = acc * 31 + x % 7 + x / 7 % 13 + x % 1000003;
        x = x / 3 + x / 1000003;
    }
    return acc;
}

static u64 __attribute__((noinline)) hi_twice(u64 a, u64 b) {
    u64 h1 = (u64)(((u128)a * b) >> 64);
    u64 l1 = (u64)((u128)a * b);
    u64 h2 = (u64)(((u128)a * b) >> 64);
    return h1 * 3 + l1 + h2;
}

static i64 __attribute__((noinline)) shi_twice(i64 a, i64 b) {
    i64 h1 = (i64)(((i128)a * b) >> 64);
    i128 w = (i128)a;
    i64 h2 = (i64)((w * b) >> 64);
    return (i64)((u64)h1 - (u64)h2 * 5 + (u64)(i64)(w >> 1));
}

int main(void) {
    u64 uv[] = { 0, 9, 10, 99, 100, 12345, 99999999999ULL, 1000000000000000000ULL,
                 0xffffffffffffffffULL, 0x8000000000000000ULL, 18446744073709551557ULL };
    i64 sv[] = { 0, 7, -7, 123456789, -987654321012LL, LLONG_MAX, LLONG_MIN, LLONG_MIN + 1 };
    char out[32];
    u64 h = 0;
    for (int i = 0; i < 11; i++) {
        int n = digits100(uv[i], out);
        printf("%s %d\n", out, n);
        h = h * 1000003 + mixed(uv[i]);
        for (int j = 0; j < 11; j++)
            h = h * 1000003 + hi_twice(uv[i], uv[j]);
    }
    for (int i = 0; i < 8; i++) {
        h = h * 1000003 + (u64)signed_digit_sum(sv[i]);
        for (int j = 0; j < 8; j++)
            h = h * 1000003 + (u64)shi_twice(sv[i], sv[j]);
    }
    printf("hash %llu\n", h);
    return 0;
}
