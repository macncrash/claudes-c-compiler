/* Searches shaped like LZ77 match finding: loop conditions joined by && and
 * || (each operand a branch, not a computed boolean), a running best length
 * and its position picked by selects, and inner loops reading p[j + i] with j
 * fixed per loop (the base p + j built once), with negative j and an
 * unsigned offset. Kept as branches, a second test of the same two values is
 * decided by the first one (x != y after x == y), but a signed order says
 * nothing of the unsigned one. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint8_t buf[4096];
static int32_t chain[4096];

__attribute__((noinline)) static long longest(const uint8_t *in, long n, long i, long *where) {
    long best = 0, dist = 0;
    int32_t cand = chain[i];
    for (int depth = 0; cand >= 0 && depth < 8 && i - cand <= 1000; depth++) {
        long max = n - i < 40 ? n - i : 40, l = 0;
        while (l < max && in[cand + l] == in[i + l])
            l++;
        if (l > best) {
            best = l;
            dist = i - cand;
        }
        cand = chain[cand];
    }
    *where = dist;
    return best;
}

/* || in a loop condition, with the operands' order mattering for safety. */
__attribute__((noinline)) static int skip(const int *a, int n, int k) {
    int i = 0;
    while (i < n && (a[i] < 0 || a[i] == k || (i & 3) == 3))
        i++;
    return i;
}

/* A boolean phi that is also used as a value: stays a select. */
__attribute__((noinline)) static int count_pairs(const int *a, int n) {
    int c = 0;
    for (int i = 0; i + 1 < n; i++) {
        int both = a[i] > 0 && a[i + 1] > 0;
        c += both;
        if (both)
            c += 2;
    }
    return c;
}

/* Offsets that are negative, or unsigned. */
__attribute__((noinline)) static long window(const long *p, long j, int n) {
    long s = 0;
    for (long i = 0; i < n; i++)
        s += p[j + i] * (i + 1);
    return s;
}

__attribute__((noinline)) static unsigned unsigned_sum(const uint8_t *p, unsigned long j, int n) {
    unsigned s = 0;
    for (unsigned long i = 0; i < (unsigned long)n; i++)
        s = s * 31 + p[i + j];
    return s;
}

/* Each test after the first is decided by it, or (mixing signed and
 * unsigned order) is not. */
__attribute__((noinline)) static int relations(int x, int y) {
    unsigned ux = (unsigned)x, uy = (unsigned)y;
    int m = 0;
    if (x == y && x != y)
        m |= 1;
    if (x < y && y < x)
        m |= 2;
    if (x <= y || x > y)
        m |= 4;
    if (x < y && x != y)
        m |= 8;
    if (x < y && ux < uy)
        m |= 16;
    if (x < y && ux > uy)
        m |= 32;
    if (ux == uy && x >= y)
        m |= 64;
    return m;
}

int main(void) {
    uint32_t x = 12345;
    for (int i = 0; i < 4096; i++) {
        x = x * 1103515245u + 12345u;
        buf[i] = (i % 512 < 400) ? "abcabdabcabcd"[(x >> 16) % 5] : (uint8_t)(x >> 20);
    }
    int32_t head[256];
    memset(head, -1, sizeof head);
    long total = 0, dsum = 0;
    for (long i = 0; i + 3 <= 4096; i++) {
        int h = (buf[i] * 31 + buf[i + 1] * 7 + buf[i + 2]) & 255;
        chain[i] = head[h];
        head[h] = (int32_t)i;
        long d;
        long l = longest(buf, 4096, i, &d);
        total += l;
        dsum += d;
    }
    printf("longest %ld %ld\n", total, dsum);

    int a[64];
    for (int i = 0; i < 64; i++)
        a[i] = (i * 7) % 11 - 3;
    printf("skip %d %d %d\n", skip(a, 64, 4), skip(a, 64, 1), skip(a + 5, 59, 2));
    printf("pairs %d\n", count_pairs(a, 64));

    long w[100];
    for (int i = 0; i < 100; i++)
        w[i] = i * i - 50;
    printf("window %ld %ld\n", window(w + 50, -20, 30), window(w, 3, 90));
    printf("unsigned %u %u\n", unsigned_sum(buf + 100, 50, 80), unsigned_sum(buf, 7, 200));
    printf("relations %d %d %d %d %d\n", relations(0, 0), relations(1, 2), relations(4, 3),
           relations(-1, 0), relations(-5, -2));
    return 0;
}
