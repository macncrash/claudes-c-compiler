/* Loops whose last blocks, reached only by jumps, are laid out before the
 * header so the latch falls into it: tails reached by `continue`, a
 * `cond ? constant : call()` arm (perft's shape), an inner loop inside the
 * moved blocks, breaks and early returns out of the loop, do-while loops,
 * and loops that run zero times. */
#include <stdio.h>

static int calls;

__attribute__((noinline)) static long f(long v) { calls++; return v % 7 + 2; }

/* perft's shape: skip some, then a constant or a call. */
__attribute__((noinline)) static long tree(int depth, int width) {
    long n = 0;
    for (int i = 0; i < width; i++) {
        if ((i + depth) % 3 == 0)
            continue;
        n += depth == 1 ? 1 : tree(depth - 1, width);
    }
    return n;
}

/* A tail reached by continue from two places. */
__attribute__((noinline)) static long continues(const int *v, int n) {
    long s = 0;
    for (int i = 0; i < n; i++) {
        if (v[i] < 0)
            continue;
        if (v[i] > 50) {
            s += f(v[i]);
            continue;
        }
        s = s * 3 + v[i];
    }
    return s;
}

/* An inner loop in the outer loop's tail. */
__attribute__((noinline)) static long inner_tail(int n) {
    long s = 0;
    for (int i = 0; i < n; i++) {
        if (i & 1)
            s += f(i);
        else
            s -= i;
        for (int j = 0; j < i % 4; j++)
            s += j * i;
    }
    return s;
}

/* Breaks and an early return. */
__attribute__((noinline)) static long exits(const int *v, int n, int stop) {
    long s = 0;
    for (int i = 0; i < n; i++) {
        if (v[i] == stop)
            break;
        if (v[i] == -stop)
            return -s;
        s += v[i] > 10 ? f(v[i]) : 1;
    }
    return s;
}

/* A do-while with a tail reached by continue. */
__attribute__((noinline)) static long dowhile(int n) {
    long s = 0;
    int i = 0;
    do {
        i++;
        if (i % 4 == 0)
            continue;
        s += i % 3 == 0 ? f(i) : i;
    } while (i < n);
    return s;
}

int main(void) {
    int v[10] = {4, -1, 70, 12, 3, -9, 55, 9, 100, 2};
    for (int n = 0; n <= 10; n += 5)
        printf("%d: %ld %ld %ld %ld %ld %ld %ld\n", n, tree(n % 4 + 1, 5), continues(v, n),
               inner_tail(n), exits(v, n, 9), exits(v, n, 3), exits(v, n, 7), dowhile(n));
    printf("calls %d\n", calls);
    return 0;
}
