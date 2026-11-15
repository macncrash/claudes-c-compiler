/* __builtin_convertvector(v, T): each lane converted as by a cast (a vector
 * cast keeps the bits). ccc did not know it: the name, 23 characters long,
 * was turned away by the keyword fast path (lengths 2-17 or 28), and the
 * call failed as an undeclared function taking a type. Integer and floating
 * lanes of several widths, a 32-byte result, nested conversions, an operand
 * with a side effect (evaluated once), typeof and __has_builtin. Found by
 * the vector extension fuzzer. */
#include <stdio.h>
typedef int v4i __attribute__((vector_size(16)));
typedef unsigned v4u __attribute__((vector_size(16)));
typedef float v4f __attribute__((vector_size(16)));
typedef double v4d __attribute__((vector_size(32)));
typedef short v4s __attribute__((vector_size(8)));
typedef unsigned char v4c __attribute__((vector_size(4)));
typedef long long v4l __attribute__((vector_size(32)));

static int calls;
__attribute__((noinline)) static v4i get(int k) { calls++; return (v4i){ k, -k, k * 3, -300 }; }
__attribute__((noinline)) static v4f tof(v4i x) { return __builtin_convertvector(x, v4f); }

int main(void) {
    v4i a = { 1, -2, 300, -40000 };
    v4f f = __builtin_convertvector(a, v4f);
    printf("f %g %g %g %g\n", f[0], f[1], f[2], f[3]);
    v4f g = { 1.75f, -2.5f, 99.9f, -0.25f };
    v4i gi = __builtin_convertvector(g, v4i);
    printf("gi %d %d %d %d\n", gi[0], gi[1], gi[2], gi[3]);
    v4d d = __builtin_convertvector(a, v4d);
    printf("d %g %g %g %g %zu\n", d[0], d[1], d[2], d[3], sizeof d);
    v4s s = __builtin_convertvector(a, v4s);
    printf("s %d %d %d %d\n", s[0], s[1], s[2], s[3]);
    v4c c = __builtin_convertvector(a, v4c);
    printf("c %u %u %u %u\n", c[0], c[1], c[2], c[3]);
    v4l l = __builtin_convertvector(a, v4l);
    printf("l %lld %lld %lld %lld\n", l[0], l[1], l[2], l[3]);
    v4u u = __builtin_convertvector(a, v4u);
    printf("u %u %u %u %u\n", u[0], u[1], u[2], u[3]);
    v4d back = __builtin_convertvector(__builtin_convertvector(u, v4f), v4d);
    printf("back %g %g %g %g\n", back[0], back[1], back[2], back[3]);
    v4i once = __builtin_convertvector(__builtin_convertvector(get(7), v4l), v4i);
    printf("once %d %d %d %d calls %d\n", once[0], once[1], once[2], once[3], calls);
    __typeof__(__builtin_convertvector(a, v4d)) t = d;
    printf("t %zu %g\n", sizeof t, t[3]);
    v4f r = tof(a) + __builtin_convertvector(gi, v4f);
    printf("r %g %g %g %g\n", r[0], r[1], r[2], r[3]);
#if __has_builtin(__builtin_convertvector)
    printf("hb 1\n");
#else
    printf("hb 0\n");
#endif
    return 0;
}
