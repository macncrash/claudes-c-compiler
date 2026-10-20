/* AArch64 homogeneous floating-point aggregates (structs of 1-4 identical
 * float or double members, including arrays and nested structs) are passed
 * and returned in s/d registers. Calls go both ways across the ccc/clang
 * boundary (abi_hfa.helper.c). */
#include <stdarg.h>
#include <stdio.h>

struct f1 { float a; };
struct f2 { float a, b; };
struct f3 { float a, b, c; };
struct f4 { float a, b, c, d; };
struct d1 { double a; };
struct d2 { double a, b; };
struct d3 { double a, b, c; };
struct d4 { double a, b, c, d; };
struct farr { float v[4]; };
struct dnest { struct d2 p; double c; };
struct notfa { float a; double b; };
struct big5 { float a, b, c, d, e; };

double h_args(struct f1 a, struct f2 b, struct f3 c, struct d1 d);
double h_d4(struct d4 x);
double h_f4(struct f4 x);
double h_arr(struct farr x);
double h_nest(struct dnest x);
double h_notfa(struct notfa x);
double h_big5(struct big5 x);
double h_overflow(double a, double b, double c, double d, double e, double f,
                  struct d3 x, double y);
double h_varargs(int n, ...);
double h_va_d3(int n, ...);
struct f1 h_ret_f1(float a);
struct f2 h_ret_f2(float a);
struct f3 h_ret_f3(float a);
struct f4 h_ret_f4(float a);
struct d1 h_ret_d1(double a);
struct d2 h_ret_d2(double a);
struct d3 h_ret_d3(double a);
struct d4 h_ret_d4(double a);
struct farr h_ret_arr(float a);
struct dnest h_ret_nest(double a);
double h_call_ccc(void);
extern double (*h_d4_ptr)(struct d4);
extern struct f3 (*h_ret_f3_ptr)(float);

/* Called from the helper (clang caller, ccc callee). */
double c_args(struct f1 a, struct f2 b, struct f3 c, struct d1 d) {
    return a.a + b.a * 2 + b.b * 3 + c.a * 4 + c.b * 5 + c.c * 6 + d.a * 7;
}
double c_d4(struct d4 x) { return x.a + x.b * 2 + x.c * 3 + x.d * 4; }
double c_overflow(double a, double b, double c, double d, double e, double f,
                  struct d3 x, double y) {
    return a + b + c + d + e + f + x.a * 10 + x.b * 100 + x.c * 1000 + y * 10000;
}
double c_varargs(int n, ...) {
    va_list ap;
    va_start(ap, n);
    double sum = 0;
    for (int i = 0; i < n; i++) {
        struct d2 p = va_arg(ap, struct d2);
        sum += p.a * (i + 1) + p.b * 10 * (i + 1);
    }
    va_end(ap);
    return sum;
}
double c_va_d3(int n, ...) {
    va_list ap;
    va_start(ap, n);
    double sum = 0;
    for (int i = 0; i < n; i++) {
        struct d3 p = va_arg(ap, struct d3);
        sum += (p.a + p.b * 10 + p.c * 100) * (i + 1);
    }
    va_end(ap);
    return sum;
}
struct f1 c_ret_f1(float a) { struct f1 r = {a}; return r; }
struct f2 c_ret_f2(float a) { struct f2 r = {a, a * 2}; return r; }
struct f3 c_ret_f3(float a) { struct f3 r = {a, a * 2, a * 3}; return r; }
struct d2 c_ret_d2(double a) { struct d2 r = {a, a * 2}; return r; }
struct d4 c_ret_d4(double a) { struct d4 r = {a, a * 2, a * 3, a * 4}; return r; }
struct dnest c_ret_nest(double a) { struct dnest r = {{a, a + 1}, a + 2}; return r; }

int main(void) {
    struct f1 a = {1}; struct f2 b = {2, 3}; struct f3 c = {4, 5, 6}; struct d1 d = {7};
    printf("args %.2f\n", h_args(a, b, c, d));
    printf("d4 %.2f\n", h_d4((struct d4){1, 2, 3, 4}));
    printf("f4 %.2f\n", h_f4((struct f4){1, 2, 3, 4}));
    printf("arr %.2f\n", h_arr((struct farr){{1, 2, 3, 4}}));
    printf("nest %.2f\n", h_nest((struct dnest){{1, 2}, 3}));
    printf("notfa %.2f\n", h_notfa((struct notfa){1, 2}));
    printf("big5 %.2f\n", h_big5((struct big5){1, 2, 3, 4, 5}));
    printf("overflow %.2f\n", h_overflow(1, 2, 3, 4, 5, 6, (struct d3){1, 2, 3}, 7));
    printf("varargs %.2f\n", h_varargs(2, (struct d2){1, 2}, (struct d2){3, 4}));
    printf("va_d3 %.2f\n", h_va_d3(2, (struct d3){1, 2, 3}, (struct d3){4, 5, 6}));
    printf("c_va_d3 %.2f\n", c_va_d3(2, (struct d3){1, 2, 3}, (struct d3){4, 5, 6}));

    struct f1 r1 = h_ret_f1(1.5f); printf("ret_f1 %.2f\n", r1.a);
    struct f2 r2 = h_ret_f2(1.5f); printf("ret_f2 %.2f %.2f\n", r2.a, r2.b);
    struct f3 r3 = h_ret_f3(1.5f); printf("ret_f3 %.2f %.2f %.2f\n", r3.a, r3.b, r3.c);
    struct f4 r4 = h_ret_f4(1.5f); printf("ret_f4 %.2f %.2f %.2f %.2f\n", r4.a, r4.b, r4.c, r4.d);
    struct d1 s1 = h_ret_d1(2.5); printf("ret_d1 %.2f\n", s1.a);
    struct d2 s2 = h_ret_d2(2.5); printf("ret_d2 %.2f %.2f\n", s2.a, s2.b);
    struct d3 s3 = h_ret_d3(2.5); printf("ret_d3 %.2f %.2f %.2f\n", s3.a, s3.b, s3.c);
    struct d4 s4 = h_ret_d4(2.5); printf("ret_d4 %.2f %.2f %.2f %.2f\n", s4.a, s4.b, s4.c, s4.d);
    struct farr ra = h_ret_arr(1.0f); printf("ret_arr %.2f %.2f %.2f %.2f\n", ra.v[0], ra.v[1], ra.v[2], ra.v[3]);
    struct dnest rn = h_ret_nest(1.0); printf("ret_nest %.2f %.2f %.2f\n", rn.p.a, rn.p.b, rn.c);

    printf("call_ccc %.2f\n", h_call_ccc());
    printf("c_args %.2f\n", c_args(a, b, c, d));
    printf("c_overflow %.2f\n", c_overflow(1, 2, 3, 4, 5, 6, (struct d3){1, 2, 3}, 7));
    printf("c_varargs %.2f\n", c_varargs(2, (struct d2){1, 2}, (struct d2){3, 4}));
    struct d4 cd = c_ret_d4(2.5); printf("c_ret_d4 %.2f %.2f %.2f %.2f\n", cd.a, cd.b, cd.c, cd.d);

    printf("fptr_d4 %.2f\n", h_d4_ptr((struct d4){1, 2, 3, 4}));
    struct f3 pf = h_ret_f3_ptr(2.0f); printf("fptr_ret_f3 %.2f %.2f %.2f\n", pf.a, pf.b, pf.c);
    return 0;
}
