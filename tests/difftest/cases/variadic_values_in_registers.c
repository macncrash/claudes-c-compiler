#include <stdarg.h>
#include <stdio.h>

struct pair { long a, b; };
struct big { long v[5]; };

/* Sum mixed args; the format letters pick the types. */
static double __attribute__((noinline)) mix(int n, const char *fmt, ...) {
    va_list ap, ap2;
    double total = 0;
    long scale = n * 3 + 1;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    for (const char *p = fmt; *p; p++) {
        switch (*p) {
        case 'i': total += va_arg(ap, int) * scale; break;
        case 'l': total += (double)va_arg(ap, long); break;
        case 'd': total += va_arg(ap, double) * 0.5; break;
        case 'p': { struct pair q = va_arg(ap, struct pair); total += q.a - q.b; break; }
        case 'b': { struct big q = va_arg(ap, struct big); total += q.v[0] + q.v[4]; break; }
        }
        scale++;
    }
    /* The copy still sees the first argument. */
    if (*fmt == 'i') total += va_arg(ap2, int);
    va_end(ap2);
    va_end(ap);
    return total + n;
}

static int __attribute__((noinline)) fmt_into(char *buf, int len, const char *fmt, ...) {
    va_list ap;
    if (len <= 0) return -1;
    va_start(ap, fmt);
    int r = vsnprintf(buf, len, fmt, ap);
    va_end(ap);
    buf[len - 1] = 0;
    return r;
}

static long __attribute__((noinline)) count_until_zero(long first, ...) {
    va_list ap;
    long n = 0, sum = 0;
    va_start(ap, first);
    for (long v = first; v; v = va_arg(ap, long)) {
        n++;
        sum += v * n;
    }
    va_end(ap);
    return sum;
}

int main(void) {
    struct pair pr = { 40, 2 };
    struct big bg = { { 1, 2, 3, 4, 5 } };
    printf("%.2f\n", mix(2, "ildpb", 7, 100L, 3.0, pr, bg));
    printf("%.2f\n", mix(0, "dd", 1.5, -2.5));
    char buf[32];
    int r = fmt_into(buf, sizeof buf, "%d-%s-%.1f", 42, "x", 2.5);
    printf("%d %s\n", r, buf);
    printf("%d\n", fmt_into(buf, 0, "%d", 1));
    printf("%ld\n", count_until_zero(5, 6L, 7L, 8L, 0L));
    return 0;
}
