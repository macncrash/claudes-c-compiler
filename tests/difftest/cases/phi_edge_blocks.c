/* Values merged along critical edges, whose copies phi elimination puts in
 * blocks of their own: before the block they jump to on a loop's back
 * edge, after a block that calls, or where nothing falls through into it,
 * and at the end of the function otherwise. Covers swaps around a loop,
 * a constant on one path and a call on the other (perft's
 * `n += depth == 1 ? 1 : perft()`), merges with no call, nested loops, a
 * switch, and loops that run zero times. */
#include <stdio.h>

static int calls;

__attribute__((noinline)) static long leaf(long v) { calls++; return v * 3 + 1; }

/* Fibonacci-style swap carried around the loop. */
__attribute__((noinline)) static long swaps(int n) {
    long a = 0, b = 1;
    for (int i = 0; i < n; i++) {
        long t = a + b;
        a = b;
        b = t;
        if (t > 1000)
            b -= 1000;
    }
    return a * 10000 + b;
}

/* A constant on one path, a call on the other. */
__attribute__((noinline)) static long tree(int depth, int width) {
    long n = 0;
    for (int i = 0; i < width; i++) {
        if ((i * 7 + depth) % 5 == 0)
            continue;
        n += depth == 1 ? 1 : tree(depth - 1, width);
    }
    return n;
}

/* Merges with no call: which path sets the value. */
__attribute__((noinline)) static long merges(const int *v, int n) {
    long s = 0;
    for (int i = 0; i < n; i++) {
        long x = v[i] < 0 ? -1 : v[i] > 100 ? 100 : v[i];
        if (v[i] & 1)
            x = x * 2;
        s = s * 31 + x;
    }
    return s;
}

/* Nested loops whose inner one may not run. */
__attribute__((noinline)) static long nested(int n) {
    long s = 0;
    for (int i = 0; i < n; i++) {
        long t = i;
        for (int j = i; j < n - i; j++)
            t = t * 3 % 1009 + j;
        s += t;
    }
    return s;
}

/* A switch merging values, with a call on one case. */
__attribute__((noinline)) static long cases(int n) {
    long s = 0;
    for (int i = 0; i < n; i++) {
        long x;
        switch (i % 4) {
        case 0: x = 1; break;
        case 1: x = leaf(i); break;
        case 2: x = -i; break;
        default: x = s & 15; break;
        }
        s += x;
    }
    return s;
}

/* A do-while whose continue jumps to the test. */
__attribute__((noinline)) static long dowhile(int n) {
    long s = 0, k = 0;
    do {
        k++;
        if (k % 3 == 0)
            continue;
        s += k * k;
    } while (k < n);
    return s;
}

int main(void) {
    int v[12] = {5, -3, 120, 7, 0, 99, 101, -50, 42, 13, 64, 1};
    for (int n = 0; n <= 12; n += 6)
        printf("%d: %ld %ld %ld %ld %ld %ld\n", n, swaps(n * 5), tree(n % 4 + 1, 6),
               merges(v, n), nested(n), cases(n * 3), dowhile(n));
    printf("calls %d\n", calls);
    return 0;
}
