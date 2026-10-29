/* A parameter that can stay in its argument register once the code
 * generator's short scratch uses of that register (a static table's
 * address, a byte loaded through it) move to a free temporary: SQLite's
 * vdbeRecordCompareInt reads aShift[] while pKey1 waits for x0. Also
 * calls between the parameter's uses, results read after a call, and a
 * function pointer called with the argument register in use. */
#include <stdio.h>

struct key { long i; signed char lt, gt; };

static long __attribute__((noinline)) slow_compare(const unsigned char *rec, const struct key *k, int flag) {
    long v = 0;
    for (int i = 1; i < 9; i++)
        v = v * 3 + rec[i];
    return (v < k->i ? k->lt : v > k->i ? k->gt : 0) + flag;
}

static long __attribute__((noinline)) compare_int(const unsigned char *rec, const struct key *k, int flag) {
    static const unsigned char shift[] = { 0, 56, 48, 40, 32, 16, 0, 0 };
    unsigned t = rec[0];
    if (t == 0 || t > 6)
        return slow_compare(rec, k, flag);
    unsigned long raw = 0;
    for (int i = 0; i < 8; i++)
        raw |= (unsigned long)rec[1 + i] << (8 * i);
    long v = (long)(raw << shift[t]) >> shift[t];
    if (v < k->i)
        return k->lt;
    if (v > k->i)
        return k->gt;
    return slow_compare(rec, k, flag) * 2;
}

static unsigned __attribute__((noinline)) mix(unsigned a, unsigned b) { return a * 2654435761u ^ b; }

/* A parameter used before and after a call that takes another value in x0. */
static unsigned __attribute__((noinline)) around_calls(const unsigned *p, int n) {
    static const unsigned salt[] = { 3, 5, 7, 11, 13, 17, 19, 23 };
    unsigned h = 0;
    for (int i = 0; i < n; i++) {
        h = mix(h + salt[i & 7], p[i]);
        h ^= p[(i * 3) % n] + salt[(unsigned)h & 7];
    }
    return h + p[0];
}

static long __attribute__((noinline)) twice(long (*f)(long), long x) {
    static const long add[] = { 1, -1, 100 };
    long r = f(x + add[x & 1]);
    return f(r + add[2]) + x;
}

static long sq(long x) { return x * x % 1000003; }
static long neg(long x) { return -x; }

int main(void) {
    unsigned char recs[][10] = {
        { 1, 0x7f }, { 1, 0x80 }, { 2, 0x12, 0x34 }, { 3, 0xff, 0xff, 0xfe }, { 4, 0x80, 0, 0, 1 },
        { 5, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06 }, { 6, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf0 },
        { 0, 9, 8, 7, 6, 5, 4, 3, 2 }, { 7, 1, 1, 1, 1, 1, 1, 1, 1 },
    };
    struct key keys[] = { { 0, -1, 1 }, { 4660, -5, 5 }, { -16, 2, -2 }, { 127, 9, -9 } };
    unsigned long s = 0;
    for (unsigned r = 0; r < sizeof recs / sizeof recs[0]; r++)
        for (unsigned k = 0; k < 4; k++)
            s = s * 7 + (unsigned long)compare_int(recs[r], &keys[k], (int)k);
    printf("compare %lu\n", s);

    unsigned v[13];
    for (int i = 0; i < 13; i++)
        v[i] = (unsigned)(i * i * 977 + 3);
    printf("calls %u %u\n", around_calls(v, 13), around_calls(v + 2, 4));
    printf("twice %ld %ld %ld\n", twice(sq, 12), twice(neg, 7), twice(sq, -40));
    return 0;
}
