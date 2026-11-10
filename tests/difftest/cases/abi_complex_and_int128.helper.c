/* Built by the reference compiler: callees for the main file's calls, and
 * callers of the main file's functions. */
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

/* A struct of complex members is an HFA: { float _Complex a, b; } in s0-s3. */
struct NC h_nc(struct NC v)
{
    printf("h_nc %a %a %a %a\n", __real__ v.in.a, __imag__ v.in.a, __real__ v.in.b, __imag__ v.in.b);
    return (struct NC){ { v.in.b, v.in.a } };
}

/* An HFA after complex arguments still fits in d5-d7. */
void h_after_complex(double _Complex a, double _Complex b, double c, struct H3 s, float x)
{
    printf("h_after_complex %a %a %a | %a %a %a | %a\n", __real__ a, __imag__ b, c, s.h[0], s.h[1], s.h[2], x);
}

/* A complex value with one FP register left goes wholly on the stack. */
void h_last_reg(double a, double b, double c, double d, double e, double f, double g,
                double _Complex z, double y, float w)
{
    printf("h_last_reg %a %a %a %a %a\n", g, __real__ z, __imag__ z, y, w);
}

/* __int128 after an int: x1:x2 on Apple arm64, then a union by reference in x3. */
unsigned h_i128(int a, unsigned __int128 b, float c, union U22 u)
{
    printf("h_i128 %d %lu %lu %a %d\n", a, HI(b), LO(b), c, u.a);
    return 7;
}

struct F2 h_f2(int x)
{
    return (struct F2){ { x, -7.0f } };
}

/* Named complex float, then variadic arguments. */
int h_va_complex(float _Complex z, union U22 u, ...)
{
    va_list ap;
    va_start(ap, u);
    long l = va_arg(ap, long);
    va_end(ap);
    printf("h_va_complex %a %a %d %ld\n", __real__ z, __imag__ z, u.a, l);
    return 3;
}

/* A 16-byte aligned struct on the stack starts at a 16-byte boundary. */
void h_q(long a, long b, long c, long d, long e, long f, long g, long h, char i, struct Q q, char j)
{
    printf("h_q %d %lu %lu %d\n", i, HI(q.v), LO(q.v), j);
}

/* The main file's functions, called from here. */
struct NC c_nc(struct NC v);
void c_last_reg(double a, double b, double c, double d, double e, double f, double g,
                double _Complex z, double y, float w);
unsigned c_i128(int a, unsigned __int128 b, float c, union U22 u);
int c_va_i128(int n, ...);
int c_va_q(int n, ...);

void call_back(void)
{
    struct NC r = c_nc((struct NC){ { 1.5f + 2.5f * 1.0fi, 3.5f + 4.5f * 1.0fi } });
    printf("back c_nc %a %a %a %a\n", __real__ r.in.a, __imag__ r.in.a, __real__ r.in.b, __imag__ r.in.b);
    c_last_reg(1, 2, 3, 4, 5, 6, 7, 8.0 + 9.0 * 1.0i, 10, 11);
    unsigned __int128 b = (unsigned __int128)0xfedcba9876543210ULL << 64 | 5;
    printf("back c_i128 %u\n", c_i128(255, b, 1.5f, (union U22){ -128 }));
    __int128 v = -((__int128)0x123456789 << 64 | 0x8000000000000000ULL);
    printf("back c_va_i128 %d\n", c_va_i128(1, v, 0.25));
    printf("back c_va_q %d\n", c_va_q(1, 11, (struct Q){ v }, 12));
}
