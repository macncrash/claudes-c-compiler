/* Calls across the Apple arm64 ABI with the helper (always built by the
 * reference compiler), in both directions:
 * - a variadic function returning an HFA or through x8: the hidden result
 *   pointer is a named argument, so the last named one stays in a register;
 * - an HFA that overflows the FP registers goes on the stack, and is not
 *   moved into x7 because of a struct return;
 * - a one-double HFA passed variadically is on the stack, not kept in a
 *   vector register;
 * - named HFAs on the stack are packed: { float h[3]; } takes 12 bytes. */
#include <stdarg.h>
#include <stdio.h>

struct T3 { double a, b, c; };
struct F2 { float m0, m1; };
struct D1 { double m0; };
struct F3 { float h[3]; };
struct D4 { double h[4]; };
struct Big { long a, b, c; };

struct T3 h_hfa_va(int k, double d, ...);
struct Big h_big_va(long double p, ...);
struct F2 h_overflow(double a, double b, double c, double d, double e, double f, double g,
                     struct F2 p7, unsigned char p8);
int h_d1_va(struct F2 p, ...);
void h_packed(struct F2 a, struct F2 b, struct F2 c, struct D4 d, struct D4 e, struct F3 f,
              float g, float h);
void call_back(void);

struct T3 c_hfa_va(int k, double d, ...)
{
    va_list ap;
    va_start(ap, d);
    int i = va_arg(ap, int);
    double e = va_arg(ap, double);
    va_end(ap);
    printf("c_hfa_va %d %a %d %a\n", k, d, i, e);
    return (struct T3){ k + d, i, e };
}

struct F2 c_overflow(double a, double b, double c, double d, double e, double f, double g,
                     struct F2 p7, unsigned char p8)
{
    printf("c_overflow %a %a %a %a %d\n", a + b + c + d + e + f, g, p7.m0, p7.m1, p8);
    return (struct F2){ p7.m1, p7.m0 };
}

int c_d1_va(struct F2 p, ...)
{
    va_list ap;
    va_start(ap, p);
    struct D1 v = va_arg(ap, struct D1);
    va_end(ap);
    printf("c_d1_va %a %a %a\n", p.m0, p.m1, v.m0);
    return 1;
}

void c_packed(struct F2 a, struct F2 b, struct F2 c, struct D4 d, struct D4 e, struct F3 f,
              float g, float h)
{
    printf("c_packed %a %a %a %a | %a %a | %a %a %a | %a %a\n", a.m0, b.m1, c.m0, c.m1,
           d.h[0], e.h[3], f.h[0], f.h[1], f.h[2], g, h);
}

int main(void)
{
    struct T3 t = h_hfa_va(1, 2.5, 7, 8.5);
    printf("main h_hfa_va %a %a %a\n", t.a, t.b, t.c);
    struct Big b = h_big_va(3e10L, 7L);
    printf("main h_big_va %ld %ld %ld\n", b.a, b.b, b.c);
    struct F2 o = h_overflow(1, 2, 3, 4, 5, 6, 7, (struct F2){ 1.5f, 2.5f }, 128);
    printf("main h_overflow %a %a\n", o.m0, o.m1);
    printf("main h_d1_va %d\n", h_d1_va((struct F2){ 1e-3f, 2.0f }, (struct D1){ 3e10 }));
    h_packed((struct F2){ 1, 2 }, (struct F2){ 3, 4 }, (struct F2){ 5, 6 },
             (struct D4){ { 7, 8, 9, 10 } }, (struct D4){ { 11, 12, 13, 14 } },
             (struct F3){ { 15, 16, 17 } }, 18, 19);
    call_back();
    return 0;
}
