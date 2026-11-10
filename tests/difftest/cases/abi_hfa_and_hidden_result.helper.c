/* Built by the reference compiler: callees for the main file's calls, and
 * callers of the main file's functions. */
#include <stdarg.h>
#include <stdio.h>

struct T3 { double a, b, c; };
struct F2 { float m0, m1; };
struct D1 { double m0; };
struct F3 { float h[3]; };
struct D4 { double h[4]; };
struct Big { long a, b, c; };

/* A variadic function returning an HFA (in d0-d2) or a big struct (through x8). */
struct T3 h_hfa_va(int k, double d, ...)
{
    va_list ap;
    va_start(ap, d);
    int i = va_arg(ap, int);
    double e = va_arg(ap, double);
    va_end(ap);
    printf("h_hfa_va %d %a %d %a\n", k, d, i, e);
    return (struct T3){ k + d, i, e };
}

struct Big h_big_va(long double p, ...)
{
    va_list ap;
    va_start(ap, p);
    long x = va_arg(ap, long);
    va_end(ap);
    printf("h_big_va %La %ld\n", p, x);
    return (struct Big){ (long)p, x, 3 };
}

/* An HFA that no longer fits in the FP registers goes on the stack; the
 * char after it still takes a GP register. */
struct F2 h_overflow(double a, double b, double c, double d, double e, double f, double g,
                     struct F2 p7, unsigned char p8)
{
    printf("h_overflow %a %a %a %a %d\n", a + b + c + d + e + f, g, p7.m0, p7.m1, p8);
    return (struct F2){ p7.m1, p7.m0 };
}

/* A one-double HFA passed variadically: on the stack. */
int h_d1_va(struct F2 p, ...)
{
    va_list ap;
    va_start(ap, p);
    struct D1 v = va_arg(ap, struct D1);
    va_end(ap);
    printf("h_d1_va %a %a %a\n", p.m0, p.m1, v.m0);
    return 1;
}

/* Named HFAs on the stack are packed at their element's alignment. */
void h_packed(struct F2 a, struct F2 b, struct F2 c, struct D4 d, struct D4 e, struct F3 f,
              float g, float h)
{
    printf("h_packed %a %a %a %a | %a %a | %a %a %a | %a %a\n", a.m0, b.m1, c.m0, c.m1,
           d.h[0], e.h[3], f.h[0], f.h[1], f.h[2], g, h);
}

/* The main file's functions, called from here. */
struct T3 c_hfa_va(int k, double d, ...);
struct F2 c_overflow(double a, double b, double c, double d, double e, double f, double g,
                     struct F2 p7, unsigned char p8);
int c_d1_va(struct F2 p, ...);
void c_packed(struct F2 a, struct F2 b, struct F2 c, struct D4 d, struct D4 e, struct F3 f,
              float g, float h);

void call_back(void)
{
    struct T3 t = c_hfa_va(4, 0.5, 6, 7.25);
    printf("back c_hfa_va %a %a %a\n", t.a, t.b, t.c);
    struct F2 o = c_overflow(1, 2, 3, 4, 5, 6, 7, (struct F2){ 1.5f, -2.25f }, 200);
    printf("back c_overflow %a %a\n", o.m0, o.m1);
    printf("back c_d1_va %d\n", c_d1_va((struct F2){ 0.5f, 0.25f }, (struct D1){ 3e10 }));
    c_packed((struct F2){ 1, 2 }, (struct F2){ 3, 4 }, (struct F2){ 5, 6 },
             (struct D4){ { 7, 8, 9, 10 } }, (struct D4){ { 11, 12, 13, 14 } },
             (struct F3){ { 15, 16, 17 } }, 18, 19);
}
