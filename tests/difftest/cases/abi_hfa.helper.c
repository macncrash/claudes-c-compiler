#include <stdarg.h>

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
struct notfa { float a; double b; };   /* mixed: not an HFA */
struct big5 { float a, b, c, d, e; }; /* 5 members: not an HFA */

double h_args(struct f1 a, struct f2 b, struct f3 c, struct d1 d) {
    return a.a + b.a * 2 + b.b * 3 + c.a * 4 + c.b * 5 + c.c * 6 + d.a * 7;
}
double h_d4(struct d4 x) { return x.a + x.b * 2 + x.c * 3 + x.d * 4; }
double h_f4(struct f4 x) { return x.a + x.b * 2 + x.c * 3 + x.d * 4; }
double h_arr(struct farr x) { return x.v[0] + x.v[1] * 2 + x.v[2] * 3 + x.v[3] * 4; }
double h_nest(struct dnest x) { return x.p.a + x.p.b * 2 + x.c * 3; }
double h_notfa(struct notfa x) { return x.a + x.b * 2; }
double h_big5(struct big5 x) { return x.a + x.b * 2 + x.c * 3 + x.d * 4 + x.e * 5; }

/* d0-d5 hold a..f; x needs 3 registers but only 2 remain, so it goes on
 * the stack and no later argument may use d6/d7; y goes on the stack too. */
double h_overflow(double a, double b, double c, double d, double e, double f,
                  struct d3 x, double y) {
    return a + b + c + d + e + f + x.a * 10 + x.b * 100 + x.c * 1000 + y * 10000;
}

double h_varargs(int n, ...) {
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

/* An HFA over 16 bytes is still passed by value, also as a variadic arg. */
double h_va_d3(int n, ...) {
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

struct f1 h_ret_f1(float a) { struct f1 r = {a}; return r; }
struct f2 h_ret_f2(float a) { struct f2 r = {a, a * 2}; return r; }
struct f3 h_ret_f3(float a) { struct f3 r = {a, a * 2, a * 3}; return r; }
struct f4 h_ret_f4(float a) { struct f4 r = {a, a * 2, a * 3, a * 4}; return r; }
struct d1 h_ret_d1(double a) { struct d1 r = {a}; return r; }
struct d2 h_ret_d2(double a) { struct d2 r = {a, a * 2}; return r; }
struct d3 h_ret_d3(double a) { struct d3 r = {a, a * 2, a * 3}; return r; }
struct d4 h_ret_d4(double a) { struct d4 r = {a, a * 2, a * 3, a * 4}; return r; }
struct farr h_ret_arr(float a) { struct farr r = {{a, a + 1, a + 2, a + 3}}; return r; }
struct dnest h_ret_nest(double a) { struct dnest r = {{a, a + 1}, a + 2}; return r; }

/* clang caller, ccc callee */
double c_args(struct f1 a, struct f2 b, struct f3 c, struct d1 d);
double c_d4(struct d4 x);
double c_overflow(double a, double b, double c, double d, double e, double f,
                  struct d3 x, double y);
double c_varargs(int n, ...);
double c_va_d3(int n, ...);
struct f1 c_ret_f1(float a);
struct f2 c_ret_f2(float a);
struct f3 c_ret_f3(float a);
struct d2 c_ret_d2(double a);
struct d4 c_ret_d4(double a);
struct dnest c_ret_nest(double a);

double h_call_ccc(void) {
    struct f1 a = {1}; struct f2 b = {2, 3}; struct f3 c = {4, 5, 6}; struct d1 d = {7};
    struct d4 q = {1, 2, 3, 4};
    struct d3 x = {1, 2, 3};
    struct d2 p1 = {1, 2}, p2 = {3, 4};
    double r = c_args(a, b, c, d) + c_d4(q) + c_overflow(1, 2, 3, 4, 5, 6, x, 7)
        + c_varargs(2, p1, p2) + c_va_d3(2, x, (struct d3){4, 5, 6}) * 1000000;
    struct f3 rf = c_ret_f3(1.5f);
    struct d4 rd = c_ret_d4(2.5);
    struct dnest rn = c_ret_nest(3.5);
    struct f1 r1 = c_ret_f1(0.25f);
    struct f2 r2 = c_ret_f2(0.5f);
    struct d2 s2 = c_ret_d2(0.75);
    return r + rf.a + rf.b * 2 + rf.c * 3 + rd.a + rd.b * 2 + rd.c * 3 + rd.d * 4
        + rn.p.a + rn.p.b * 2 + rn.c * 3
        + r1.a * 10 + r2.a * 100 + r2.b * 1000 + s2.a * 10000 + s2.b * 100000;
}

/* Function pointers to clang-compiled HFA functions, for ccc to call. */
double (*h_d4_ptr)(struct d4) = h_d4;
struct f3 (*h_ret_f3_ptr)(float) = h_ret_f3;
