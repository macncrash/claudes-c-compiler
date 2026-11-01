/* Functions that return early or tail-call on a fast path and call out on
 * a slow one, which builds the frame only there: values from the fast path
 * read after the slow path's calls (integers, pointers, doubles), slow paths
 * that loop over calls, recursion, calls through pointers, stack arguments,
 * and fast paths that write memory. Each path must give the same results. */
#include <stdio.h>
#include <string.h>

struct cursor { unsigned char state; unsigned char flags; unsigned short ix; int n; int *cells; long sum; };

static int depth_seen;

__attribute__((noinline)) static int slow_next(struct cursor *c) {
    c->sum += 1000;
    return c->ix > 3 ? 101 : 0;
}

__attribute__((noinline)) static int child(struct cursor *c, int ix) {
    c->sum += c->cells[ix % c->n];
    depth_seen++;
    return depth_seen % 3 == 0;
}

/* sqlite3BtreeNext's shape: tail calls, an early return, a loop of calls. */
__attribute__((noinline)) static int next(struct cursor *c) {
    c->flags &= ~6;
    if (c->state != 0)
        return slow_next(c);
    if (++c->ix >= c->n) {
        c->ix--;
        return slow_next(c);
    }
    if (c->cells[c->ix] & 1)
        return 0;
    int rc;
    do {
        rc = child(c, c->ix);
    } while (rc == 0 && (c->cells[c->ix] & 2) == 0);
    return rc;
}

__attribute__((noinline)) static double scale(double x) { return x * 1.5 + 0.25; }

/* A double and a pointer from before the slow path's calls, used after. */
__attribute__((noinline)) static double mix(const double *v, int n, double bias) {
    if (n <= 0)
        return bias;
    double t = bias;
    for (int i = 0; i < n; i++)
        t += scale(v[i]) * bias;
    return t + v[0];
}

__attribute__((noinline)) static long nine(long a, long b, long c, long d, long e, long f, long g, long h, long i) {
    return a - b + c * d - e + f * g - h + i * 3;
}

/* Stack arguments only on the slow path. */
__attribute__((noinline)) static long spread(long x, long y) {
    if (x == y)
        return x;
    return nine(x, y, x + y, x - y, x * 2, y * 2, x ^ y, x | y, x & y) + x;
}

static int (*hooks[2])(struct cursor *) = { slow_next, 0 };

/* A call through a pointer on the slow path; the fast path writes memory. */
__attribute__((noinline)) static int dispatch(struct cursor *c, int which, int *out) {
    *out = which * 7;
    if (which < 0 || !hooks[which & 1])
        return -1;
    int r = hooks[which & 1](c);
    return r + *out + c->ix;
}

/* Recursion on the slow path. */
__attribute__((noinline)) static long tri(long n, long acc) {
    if (n <= 0)
        return acc;
    return tri(n - 1, acc + n) + (n & 1);
}

/* Several values from the fast path live across a call. */
__attribute__((noinline)) static long many(long a, long b, long c, long d, char *buf) {
    if (a < 0)
        return -a;
    long s = a * 3 + b;
    long t = c - d;
    int len = snprintf(buf, 32, "%ld:%ld", s, t);
    return s + t + len + a + b + c + d;
}

int main(void) {
    int cells[8] = { 2, 5, 8, 3, 6, 9, 4, 1 };
    long total = 0;
    for (int round = 0; round < 3; round++) {
        struct cursor c = { (unsigned char)(round == 2), 0xff, 0, 8, cells, 0 };
        for (int k = 0; k < 10; k++) {
            int r = next(&c);
            total += r * 31 + c.ix + c.flags + c.sum;
        }
        printf("next round %d: ix %u flags %u sum %ld\n", round, c.ix, c.flags, c.sum);
    }
    double v[5] = { 1.0, -2.5, 3.25, 0.5, 8.0 };
    printf("mix %.4f %.4f %.4f\n", mix(v, 0, 2.0), mix(v, 5, 0.5), mix(v, 3, -1.0));
    printf("spread %ld %ld %ld\n", spread(4, 4), spread(10, 3), spread(-7, 12));
    struct cursor d = { 1, 0, 9, 8, cells, 0 };
    int out = 0;
    int r1 = dispatch(&d, 0, &out);
    int o1 = out;
    int r2 = dispatch(&d, 1, &out);
    int o2 = out;
    int r3 = dispatch(&d, -3, &out);
    printf("dispatch %d %d %d %d %d %d sum %ld\n", r1, o1, r2, o2, r3, out, d.sum);
    printf("tri %ld %ld %ld\n", tri(0, 5), tri(1, 0), tri(40, 2));
    char buf[32];
    long m1 = many(-9, 1, 2, 3, buf);
    long m2 = many(5, 6, 7, 1, buf);
    printf("many %ld %ld %s\n", m1, m2, buf);
    printf("total %ld depth %d\n", total, depth_seen);
    return 0;
}
