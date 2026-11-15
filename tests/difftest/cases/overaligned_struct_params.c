/* Over-aligned struct parameters (aligned(32), _Alignas(64) and aligned(16)
 * members): ccc copied them into a parameter slot of the default 8-byte
 * alignment, so the parameter sat misaligned in the callee. Found by the
 * over-aligned object fuzzer. */
#include <stdio.h>
#include <stdint.h>
struct A2 { _Alignas(64) char c[3]; int x; };
struct A3 { char c[24]; } __attribute__((aligned(32)));
struct A4 { char c; long long m __attribute__((aligned(16))); };
#define AL(p, n) ((int)(((uintptr_t)(p)) % (n) == 0))
__attribute__((noinline)) struct A2 pass2(struct A2 v, int t) { printf("callee A2 %d %d %d\n", AL(&v, 64), v.c[2], v.x); v.x += t; return v; }
__attribute__((noinline)) struct A3 pass3(struct A3 v) { printf("callee A3 %d %d\n", AL(&v, 32), v.c[23]); v.c[0]++; return v; }
__attribute__((noinline)) struct A4 pass4(int a, struct A4 v) { printf("callee A4 %d %lld\n", AL(&v, 16), v.m); v.m += a; return v; }
int main(void) {
    char pad = 1;
    struct A2 a = { { 1, 2, 3 }, 40 };
    struct A2 r = pass2(a, 2);
    printf("caller A2 %d %d %d\n", AL(&r, 64), r.x, pad);
    struct A3 b = { { 5 } };
    b.c[23] = 7;
    struct A3 s = pass3(b);
    printf("caller A3 %d %d\n", AL(&s, 32), s.c[0]);
    struct A4 c = { 'q', 99 };
    struct A4 u = pass4(3, c);
    printf("caller A4 %d %lld\n", AL(&u, 16), u.m);
    return 0;
}
