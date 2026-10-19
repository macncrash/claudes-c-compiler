/* Variadic calls in both directions across the ccc/clang boundary. */
#include <stdarg.h>
#include <stdio.h>

struct pt { int x, y; };
struct big { long a, b, c; };

long h_vsum(int n, ...);
double h_vdsum(int n, ...);
long h_vmixed(const char *fmt, ...);
long h_forward(long (*fn)(int, ...));

static long my_vsum(int n, ...) {
    va_list ap; va_start(ap, n);
    long s = 0;
    for (int i = 0; i < n; i++) s += va_arg(ap, int);
    va_end(ap);
    return s;
}

static long v_inner(int n, va_list ap) {
    long s = 0;
    for (int i = 0; i < n; i++) s += va_arg(ap, long) * (i + 1);
    return s;
}
static long v_outer(int n, ...) { va_list ap; va_start(ap, n); long r = v_inner(n, ap); va_end(ap); return r; }

static double structs(int n, ...) {
    va_list ap; va_start(ap, n);
    struct pt p = va_arg(ap, struct pt);
    double d = va_arg(ap, double);
    struct big b = va_arg(ap, struct big);
    float f = (float)va_arg(ap, double);
    va_end(ap);
    return p.x + p.y + d + b.a + b.b + b.c + f;
}

int main(void) {
    printf("%ld\n", h_vsum(4, 10L, 20L, 30L, 40L));
    printf("%ld\n", h_vsum(12, 1L, 2L, 3L, 4L, 5L, 6L, 7L, 8L, 9L, 10L, 11L, 12L));
    printf("%.3f\n", h_vdsum(3, 0.5, 0.25, 0.125));
    printf("%ld\n", h_vmixed("ildc", 7, 100000000000L, 2.5, 'z'));
    printf("%ld\n", h_forward((long (*)(int, ...))my_vsum));
    printf("%ld\n", v_outer(3, 5L, 6L, 7L));
    struct pt p = {1, 2}; struct big b = {10, 20, 30};
    printf("%.2f\n", structs(4, p, 3.5, b, 0.25f));
    char buf[128];
    snprintf(buf, sizeof buf, "[%5d|%-5s|%08.3f|%llx|%c]", 42, "ab", -3.14159, 0xdeadbeefcafeULL, 'q');
    puts(buf);
    return 0;
}
