/* Constant vector broadcasts kept in registers across calls, which clobber
 * them: each must be rebuilt before its next use after a call, though not
 * after calls that reach no use without another call first. Constants used
 * on both sides of a call in one loop, a conditional call, a constant first
 * used after a call, and loops of calls between loops that use constants. */
#include <stdio.h>

typedef unsigned u4 __attribute__((vector_size(16)));
typedef unsigned u2 __attribute__((vector_size(8)));
typedef float f4 __attribute__((vector_size(16)));
typedef double d2 __attribute__((vector_size(16)));

typedef unsigned long long u64;
static u64 h;

__attribute__((noinline)) static void mix(const void *p, unsigned n) {
    const unsigned char *b = p;
    for (unsigned i = 0; i < n; i++)
        h = (h ^ b[i]) * 0x100000001b3ULL;
}

__attribute__((noinline)) static u2 step(u2 acc, u2 v, unsigned k) {
    return acc * 3u + (v ^ k);
}

static volatile int ticks;
__attribute__((noinline)) static void tick(void) { ticks++; }

#define N 64
static u4 a[N];
static f4 f[N];
static d2 d[N];
static u2 acc[N];

/* Constants on both sides of the call. */
__attribute__((noinline)) static void around(int n) {
    for (int i = 0; i < n; i++) {
        a[i] = a[i] * 2654435761u + 17u;
        tick();
        a[i] = (a[i] ^ 0x9e3779b9u) + 17u;
    }
}

/* A call on some iterations only. */
__attribute__((noinline)) static void sometimes(int n) {
    for (int i = 0; i < n; i++) {
        if (i % 3 == 0)
            mix(&a[i], sizeof a[i]);
        f[i] = f[i] * 0.75f + 1.25f;
    }
}

/* The constants are first used after a call. */
__attribute__((noinline)) static void after(int n) {
    tick();
    for (int i = 0; i < n; i++)
        d[i] = d[i] * 1.5 - 0.25;
    tick();
    for (int i = 0; i < n; i++)
        d[i] = d[i] * 0.5 + 0.25;
}

/* As in a benchmark: loops using constants, a loop of calls, and another
 * call before the constants are used again. */
__attribute__((noinline)) static void rounds(int reps) {
    for (int r = 0; r < reps; r++) {
        for (int i = 0; i < N; i++)
            f[i] = f[i] * 0.5f + 2.0f;
        for (int i = 0; i < N; i++)
            a[i] = (a[i] ^ 0x5bd1e995u) * 2654435761u;
        for (int i = 0; i < N; i++)
            acc[i] = step(acc[i], (u2){ (unsigned)i, -(unsigned)i }, i & 7);
        tick();
        for (int i = 0; i < N; i++)
            d[i] = d[i] * 0.5 + 1.0;
        for (int i = 0; i < N; i++)
            acc[i] = step(acc[i], (u2){ (unsigned)r, (unsigned)i }, 3);
    }
}

int main(void) {
    for (int i = 0; i < N; i++) {
        a[i] = (u4){ i, i * 7u, i * 13u + 1, ~(unsigned)i };
        f[i] = (f4){ i, i * 0.5f, -i, 3.0f };
        d[i] = (d2){ i * 0.25, 1.0 - i };
        acc[i] = (u2){ i, i * 3u };
    }
    around(N);
    sometimes(N);
    after(N);
    rounds(5);
    mix(a, sizeof a);
    mix(f, sizeof f);
    mix(d, sizeof d);
    mix(acc, sizeof acc);
    printf("%llu %d\n", h, ticks);
    return 0;
}
