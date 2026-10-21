/* Signed int loop counters, which the optimizer keeps as 64-bit values
 * (signed overflow being undefined): counting up and down, bounds and
 * starts from parameters, negative values, the counter used as an index, in
 * 32-bit arithmetic, stored, converted, returned after the loop, and nested
 * and early-exit loops. Values near the int limits must stay exact. */
#include <limits.h>
#include <stdio.h>

static int a[64];
static long sums[8];

__attribute__((noinline)) static long up(int lo, int hi) {
    long s = 0;
    for (int i = lo; i < hi; i++)
        s += (long)i * 3 + a[i & 63];
    return s;
}

__attribute__((noinline)) static long down(int hi, int lo) {
    long s = 0;
    for (int i = hi; i >= lo; i -= 3)
        s = s * 7 + i;
    return s;
}

/* The counter itself is the result. */
__attribute__((noinline)) static int find(const int *p, int n, int x) {
    int i;
    for (i = 0; i < n; i++)
        if (p[i] == x)
            break;
    return i;
}

/* 32-bit arithmetic on the counter wraps per the int type. */
__attribute__((noinline)) static unsigned mix(int n) {
    unsigned h = 0;
    for (int i = 0; i < n; i++) {
        int sq = i * i * 40503;     /* 32-bit multiply, exact for these i */
        h = h * 31 + (unsigned)sq + (unsigned)i;
        a[i & 63] = i - 7;          /* stored as int */
    }
    return h;
}

/* Near the top of the int range: no step goes past INT_MAX. */
__attribute__((noinline)) static long near_max(void) {
    long s = 0;
    for (int i = INT_MAX - 5; i < INT_MAX; i++)
        s += i;
    for (int i = INT_MIN + 5; i > INT_MIN; i--)
        s += i & 0xff;
    return s;
}

__attribute__((noinline)) static long nested(int n) {
    long s = 0;
    for (int i = -n; i < n; i++)
        for (int j = i; j < n; j += 2)
            s += (long)i * j - (unsigned)j % 5;
    return s;
}

__attribute__((noinline)) static int dowhile(int n) {
    int i = n, steps = 0;
    do {
        steps++;
        i -= 2;
    } while (i > -10);
    return steps * 1000 + i;
}

int main(int argc, char **argv) {
    (void)argv;
    for (int i = 0; i < 64; i++)
        a[i] = i * i - 50;
    printf("up %ld %ld %ld\n", up(0, 64), up(-20, 10), up(5, 5));
    printf("down %ld %ld\n", down(40, -9), down(-1, 0));
    printf("find %d %d\n", find(a, 64, 31), find(a, 64, 12345));
    printf("mix %u %d %d\n", mix(100), a[0], a[63]);
    printf("near %ld\n", near_max());
    printf("nested %ld\n", nested(argc + 6));
    printf("dowhile %d %d\n", dowhile(7), dowhile(-30));
    for (int i = 7; i >= 0; i--)
        sums[i] = (long)i << 40;
    printf("sums %lx %lx\n", sums[1], sums[7]);
    return 0;
}
