/* Loops walking a pointer read once per iteration, whose step moves next to
 * the load (a post-indexed load on AArch64): loads of each width and
 * signedness, steps down and steps too large for the immediate, and the
 * shapes that must keep the step in the latch: the pointer read twice, read
 * after the loop, left by a break, stored through, and used in an inner loop. */
#include <stdio.h>
#include <string.h>

struct big { int a[80]; };

static int ints[64];
static unsigned char bytes[64];
static short shorts[64];
static signed char sbytes[64];
static long longs[64];
static struct big bigs[4];

__attribute__((noinline)) static unsigned long sum_ints(const int *p, const int *end) {
    unsigned long s = 0;
    while (p < end)
        s = s * 3 + *p++;
    return s;
}

__attribute__((noinline)) static unsigned long sum_widths(int n) {
    const unsigned char *b = bytes;
    const short *h = shorts;
    const signed char *c = sbytes;
    const long *l = longs;
    unsigned long s = 0;
    for (const unsigned char *e = b + n; b < e; b++)
        s += *b;
    for (const short *e = h + n; h < e; h++)
        s = s * 5 + *h;
    for (const signed char *e = c + n; c < e; c++)
        s = s * 7 + *c;
    for (const long *e = l + n; l < e; l++)
        s ^= *l;
    return s;
}

__attribute__((noinline)) static unsigned long down(const int *p, const int *begin) {
    unsigned long s = 0;
    while (p > begin)
        s = s * 3 + *--p;
    for (const int *q = begin + 63; q >= begin; q--)
        s += *q;
    return s;
}

__attribute__((noinline)) static long big_steps(int n) {
    long s = 0;
    for (const struct big *p = bigs; p < bigs + n; p++)
        s += p->a[0] + p->a[79];
    return s;
}

/* The pointer is read again in the body. */
__attribute__((noinline)) static unsigned long read_twice(const int *p, const int *end) {
    unsigned long s = 0;
    while (p < end) {
        s += *p;
        s += (unsigned long)(end - p);
        p++;
    }
    return s;
}

/* The pointer is used after the loop. */
__attribute__((noinline)) static long after_loop(const int *p, const int *end) {
    long s = 0;
    while (p < end) {
        if (*p == 42)
            break;
        p++;
    }
    s = end - p;
    return s * 100 + (p < end ? *p : -1);
}

/* A break between the load and the step. */
__attribute__((noinline)) static const int *find(const int *p, const int *end, int v) {
    while (p < end) {
        int x = *p;
        if (x == v)
            return p;
        p++;
    }
    return 0;
}

/* Stores through the pointer. */
__attribute__((noinline)) static void fill(int *p, int *end, int v) {
    while (p < end) {
        *p = v;
        v = (v * 3 + 1) & 0xffff;
        p++;
    }
}

__attribute__((noinline)) static void scale(int *p, int *end) {
    while (p < end) {
        int x = *p;
        *p = x * 2 + 1;
        p++;
    }
}

/* The pointer is walked by an inner loop too. */
__attribute__((noinline)) static unsigned long nested(const int *p, int rows, int cols) {
    unsigned long s = 0;
    for (int r = 0; r < rows; r++) {
        s += *p;
        for (int c = 1; c < cols; c++)
            s = s * 3 + p[c];
        p += cols;
    }
    return s;
}

/* Zero iterations. */
__attribute__((noinline)) static unsigned long empty(const int *p) {
    return sum_ints(p, p);
}

int main(void) {
    for (int i = 0; i < 64; i++) {
        ints[i] = i * 7 - 100;
        bytes[i] = (unsigned char)(i * 37 + 200);
        shorts[i] = (short)(i * 1013 - 30000);
        sbytes[i] = (signed char)(i * 29 - 90);
        longs[i] = (long)i * 0x123456789L;
    }
    for (int k = 0; k < 4; k++)
        for (int i = 0; i < 80; i++)
            bigs[k].a[i] = k * 100 + i;

    printf("sum_ints %lu\n", sum_ints(ints, ints + 64));
    printf("sum_widths %lu %lu\n", sum_widths(64), sum_widths(3));
    printf("down %lu\n", down(ints + 64, ints));
    printf("big_steps %ld %ld\n", big_steps(4), big_steps(0));
    printf("read_twice %lu\n", read_twice(ints, ints + 64));
    ints[20] = 42;
    printf("after_loop %ld %ld\n", after_loop(ints, ints + 64), after_loop(ints + 21, ints + 64));
    const int *f = find(ints, ints + 64, 42);
    printf("find %ld %d\n", f ? (long)(f - ints) : -1L, find(ints, ints + 64, 12345) == 0);
    int buf[40];
    memset(buf, 0, sizeof buf);
    fill(buf + 2, buf + 38, 5);
    scale(buf, buf + 40);
    unsigned long s = 0;
    for (int i = 0; i < 40; i++)
        s = s * 31 + buf[i];
    printf("fill %lu %d %d %d\n", s, buf[0], buf[2], buf[39]);
    printf("nested %lu %lu\n", nested(ints, 8, 8), nested(ints, 4, 1));
    printf("empty %lu\n", empty(ints));
    return 0;
}
