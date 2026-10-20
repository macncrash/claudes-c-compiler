#include <stdarg.h>

struct ld2 { long double a, b; };
struct ld4 { long double v[4]; };

long double ld_scale(long double x, int n) { return x * n; }
double ld_sum2(struct ld2 s) { return (double)(s.a + s.b); }
struct ld2 ld_make2(double a, double b) { struct ld2 r = { a, b }; return r; }
double ld_sum4(struct ld4 s) { return (double)(s.v[0] + s.v[1] + s.v[2] + s.v[3]); }

double ld_vsum(int n, ...) {
    va_list ap;
    va_start(ap, n);
    long double t = 0;
    for (int i = 0; i < n; i++)
        t += va_arg(ap, long double);
    va_end(ap);
    return (double)t;
}
