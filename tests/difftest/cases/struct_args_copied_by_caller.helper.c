struct Big { long a[5]; };
struct Al { _Alignas(64) char c[3]; int x; };
/* Callees that modify their by-value parameters: the caller's objects must not change. */
__attribute__((noinline)) long scribble(struct Big b) { b.a[0] = -1; b.a[4] = -5; return b.a[1] + b.a[4]; }
__attribute__((noinline)) int scribble_al(struct Al v) { v.x = -7; v.c[0] = 'z'; return v.c[1] + v.x; }
__attribute__((noinline)) long scribble2(struct Big b, struct Big c) { b.a[2] = 0; c.a[2] = 0; return b.a[3] + c.a[3]; }
#include <stdarg.h>
__attribute__((noinline)) long scribble_va(int n, ...) {
    va_list ap; va_start(ap, n); long s = 0;
    for (int i = 0; i < n; i++) { struct Big b = va_arg(ap, struct Big); s += b.a[i]; }
    va_end(ap); return s;
}
struct Big make_big(long k) { struct Big b = { { k, k + 1, k + 2, k + 3, k + 4 } }; return b; }
typedef long v4l __attribute__((vector_size(32)));
__attribute__((noinline)) long scribble_vec(v4l v) { v[0] = -9; return v[1] + v[3]; }
