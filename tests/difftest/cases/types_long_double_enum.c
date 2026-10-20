/* long double and enums keep their identity as types even where they share
 * a representation with another type (long double is IEEE double on Apple
 * arm64; an enum is its underlying integer type). Calls into the clang-built
 * helper check that long double values and structs still pass as doubles. */
#include <stdio.h>
#include <stdarg.h>

#define COMPAT(a, b) __builtin_types_compatible_p(a, b)
#define KIND(x) _Generic((x), float: "float", double: "double", \
                         long double: "long double", int: "int", \
                         unsigned: "unsigned", default: "other")

typedef enum { hot, dog, poo, bear } dingos;
typedef enum { janette, laura, amanda } cranberry;
enum neg { NEG_A = -1, NEG_B };
enum big { BIG_A = 1, BIG_B = 0x100000000LL };
enum tagged { T0, T1 };
enum tagged tagged_var;
typedef long double ld_t;

struct ld2 { long double a, b; };
struct ld4 { long double v[4]; };

long double ld_scale(long double x, int n);
double ld_sum2(struct ld2 s);
struct ld2 ld_make2(double a, double b);
double ld_sum4(struct ld4 s);
double ld_vsum(int n, ...);

static long double sum_va(int n, ...) {
    va_list ap;
    va_start(ap, n);
    long double t = 0;
    for (int i = 0; i < n; i++)
        t += va_arg(ap, long double);
    va_end(ap);
    return t;
}

int main(void) {
    long double ld = 2.5L;
    double d = 1.25;
    dingos dg = dog;

    printf("ld/d %d ld/ld_t %d ld*/d* %d ld/float %d\n",
           COMPAT(long double, double), COMPAT(long double, ld_t),
           COMPAT(long double *, double *), COMPAT(typeof(ld), typeof(1.0L)));
    printf("typeof %d %d %d\n", COMPAT(typeof(ld), double),
           COMPAT(typeof(ld + d), long double), COMPAT(typeof(d * 2), double));

    printf("enum/enum %d enum/self %d enum/uint %d enum/int %d\n",
           COMPAT(dingos, cranberry), COMPAT(dingos, dingos),
           COMPAT(dingos, unsigned int), COMPAT(dingos, int));
    printf("neg/int %d neg/uint %d big/ulong %d tag %d tag/var %d\n",
           COMPAT(enum neg, int), COMPAT(enum neg, unsigned),
           COMPAT(enum big, unsigned long), COMPAT(enum tagged, enum tagged),
           COMPAT(enum tagged, typeof(tagged_var)));
    printf("ptrs %d %d constant %d %d\n",
           COMPAT(enum neg *, enum tagged *), COMPAT(dingos *, dingos *),
           COMPAT(typeof(hot), int), COMPAT(typeof(hot), typeof(laura)));
    printf("dg %d\n", COMPAT(typeof(dg), dingos));

    printf("generic %s %s %s %s %s\n", KIND(ld), KIND(d), KIND(1.0L),
           KIND(ld * 2), KIND((float)d + ld));
    printf("sizes %zu %zu %zu %zu\n", sizeof(long double), _Alignof(long double),
           sizeof(1.0L), sizeof(struct ld2));

    struct ld2 s = { 1.5L, 2.25L };
    struct ld4 q = { { 1, 2, 3, 4.5L } };
    struct ld2 m = ld_make2(3.0, 4.0);
    printf("calls %.3Lf %.3f %.3f %.3f %.3Lf %.3Lf\n", ld_scale(ld, 3),
           ld_sum2(s), ld_sum4(q), ld_vsum(3, 1.0L, 2.0L, ld), m.a, m.b);
    printf("va %.3Lf\n", sum_va(3, 0.5L, ld, (long double)d));
    printf("conv %d %ld %.3Lf\n", (int)(ld * 4), (long)(ld * 1e6L),
           (long double)(unsigned long)ld);
    return 0;
}
