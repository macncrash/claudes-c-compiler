/* A function pointer's own const (int (*const fp)(int)), seen through
 * _Generic and __typeof__: in locals, globals, arrays, struct fields,
 * parameters (also a pointer to one, int (*const *ppf)(int)), typedefs
 * of one, const written on a function pointer typedef or typeof, and
 * type names. Calls through each kind still work. */
#include <stdio.h>

typedef int fn_t(int);
/* 0: pointer to function pointer, 1: pointer to const function pointer */
#define FPP(x) _Generic((x), int (**)(int): 0, int (*const *)(int): 1, default: 9)
/* the controlling function pointer's own const is dropped */
#define FP(x) _Generic((x), int (*)(int): 0, default: 9)

static int twice(int v) { return 2 * v; }

typedef int (*FP_t)(int);
typedef int (*const CFP_t)(int);

struct ops { int (*f)(int); int (*const cf)(int); };

int (*gf)(int) = twice;
int (*const gcf)(int) = twice;
int (*const gcfa[2])(int) = {twice, twice};

static int params(int (*const pf)(int), int (*const *ppf)(int), const FP_t tf,
                  int (*plain)(int)) {
    printf("param calls %d %d %d %d %d\n", pf(1), (*ppf)(2), (**ppf)(3), tf(4), plain(5));
    return FPP(&pf) * 1000 + FPP(ppf) * 100 + FPP(&tf) * 10 + FPP(&plain);
}

int main(void) {
    int (*f)(int) = twice;
    int (*const cf)(int) = twice;
    int (*const *pcf)(int) = &cf;
    int (**const cpf)(int) = &f;
    fn_t *const tcf = twice;
    printf("locals %d %d %d %d %d\n", FPP(&f), FPP(&cf), FPP(pcf), FPP(cpf), FPP(&tcf));
    printf("top %d %d %d\n", FP(cf), FP(*pcf), FP(gcf));

    const FP_t t1 = twice;
    CFP_t t2 = twice;
    FP_t t3 = twice;
    printf("typedefs %d %d %d\n", FPP(&t1), FPP(&t2), FPP(&t3));

    printf("globals %d %d %d\n", FPP(&gf), FPP(&gcf), FPP(&gcfa[0]));

    __typeof__(cf) u1 = twice;
    __typeof__(f) u2 = twice;
    __typeof__(int (*const)(int)) u3 = twice;
    const __typeof__(f) u4 = twice;
    printf("typeof %d %d %d %d\n", FPP(&u1), FPP(&u2), FPP(&u3), FPP(&u4));

    struct ops o = {twice, twice};
    const struct ops co = {twice, twice};
    printf("members %d %d %d %d\n", FPP(&o.f), FPP(&o.cf), FPP(&co.f), FPP(&co.cf));

    int (*const arr[2])(int) = {twice, twice};
    printf("arrays %d %d\n", FPP(arr), FPP(&arr[1]));

    printf("params %d\n", params(twice, &cf, twice, twice));
    printf("calls %d %d %d %d\n", cf(1), (*pcf)(2), gcf(3), co.cf(4));
    printf("typeof calls %d %d %d %d %d %d\n", u1(5), u2(6), u3(7), u4(8),
           t1(9), t2(10));
    int (*const lf)(int) = u3;
    int (*const *plf)(int) = &lf;
    printf("more calls %d %d %d\n", lf(11), (*plf)(12), (**plf)(13));
    return 0;
}
