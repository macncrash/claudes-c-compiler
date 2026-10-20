#include <stdarg.h>

struct small { int a, b; };

long h_mixed(long a, long b, long c, long d, long e, long f, long g, long h,
             char i, long j, short k, struct small s, char l, int m) {
    return a + b + c + d + e + f + g + h + i * 10 + j * 100 + k * 1000
        + s.a * 10000 + s.b * 100000 + l * 1000000 + m * 10000000L;
}

double h_floats(double a, double b, double c, double d, double e, double f, double g, double h,
                float i, float j, double k, float l) {
    return a + b + c + d + e + f + g + h + i * 10 + j * 100 + k * 1000 + l * 10000;
}

long h_varargs(long a, long b, long c, long d, long e, long f, long g, long h,
               char i, short j, int n, ...) {
    va_list ap;
    va_start(ap, n);
    long sum = a + b + c + d + e + f + g + h + i * 10 + j * 100;
    for (int k = 0; k < n; k++)
        sum += va_arg(ap, int) * 1000L * (k + 1);
    va_end(ap);
    return sum;
}

long c_mixed(long a, long b, long c, long d, long e, long f, long g, long h,
             char i, long j, short k, struct small s, char l, int m);
double c_floats(double a, double b, double c, double d, double e, double f, double g, double h,
                float i, float j, double k, float l);
long c_varargs(long a, long b, long c, long d, long e, long f, long g, long h,
               char i, short j, int n, ...);

/* clang caller, ccc callee */
long h_call_ccc(void) {
    struct small s = {3, 4};
    long r = c_mixed(1, 2, 3, 4, 5, 6, 7, 8, 9, 2, 5, s, 6, 7);
    r += (long)c_floats(1, 2, 3, 4, 5, 6, 7, 8, 1.5f, 2.25f, 3.5, 4.75f);
    r += c_varargs(1, 2, 3, 4, 5, 6, 7, 8, 9, 3, 2, 5, 6) * 7;
    return r;
}
