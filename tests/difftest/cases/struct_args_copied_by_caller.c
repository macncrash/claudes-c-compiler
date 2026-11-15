/* A struct argument larger than 16 bytes goes, on AArch64, by reference to a
 * copy the caller makes; the callee may change it. ccc passed the address of
 * the object itself, so the helper (built by the reference compiler), which
 * changes its parameters, changed the caller's locals and globals: through a
 * function pointer, from a returned struct, as a variadic argument, and for
 * a 32-byte vector too. Found by the over-aligned object fuzzer. */
#include <stdio.h>
struct Big { long a[5]; };
struct Al { _Alignas(64) char c[3]; int x; };
long scribble(struct Big b);
int scribble_al(struct Al v);
long scribble2(struct Big b, struct Big c);
long scribble_va(int n, ...);
struct Big make_big(long k);
typedef long v4l __attribute__((vector_size(32)));
long scribble_vec(v4l v);
static struct Big gb = { { 10, 20, 30, 40, 50 } };
int main(void) {
    struct Big b = { { 1, 2, 3, 4, 5 } };
    long r = scribble(b);
    printf("local %ld %ld %ld\n", b.a[0], b.a[4], r);
    r = scribble(gb);
    printf("global %ld %ld %ld\n", gb.a[0], gb.a[4], r);
    struct Al v = { { 'a', 'b', 'c' }, 9 };
    int q = scribble_al(v);
    printf("aligned %c %d %d\n", v.c[0], v.x, q);
    r = scribble2(b, b);
    printf("twice %ld %ld\n", b.a[2], r);
    struct Big *p = &b;
    r = scribble(*p);
    printf("deref %ld %ld\n", p->a[0], r);
    long (*volatile fp)(struct Big) = scribble;
    r = fp(b);
    printf("fnptr %ld %ld\n", b.a[0], r);
    r = scribble(make_big(100));
    printf("nested %ld\n", r);
    r = scribble_va(2, b, gb);
    printf("variadic %ld %ld %ld\n", r, b.a[0], gb.a[1]);
    v4l vv = { 1, 2, 3, 4 };
    r = scribble_vec(vv);
    printf("vector %ld %ld\n", vv[0], r);
    return 0;
}
