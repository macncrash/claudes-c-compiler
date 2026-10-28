/* Indirect calls whose target is read where it lives at the call: loaded
 * just before it, held across other calls, kept in a stack slot under
 * pressure, and called with stack arguments, structs passed by value,
 * variadic arguments, 128-bit integers and a variable-length array live. */
#include <stdarg.h>
#include <stdio.h>

struct pair { long a, b; };
struct big { long v[5]; };
struct ops {
    long (*unary)(long);
    long (*binary)(long, long);
    long (*many)(long, long, long, long, long, long, long, long, long, long);
    long (*by_value)(struct pair, struct big, long);
    long (*varargs)(int, ...);
    __int128 (*wide)(__int128, long);
    double (*mixed)(double, long, float);
};

static long inc(long x) { return x + 1; }
static long twice(long x) { return x * 2; }
static long sub(long a, long b) { return a - b; }
static long mul(long a, long b) { return a * b; }
static long sum10(long a, long b, long c, long d, long e, long f, long g, long h, long i, long j) {
    return a + 2 * b + 3 * c + 4 * d + 5 * e + 6 * f + 7 * g + 8 * h + 9 * i + 10 * j;
}
static long take_structs(struct pair p, struct big b, long k) {
    return p.a * 100 + p.b * 10 + b.v[0] + b.v[4] + k;
}
static long vsum(int n, ...) {
    va_list ap;
    long s = 0;
    va_start(ap, n);
    for (int i = 0; i < n; i++)
        s = s * 3 + va_arg(ap, long);
    va_end(ap);
    return s;
}
static __int128 widen(__int128 x, long k) { return x * k + 7; }
static double mix(double d, long l, float f) { return d * l + f; }

static struct ops table = { inc, sub, sum10, take_structs, vsum, widen, mix };

static long __attribute__((noinline)) apply_all(struct ops *o, long seed) {
    long r = o->unary(seed);
    r = o->binary(r, seed);
    r += o->many(r, 1, 2, 3, 4, 5, 6, 7, 8, seed);
    struct pair p = { r & 7, seed };
    struct big b = { { 1, 2, 3, 4, 5 } };
    r += o->by_value(p, b, r);
    r += o->varargs(4, r, seed, 3L, 4L);
    __int128 w = o->wide(((__int128)r << 40) + seed, 3);
    r ^= (long)(w >> 40) ^ (long)w;
    r += (long)o->mixed(1.5, r & 0xff, 0.25f);
    return r;
}

/* The target is chosen once and held across other calls. */
static long __attribute__((noinline)) held(long (*f)(long, long), long n) {
    long acc = 1;
    for (long i = 1; i <= n; i++) {
        acc = f(acc, i) % 1000003;
        acc += inc(i);
    }
    return acc;
}

/* Many values live together, so the targets go to the stack. */
static long __attribute__((noinline)) pressure(long (**fs)(long), int n, long x,
                                               long (*ten)(long, long, long, long, long, long, long, long, long, long)) {
    long (*f0)(long) = fs[0], (*f1)(long) = fs[1], (*f2)(long) = fs[2];
    long a = x, b = x + 1, c = x + 2, d = x + 3, e = x + 4, g = x + 5, h = x + 6;
    long i2 = x + 7, j = x + 8, k = x + 9, l = x + 10, m = x + 11;
#define M(x) ((x) & 0xfffff)
    for (int i = 0; i < n; i++) {
        a = M(f0(a + m)); b = M(f1(b ^ a)); c = M(f2(c + b)); d = M(f0(d - c));
        e = M(f1(e + d)); g = M(f2(g ^ e)); h = M(f0(h + g)); i2 = M(f1(i2 - h));
        j = M(f2(j + i2)); k = M(f0(k ^ j)); l = M(f1(l + k)); m = M(f2(m - l));
        long s = ten(a, b, c, d, e, g, h, i2, j, k);
        a ^= s & 15;
    }
    return a + b + c + d + e + g + h + i2 + j + k + l + m;
}

/* A variable-length array is live while targets are called with stack
 * arguments. */
static long __attribute__((noinline)) with_vla(struct ops *o, int n) {
    long v[n];
    for (int i = 0; i < n; i++)
        v[i] = o->unary(i);
    long r = 0;
    for (int i = 0; i + 9 < n; i++)
        r += o->many(v[i], v[i + 1], v[i + 2], v[i + 3], v[i + 4], v[i + 5], v[i + 6], v[i + 7], v[i + 8], v[i + 9]);
    return r;
}

int main(void) {
    printf("apply %ld %ld\n", apply_all(&table, 5), apply_all(&table, -12));
    table.unary = twice;
    table.binary = mul;
    printf("apply %ld\n", apply_all(&table, 9));
    printf("held %ld %ld\n", held(mul, 20), held(sub, 30));
    long (*fs[3])(long) = { inc, twice, inc };
    printf("pressure %ld\n", pressure(fs, 25, 3, sum10));
    printf("vla %ld\n", with_vla(&table, 17));
    return 0;
}
