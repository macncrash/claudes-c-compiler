#include <stdarg.h>
long h_vsum(int n, ...) { va_list ap; va_start(ap, n); long s = 0; for (int i = 0; i < n; i++) s += va_arg(ap, long); va_end(ap); return s; }
double h_vdsum(int n, ...) { va_list ap; va_start(ap, n); double s = 0; for (int i = 0; i < n; i++) s += va_arg(ap, double); va_end(ap); return s; }
long h_vmixed(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); long s = 0;
    for (; *fmt; fmt++) {
        switch (*fmt) {
        case 'i': s += va_arg(ap, int); break;
        case 'l': s += va_arg(ap, long) / 1000; break;
        case 'd': s += (long)(va_arg(ap, double) * 100); break;
        case 'c': s += va_arg(ap, int); break;
        }
    }
    va_end(ap); return s;
}
long h_forward(long (*fn)(int, ...)) { return fn(5, 1, 2, 3, 4, 5) + fn(9, 1, 1, 1, 1, 1, 1, 1, 1, 100); }
