/* Values live between calls but across none, which may sit in the argument
 * registers x3-x8, around calls of every shape: 0 to 10 integer arguments
 * (the last two on the stack), small and large structs by value, a struct
 * returned through x8, indirect and variadic calls. Each call's argument
 * registers must reach it, and nothing a call reads may be dropped as dead. */
#include <stdio.h>
#include <stdint.h>

/* Unsigned throughout: the mixing wraps. */
typedef unsigned long W;

struct pair { W a, b; };
struct big { W v[5]; };

__attribute__((noinline)) static W f0(void) { return 7; }
__attribute__((noinline)) static W f1(W a) { return a * 3 + 1; }
__attribute__((noinline)) static W f3(W a, W b, W c) { return a - b * c; }
__attribute__((noinline)) static W f6(W a, W b, W c, W d, W e, W f) {
    return a + 2 * b + 3 * c + 4 * d + 5 * e + 6 * f;
}
__attribute__((noinline)) static W f8(W a, W b, W c, W d, W e, W f, W g, W h) {
    return a ^ b ^ (c << 1) ^ (d << 2) ^ (e << 3) ^ (f << 4) ^ (g << 5) ^ (h << 6);
}
__attribute__((noinline)) static W f10(W a, W b, W c, W d, W e, W f, W g, W h, W i, W j) {
    return f8(a, b, c, d, e, f, g, h) * 31 + i * 7 - j;
}
__attribute__((noinline)) static W by_pair(W k, struct pair p, W m) { return k * p.a - p.b + m; }
__attribute__((noinline)) static W by_big(struct big b, W k) { return b.v[0] + b.v[1] * 2 + b.v[2] * 3 + b.v[3] * 4 + b.v[4] * 5 + k; }
__attribute__((noinline)) static struct big make_big(W s) {
    struct big b;
    for (int i = 0; i < 5; i++)
        b.v[i] = s + i * i;
    return b;
}
__attribute__((noinline)) static W sum_va(int n, ...);

#include <stdarg.h>
static W sum_va(int n, ...) {
    va_list ap;
    va_start(ap, n);
    W s = 0;
    for (int i = 0; i < n; i++)
        s = s * 5 + va_arg(ap, W);
    va_end(ap);
    return s;
}

typedef W (*fn3)(W, W, W);

__attribute__((noinline)) static uint64_t mix(const W *in, int n, fn3 g) {
    uint64_t h = 0;
    for (int i = 0; i + 9 < n; i += 3) {
        W a = in[i], b = in[i + 1], c = in[i + 2], d = in[i + 3], e = in[i + 4];
        W p = a * b, q = c ^ d, r = e - a, s = b + e, t = q * 3;
        h = h * 131 + (uint64_t)(p + q + r + s + t);
        h += (uint64_t)f0();
        W x = p ^ r, y = q + s, z = t - x;
        h = h * 7 + (uint64_t)f1(x + y + z);
        W u = y * z, v = x - y;
        h ^= (uint64_t)f3(u, v, z);
        h += (uint64_t)f6(a, b, c, d, e, u);
        W w = u + v, k = in[i + 5] * 3;
        h = h * 3 + (uint64_t)f8(a, b, c, d, e, w, k, v);
        h += (uint64_t)f10(a, b, c, d, e, w, k, v, x, y);
        struct pair pr = { k, w };
        h ^= (uint64_t)by_pair(z, pr, u);
        struct big bg = make_big(k + w);
        h += (uint64_t)by_big(bg, x);
        h = h * 11 + (uint64_t)g(a, w, k);
        h += (uint64_t)sum_va(4, a, w, k, (W)i);
    }
    return h;
}

int main(void) {
    W in[300];
    uint64_t s = 88172645463325252u;
    for (int i = 0; i < 300; i++) {
        s ^= s << 13, s ^= s >> 7, s ^= s << 17;
        in[i] = (W)(s % 100000) - 50000;
    }
    printf("mix %llu\n", (unsigned long long)mix(in, 300, f3));
    printf("mix %llu\n", (unsigned long long)mix(in + 7, 250, f3));
    return 0;
}
