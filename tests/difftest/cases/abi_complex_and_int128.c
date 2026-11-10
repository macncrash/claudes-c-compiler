/* Calls across the Apple arm64 ABI with the helper (always built by the
 * reference compiler), in both directions:
 * - a struct of complex members is an HFA (two members per complex);
 * - an HFA after complex arguments keeps its own class (the classes stayed
 *   in place while the complex arguments were split in two);
 * - a complex value with one FP register left goes wholly on the stack;
 * - __int128 after an int is in x1:x2 (Apple does not start it at an even
 *   register), and a struct passed by reference after it is in x3;
 * - va_arg(ap, __int128) reads 16 bytes at a 16-byte aligned slot, and a
 *   16-byte aligned struct on the stack, named or variadic, is aligned so;
 * - an 8-byte HFA returned through a function pointer;
 * - a variadic call through a pointer counts a complex parameter as two. */
#include <stdarg.h>
#include <stdio.h>

struct C2 { float _Complex a, b; };
struct NC { struct C2 in; };
struct H3 { double h[3]; };
struct F2 { float h[2]; };
union U22 { short a; char c[22]; };
struct Q { __int128 v; };

typedef unsigned long u64;
#define HI(x) ((u64)((unsigned __int128)(x) >> 64))
#define LO(x) ((u64)(x))

struct NC h_nc(struct NC v);
void h_after_complex(double _Complex a, double _Complex b, double c, struct H3 s, float x);
void h_last_reg(double a, double b, double c, double d, double e, double f, double g,
                double _Complex z, double y, float w);
unsigned h_i128(int a, unsigned __int128 b, float c, union U22 u);
struct F2 h_f2(int x);
int h_va_complex(float _Complex z, union U22 u, ...);
void h_q(long a, long b, long c, long d, long e, long f, long g, long h, char i, struct Q q, char j);
void call_back(void);

struct NC c_nc(struct NC v)
{
    printf("c_nc %a %a %a %a\n", __real__ v.in.a, __imag__ v.in.a, __real__ v.in.b, __imag__ v.in.b);
    return (struct NC){ { v.in.b, v.in.a } };
}

void c_last_reg(double a, double b, double c, double d, double e, double f, double g,
                double _Complex z, double y, float w)
{
    printf("c_last_reg %a %a %a %a %a\n", g, __real__ z, __imag__ z, y, w);
}

unsigned c_i128(int a, unsigned __int128 b, float c, union U22 u)
{
    printf("c_i128 %d %lu %lu %a %d\n", a, HI(b), LO(b), c, u.a);
    return 9;
}

int c_va_i128(int n, ...)
{
    va_list ap;
    va_start(ap, n);
    __int128 v = va_arg(ap, __int128);
    double d = va_arg(ap, double);
    va_end(ap);
    printf("c_va_i128 %d %lu %lu %a\n", n, HI(v), LO(v), d);
    return 2;
}

int c_va_q(int n, ...)
{
    va_list ap;
    va_start(ap, n);
    int i = va_arg(ap, int);
    struct Q q = va_arg(ap, struct Q);
    int j = va_arg(ap, int);
    va_end(ap);
    printf("c_va_q %d %d %lu %lu %d\n", n, i, HI(q.v), LO(q.v), j);
    return 4;
}

int main(void)
{
    struct NC r = h_nc((struct NC){ { 1.5f + 2.5f * 1.0fi, 3.5f + 4.5f * 1.0fi } });
    printf("main h_nc %a %a %a %a\n", __real__ r.in.a, __imag__ r.in.a, __real__ r.in.b, __imag__ r.in.b);
    h_after_complex(1.0 + 2.0 * 1.0i, 3.0 + 4.0 * 1.0i, 5.0, (struct H3){ { 1.5, 2.5, 3.5 } }, 7.5f);
    h_last_reg(1, 2, 3, 4, 5, 6, 7, 8.0 + 9.0 * 1.0i, 10, 11);
    unsigned __int128 b = (unsigned __int128)0xfedcba9876543210ULL << 64 | 5;
    printf("main h_i128 %u\n", h_i128(255, b, 1.5f, (union U22){ -128 }));
    struct F2 (*volatile fp)(int) = h_f2;
    struct F2 f = fp(3);
    printf("main fp %a %a\n", f.h[0], f.h[1]);
    int (*volatile vp)(float _Complex, union U22, ...) = h_va_complex;
    printf("main vp %d\n", vp(0.5f + 0.25f * 1.0fi, (union U22){ 42 }, -8L));
    h_q(1, 2, 3, 4, 5, 6, 7, 8, 11, (struct Q){ (__int128)7 << 64 | 9 }, 12);
    call_back();
    return 0;
}
