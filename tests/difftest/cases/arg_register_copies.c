/* Values copied between temporaries and argument registers: parameters
 * kept in a temporary at entry, arguments computed in one and copied in
 * before a call, results copied to x0 before returning. Some can share the
 * argument register; some are live while it changes (a division using x0
 * as scratch, a call's result, another argument set up first, a loop
 * carrying both). */
#include <stdio.h>
#include <string.h>

struct pg { unsigned short max, min; unsigned usable; unsigned char *data; };

static unsigned __attribute__((noinline)) sink3(unsigned a, unsigned b, unsigned c) { return a * 3 + b * 5 + c; }
static long __attribute__((noinline)) sinkl(long a, long b) { return a - 2 * b; }

/* Parameters live across a modulo whose lowering clobbers x0. */
static unsigned short __attribute__((noinline)) local_size(const struct pg *p, const unsigned char *cell, unsigned n) {
    unsigned size;
    if (n <= p->max) {
        size = n + (unsigned)(cell[0] & 3);
    } else {
        unsigned min = p->min;
        size = min + (n - min) % (p->usable - 4);
        if (size > p->max)
            size = min;
        size += cell[1];
    }
    return (unsigned short)(size ^ (unsigned)(cell - p->data));
}

/* Arguments computed in a loop and passed while the parameters still live. */
static unsigned __attribute__((noinline)) fold(const unsigned *v, int n, unsigned seed) {
    unsigned acc = seed;
    for (int i = 0; i < n; i++) {
        unsigned a = v[i] ^ acc, b = v[i] + (unsigned)i, c = acc >> 3;
        acc = sink3(b, a, c) + (unsigned)n;
    }
    return acc + (unsigned)(v[0] * 7);
}

/* Swapped arguments: x0 and x1 exchange values through temporaries. */
static long __attribute__((noinline)) swapped(long x, long y, int depth) {
    if (depth == 0)
        return sinkl(x, y);
    long r = swapped(y, x, depth - 1);
    return r + sinkl(y + r, x);
}

/* Several temporaries copied into the same argument register at different
 * points, and results copied to x0 on several paths. */
static long __attribute__((noinline)) many(const long *a, int n, long k) {
    long best = -1, sum = 0;
    for (int i = 0; i < n; i++) {
        long t = a[i] * k;
        if (t > best)
            best = sinkl(t, sum);
        sum += t >> 1;
        if (sum > 100000)
            return sinkl(sum, best);
    }
    if (best & 1)
        return best;
    return sinkl(best, sum) ^ sum;
}

/* A parameter whose register is reused for a byte read, as an inlined
 * varint loop does. */
static unsigned __attribute__((noinline)) varint(const unsigned char *p, unsigned *len) {
    unsigned v = p[0];
    const unsigned char *q = p;
    if (v >= 0x80) {
        v &= 0x7f;
        do {
            q++;
            v = (v << 7) | (*q & 0x7f);
        } while (*q >= 0x80 && q - p < 8);
    }
    *len = (unsigned)(q - p) + 1;
    return v;
}

int main(void) {
    unsigned char buf[64];
    for (int i = 0; i < 64; i++)
        buf[i] = (unsigned char)(i * 37 + 11);
    struct pg p = { 60, 20, 512, buf };
    unsigned s = 0;
    for (unsigned n = 0; n < 2000; n += 37)
        s = s * 31 + local_size(&p, buf + (n % 60), n);
    printf("local %u\n", s);

    unsigned v[16];
    for (int i = 0; i < 16; i++)
        v[i] = (unsigned)(i * 2654435761u);
    printf("fold %u %u\n", fold(v, 16, 7), fold(v + 3, 5, 0xdeadbeef));

    printf("swapped %ld %ld %ld\n", swapped(3, 10, 0), swapped(3, 10, 3), swapped(-7, 2, 6));

    long a[20];
    for (int i = 0; i < 20; i++)
        a[i] = (i * 7919) % 103 - 30;
    printf("many %ld %ld %ld\n", many(a, 20, 3), many(a, 20, 900), many(a, 0, 1));

    unsigned char vi[] = { 0x05, 0x81, 0x01, 0xff, 0xff, 0x7f, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x01 };
    unsigned tot = 0;
    for (unsigned off = 0; off < 7; off++) {
        unsigned len;
        unsigned x = varint(vi + off, &len);
        tot = tot * 131 + x + len;
    }
    printf("varint %u\n", tot);
    return 0;
}
