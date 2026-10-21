/* __builtin_constant_p of a plain local is decided after optimization, so a
 * local the optimizer proves constant gives 1 (when optimizing), anything
 * else gives 0, and a branch guarded by the answer is removed either way:
 * link_error() is never defined, so the program only links if every guarded
 * call is folded away. Parameters keep the early answer of 0. */
#include <stdio.h>
#include <stdlib.h>

extern void link_error(void);

#define BTEST(x) (__builtin_constant_p(x) ? "1" : "0")

static void expect(const char *got, int want_when_optimizing, int line) {
#ifdef __OPTIMIZE__
    if ((*got == '1') != want_when_optimizing) {
        printf("line %d: got %s\n", line, got);
        exit(1);
    }
#else
    (void)got; (void)want_when_optimizing; (void)line;
#endif
}

__attribute__((noinline)) int param_check(int p) {
    if (__builtin_constant_p(p) && p > 1000)
        link_error();
    return __builtin_constant_p(p);
}

int main(int argc, char **argv) {
    (void)argv;
    int size = sizeof(int);
    expect(BTEST(size), 1, __LINE__);
    expect(BTEST(size), 1, __LINE__);

    int k = 5;
    if (__builtin_constant_p(k) && k > 100)
        link_error();
    expect(BTEST(k), 1, __LINE__);

    int n = argc;                  /* not a constant */
    if (__builtin_constant_p(n) && n > 100)
        link_error();
    expect(BTEST(n), 0, __LINE__);

    int j = 0;                     /* changed in a loop */
    for (int i = 0; i < argc + 3; i++)
        j += i;
    expect(BTEST(j), 0, __LINE__);

    int a = 7;                     /* address taken: stays in memory */
    int *pa = &a;
    *pa += argc;
    if (__builtin_constant_p(a) && a > 100)
        link_error();

    volatile int v = 3;
    expect(BTEST(v), 0, __LINE__);

    int cp = __builtin_constant_p(k) ? k * 2 : -1;
    printf("size %d k %d n %d j %d a %d cp %s\n", size, k, n, j, a,
           cp == 10 || cp == -1 ? "ok" : "bad");
    printf("param %d\n", param_check(argc));
    return 0;
}
