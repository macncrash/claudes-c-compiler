/* 64-bit division and remainder by constants, whose multiply-high is one
 * umulh/smulh rather than a 128-bit product (sqlite3Int64ToText's
 * digit pairs, v/100 and v%100). Divisors needing the add-back fixup,
 * signed magics both under and over 2^63, negative divisors, and
 * dividends at the limits; also a digit loop with a stack buffer. */
#include <stdio.h>
#include <limits.h>

typedef unsigned long long u64;
typedef long long i64;

#define UDIV(d) static u64 __attribute__((noinline)) ud##d(u64 x) { return x / d##ULL; } \
                static u64 __attribute__((noinline)) ur##d(u64 x) { return x % d##ULL; }
#define SDIV(n, d) static i64 __attribute__((noinline)) sd##n(i64 x) { return x / (d); } \
                   static i64 __attribute__((noinline)) sr##n(i64 x) { return x % (d); }

UDIV(3) UDIV(7) UDIV(10) UDIV(100) UDIV(1000000007) UDIV(641) UDIV(274177) UDIV(12345678901234567)
SDIV(3, 3) SDIV(7, 7) SDIV(10, 10) SDIV(100, 100) SDIV(m7, -7) SDIV(m10, -10) SDIV(big, 1234567890123LL)

static int __attribute__((noinline)) to_text(i64 v, char *out) {
    char buf[24];
    int i = sizeof(buf);
    u64 x = v < 0 ? 0 - (u64)v : (u64)v;
    buf[--i] = 0;
    do {
        buf[--i] = (char)('0' + x % 10);
        x /= 10;
    } while (x);
    if (v < 0)
        buf[--i] = '-';
    int n = (int)sizeof(buf) - i;
    for (int k = 0; k < n; k++)
        out[k] = buf[i + k];
    return n - 1;
}

int main(void) {
    u64 uv[] = { 0, 1, 2, 9, 10, 99, 100, 101, 641, 999999, 1000000006, 1000000007, 4294967295ULL,
                 4294967296ULL, 6700417ULL * 641, 0x7fffffffffffffffULL, 0x8000000000000000ULL,
                 0xfffffffffffffffeULL, 0xffffffffffffffffULL, 12345678901234567ULL * 3 + 5 };
    i64 sv[] = { 0, 1, -1, 6, -6, 7, -7, 99, -99, 100, -100, 101, -101, 1234567890123LL * 5 + 3,
                 -1234567890123LL * 5 - 3, LLONG_MAX, LLONG_MIN, LLONG_MIN + 1, LLONG_MAX - 1 };
    u64 h = 0;
    for (int i = 0; i < 20; i++) {
        u64 x = uv[i];
        u64 r[] = { ud3(x), ur3(x), ud7(x), ur7(x), ud10(x), ur10(x), ud100(x), ur100(x),
                    ud1000000007(x), ur1000000007(x), ud641(x), ur641(x), ud274177(x), ur274177(x),
                    ud12345678901234567(x), ur12345678901234567(x) };
        for (int k = 0; k < 16; k++)
            h = h * 1000003 + r[k];
    }
    for (int i = 0; i < 19; i++) {
        i64 x = sv[i];
        i64 r[] = { sd3(x), sr3(x), sd7(x), sr7(x), sd10(x), sr10(x), sd100(x), sr100(x),
                    sdm7(x), srm7(x), sdm10(x), srm10(x), sdbig(x), srbig(x) };
        for (int k = 0; k < 14; k++)
            h = h * 1000003 + (u64)r[k];
    }
    char out[24];
    i64 tv[] = { 0, 7, -7, 1234567890, LLONG_MAX, LLONG_MIN, -1000000000000LL };
    for (int i = 0; i < 7; i++) {
        int n = to_text(tv[i], out);
        printf("%s %d\n", out, n);
    }
    printf("%llu %llu %lld %lld\n", ud10(0xffffffffffffffffULL), ur100(0xffffffffffffffffULL), sd7(LLONG_MIN), srm7(LLONG_MIN));
    printf("hash %llu\n", h);
    return 0;
}
