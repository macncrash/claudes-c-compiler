/* Signed add/sub overflow tests (sqlite3AddInt64's __builtin_add_overflow)
 * branch on, or set, the add's own overflow flag; a signed halving rounds
 * with the sign bit; a field of an arithmetic shift is an extract. Values
 * at and around the limits, both widths, branched on and kept as values
 * (the select of sub_or_zero and the 32-bit tests keep their eor form). */
#include <stdio.h>
#include <limits.h>

typedef long long i64;

static int __attribute__((noinline)) add64(i64 *pA, i64 b) { return __builtin_add_overflow(*pA, b, pA); }
static int __attribute__((noinline)) sub64(i64 *pA, i64 b) { return __builtin_sub_overflow(*pA, b, pA); }

static i64 __attribute__((noinline)) add_or_sat(i64 a, i64 b) {
    i64 r;
    if (__builtin_add_overflow(a, b, &r))
        return a < 0 ? LLONG_MIN : LLONG_MAX;
    return r;
}

static i64 __attribute__((noinline)) sub_or_zero(i64 a, i64 b) {
    i64 r;
    if (!__builtin_sub_overflow(a, b, &r))
        return r;
    return 0;
}

static int __attribute__((noinline)) add32(int a, int b, int *ov) {
    int r;
    *ov = __builtin_add_overflow(a, b, &r);
    return r;
}

static int __attribute__((noinline)) sub32_branch(int a, int b) {
    int r;
    if (__builtin_ssub_overflow(a, b, &r))
        return -7;
    return r;
}

static i64 __attribute__((noinline)) half64(i64 x) { return (x - 12) / 2; }
static int __attribute__((noinline)) half32(int x) { return (x - 12) / 2; }
static int __attribute__((noinline)) field(i64 x) { return (int)((x >> 4) & 0xff); }
static int __attribute__((noinline)) field32(int x) { return (x >> 20) & 0x7ff; }

int main(void) {
    i64 v[] = { 0, 1, -1, 2, -2, 12, 13, 11, LLONG_MAX, LLONG_MIN, LLONG_MAX - 1, LLONG_MIN + 1,
                0x4000000000000000LL, -0x4000000000000000LL, 123456789012345LL };
    int w[] = { 0, 1, -1, INT_MAX, INT_MIN, INT_MAX - 1, INT_MIN + 1, 0x40000000, -0x40000000, 13 };
    unsigned long long h = 0;
    for (int i = 0; i < 15; i++)
        for (int j = 0; j < 15; j++) {
            i64 a = v[i], s = v[i];
            int o1 = add64(&a, v[j]);
            int o2 = sub64(&s, v[j]);
            h = h * 1000003 + (unsigned long long)a + (unsigned long long)o1 * 3;
            h = h * 1000003 + (unsigned long long)s + (unsigned long long)o2 * 5;
            h = h * 1000003 + (unsigned long long)add_or_sat(v[i], v[j]);
            h = h * 1000003 + (unsigned long long)sub_or_zero(v[i], v[j]);
        }
    for (int i = 0; i < 10; i++)
        for (int j = 0; j < 10; j++) {
            int ov;
            int r = add32(w[i], w[j], &ov);
            h = h * 1000003 + (unsigned)r + (unsigned)ov * 7;
            h = h * 1000003 + (unsigned)sub32_branch(w[i], w[j]);
        }
    for (int i = 0; i < 15; i++) {
        if (v[i] >= LLONG_MIN + 12)
            h = h * 1000003 + (unsigned long long)half64(v[i]);
        h = h * 1000003 + (unsigned)field(v[i]);
    }
    for (int i = 0; i < 10; i++) {
        if (w[i] >= INT_MIN + 12)
            h = h * 1000003 + (unsigned)half32(w[i]);
        h = h * 1000003 + (unsigned)field32(w[i]);
    }
    i64 m = LLONG_MAX;
    int o = add64(&m, 1);
    printf("%d %lld %lld %lld %d %d %d\n", o, m, add_or_sat(LLONG_MIN, -1), half64(-1), half32(-3), field(-1), field32(INT_MIN));
    printf("hash %llu\n", h);
    return 0;
}
