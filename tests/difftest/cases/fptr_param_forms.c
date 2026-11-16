/* Function pointer parameters in every declarator form. ccc dropped the
 * `...` of a function pointer parameter (int (*f)(int, ...)), so a call
 * through it passed the variadic arguments in registers where the Apple
 * arm64 ABI puts them on the stack; it dropped the [N] of an array of
 * function pointers (int (*f[2])(int)), indexing it with the stride of
 * int; and it did not parse a pointer to a function returning a function
 * pointer (int (*(*g)(void))(int)) as a parameter, named, abstract or K&R.
 * sema now gives these parameters their function pointer types, so a call
 * through an object that is not a function is rejected. */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

typedef int fn_t(int);
typedef int (*fp_t)(int);
typedef void *(*alloc_t)(void *, size_t);

static int inc(int x) { return x + 1; }
static int dbl(int x) { return x * 2; }
static int isum(int n, ...) {
    va_list ap; va_start(ap, n); int s = 0;
    for (int i = 0; i < n; i++) s += va_arg(ap, int);
    va_end(ap); return s;
}
static double dsum(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); double s = 0;
    for (const char *p = fmt; *p; p++) s += *p == 'd' ? va_arg(ap, double) : va_arg(ap, int);
    va_end(ap); return s;
}
static int *ip(int v) { static int s; s = v; return &s; }
static fp_t getinc(void) { return inc; }
static fp_t pinc_g = inc;
static fp_t *getpp(void) { return &pinc_g; }
static void *myre(void *p, size_t n) { return realloc(p, n); }

int a1(int (*f)(int)) { return f(1); }
int a2(int (* const f)(int)) { return f(2); }
int a3(int (**f)(int)) { return (*f)(3); }
int a4(fn_t f) { return f(4); }
int a5(fp_t f) { return f(5); }
int a6(fp_t *f) { return (*f)(6); }
int a7(int f(int)) { return f(7); }
int a8(int (*f)(int n, ...)) { return isum(1, 100) + f(3, 4, 5, 6); }
void *a9(alloc_t a) { return a(NULL, 8); }
int *a10(int *(*f)(int)) { return f(10); }
int a11(int (*f[2])(int)) { return f[0](11) + f[1](11); }
int a12(int (*f[])(int), int i) { int (*g)(int) = f[i]; return g(5); }
int a13(__typeof__(inc) *f) { return f(13); }
int a14(f, x) int (*f)(int); int x; { return f(x); }
int proto(int (*(*)(void))(int));
int proto(int (*(*g)(void))(int)) { return g()(20); }
int kr(g, h) int (*(*g)(void))(int); double (*h)(const char *, ...); { return g()(30) + (int)h("did", 1.5, 2, 3.5); }
int pp(int (**(*g)(void))(int)) { return (*g())(40); }
double vd(double (*h)(const char *, ...), int n) { return h("dddi", 0.25, 0.5, 1.0, n); }
double varr(double (*h[1])(const char *, ...)) { return h[0]("dd", 2.0, 3.0); }
struct S { double (*h)(const char *, ...); };
double vs(struct S *s) { return s->h("ddd", 1.0, 1.0, 1.0); }

int main(void) {
    fp_t arr[2] = { inc, dbl };
    fp_t pinc = inc;
    int (*local)(int) = inc;
    fn_t *lp = dbl;
    double (*ha[1])(const char *, ...) = { dsum };
    struct S s = { dsum };
    void *r = a9(myre);
    printf("%d %d %d %d %d %d %d\n", a1(inc), a2(inc), a3(&local), a4(inc), a5(inc), a6(&pinc), a7(inc));
    printf("%d %d %d %d %d %d\n", a8(isum), *a10(ip), a11(arr), a12(arr, 1), a13(inc), a14(inc, 14));
    printf("%d %d %d %d %d\n", proto(getinc), kr(getinc, dsum), pp(getpp), local(0), lp(1));
    printf("%g %g %g %d\n", vd(dsum, 7), varr(ha), vs(&s), r != NULL);
    free(r);
    return 0;
}
