/* Vector variables kept in registers across calls, which clobber them: each
 * is saved before a call only when it may have changed since it was last
 * saved, and reloaded after one, or at a loop's exit, only when it is read
 * before the next call. Variables set before loops of calls and used after
 * them, written on some paths only, written after the call and read before
 * it on the next iteration, built lane by lane, parameters, arguments, and
 * loops left early. */
#include <stdio.h>

typedef unsigned u4 __attribute__((vector_size(16)));
typedef unsigned u2 __attribute__((vector_size(8)));
typedef double d2 __attribute__((vector_size(16)));

typedef unsigned long long u64;
static u64 h;

__attribute__((noinline)) static void mix(const void *p, unsigned n) {
    const unsigned char *b = p;
    for (unsigned i = 0; i < n; i++)
        h = (h ^ b[i]) * 0x100000001b3ULL;
}

static volatile int ticks;
__attribute__((noinline)) static void tick(void) { ticks++; }

__attribute__((noinline)) static u2 step(u2 acc, u2 v, unsigned k) {
    return acc * 3u + (v ^ k);
}

__attribute__((noinline)) static u4 twist(u4 v) {
    return (v << 3) ^ (v >> 5);
}

/* Set before a loop of calls, used after it. */
__attribute__((noinline)) static void before_after(int n, unsigned s) {
    u4 k = { s, s + 1, s * 3, ~s };
    u4 m = { 1, 2, 3, 4 };
    m[2] = s;
    for (int i = 0; i < n; i++)
        tick();
    u4 r = k * m + k;
    mix(&r, sizeof r);
}

/* Written on some iterations only; the call on others. */
__attribute__((noinline)) static void some_paths(int n) {
    u4 x = { 7, 11, 13, 17 };
    u4 y = { 1, 1, 1, 1 };
    for (int i = 0; i < n; i++) {
        if (i % 3 == 0)
            x = x * 5u + y;
        else
            tick();
        if (i % 4 == 1) {
            y = y ^ x;
            tick();
        }
        y = y + 1u;
    }
    mix(&x, sizeof x);
    mix(&y, sizeof y);
}

/* Written after the call, read before it on the next iteration. */
__attribute__((noinline)) static void across_iterations(int n) {
    u4 x = { 3, 5, 7, 9 };
    for (int i = 0; i < n; i++) {
        u4 t = x * 3u;
        tick();
        x = t ^ x;
        x[i & 3] += i;
    }
    mix(&x, sizeof x);
}

/* A parameter and an argument, through nested loops. */
__attribute__((noinline)) static u4 nested(u4 p, int n) {
    u4 acc = p;
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < 3; j++)
            acc = twist(acc) + p;
        acc = acc ^ (u4){ i, i, i, i };
    }
    return acc + p;
}

/* Left early, by break and by return, from loops with calls. */
__attribute__((noinline)) static u4 early(int n, unsigned stop) {
    u4 a = { 2, 4, 6, 8 };
    u4 b = { 1, 3, 5, 7 };
    for (int i = 0; i < n; i++) {
        a = twist(a);
        if ((a[0] & 15) == stop)
            break;
        if (a[1] % 97 == stop)
            return a * b;
        tick();
    }
    u2 c = { 9, 10 };
    for (int i = 0; i < n; i++)
        c = step(c, (u2){ i, a[i & 3] }, i & 7);
    b[0] += c[0];
    b[1] += c[1];
    return a + b;
}

/* Doubles kept across a call loop, modified in an outer loop. */
__attribute__((noinline)) static void outer(int reps) {
    d2 s = { 0.5, 1.5 };
    d2 t = { 2.0, -1.0 };
    for (int r = 0; r < reps; r++) {
        for (int i = 0; i < 5; i++)
            tick();
        s = s * t + 0.25;
        if (r & 1)
            t = t * 0.5;
    }
    mix(&s, sizeof s);
    mix(&t, sizeof t);
}

int main(void) {
    for (unsigned s = 0; s < 5; s++)
        before_after((int)s * 3, s * 0x9e3779b9u);
    some_paths(40);
    across_iterations(33);
    u4 v = nested((u4){ 1, 2, 3, 4 }, 9);
    mix(&v, sizeof v);
    for (unsigned stop = 0; stop < 16; stop += 5) {
        v = early(20, stop);
        mix(&v, sizeof v);
    }
    outer(7);
    printf("%llu %d\n", h, ticks);
    return 0;
}
