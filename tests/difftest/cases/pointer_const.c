/* A pointer's own const (int *const p), seen through _Generic and
 * __typeof__:
 *  - at each pointer level of locals, globals, parameters, arrays and
 *    struct fields, and through typedefs (typedef int *const CIP);
 *  - const written on a pointer typedef or typeof (const IP p) makes the
 *    pointer itself const;
 *  - a pointer member of a const struct is itself const;
 *  - the controlling expression's own top-level const is dropped. */
#include <stdio.h>

/* 0: int **, 1: int *const *, 2: const int **, 3: const int *const * */
#define PP(x) _Generic((x), int **: 0, int *const *: 1, const int **: 2, \
    const int *const *: 3, default: 9)
/* pointer-to-pointer-to-pointer, const at the middle level */
#define PPP(x) _Generic((x), int ***: 0, int **const *: 1, int *const **: 2, \
    default: 9)
/* the controlling pointer's own const is dropped */
#define P(x) _Generic((x), int *: 0, const int *: 1, default: 9)

typedef int *IP;
typedef int *const CIP;
typedef const int *PCI;

struct s { int *p; int *const cp; const int *pc; int *a[2]; };

int *gp;
int *const gcp = 0;
const int *const gccp = 0;
CIP gtcp = 0;

static int params(int *const q, int *const *qq, const IP r, CIP t) {
    return PP(&q) * 1000 + PP(qq) * 100 + PP(&r) * 10 + PP(&t);
}

int main(void) {
    int x = 0;
    int *p = &x;
    int *const cp = &x;
    const int *pc = &x;
    const int *const cpc = &x;
    int *const *pcp = &cp;
    int **const cpp = &p;
    int *const *const cpcp = &cp;
    printf("locals %d %d %d %d %d %d %d\n", PP(&p), PP(&cp), PP(&pc),
           PP(&cpc), PP(pcp), PP(cpp), PP(cpcp));
    printf("top %d %d %d %d\n", P(cp), P(cpc), P(*pcp), P(*cpcp));

    int **pp = &p;
    int **const *ppcp = &cpp;
    int *const **pcpp = 0;
    printf("three %d %d %d %d\n", PPP(&pp), PPP(ppcp), PPP(&pcp), PPP(pcpp));

    /* typedefs and written const on them */
    const IP ip = &x;
    CIP tcp = &x;
    PCI tpc = &x;
    const PCI ctpc = &x;
    printf("typedefs %d %d %d %d\n", PP(&ip), PP(&tcp), PP(&tpc), PP(&ctpc));

    /* globals */
    printf("globals %d %d %d %d\n", PP(&gp), PP(&gcp), PP(&gccp), PP(&gtcp));

    /* __typeof__ keeps the pointer's own const */
    __typeof__(cp) t1 = &x;
    __typeof__(p) t2 = &x;
    __typeof__(*pcp) t3 = &x;
    __typeof__(ip) t4 = &x;
    __typeof__(int *const) t5 = &x;
    const __typeof__(p) t6 = &x;
    printf("typeof %d %d %d %d %d %d\n", PP(&t1), PP(&t2), PP(&t3), PP(&t4),
           PP(&t5), PP(&t6));

    /* members */
    struct s s = {0, 0, 0, {0, 0}};
    const struct s cs = {0, 0, 0, {0, 0}};
    const struct s *csp = &cs;
    printf("members %d %d %d %d %d %d %d\n", PP(&s.p), PP(&s.cp), PP(&s.pc),
           PP(&cs.p), PP(&cs.pc), PP(&csp->p), PP(&cs.a[0]));
    __typeof__(cs.p) t7 = &x;
    __typeof__(s.cp) t8 = &x;
    printf("membertypeof %d %d\n", PP(&t7), PP(&t8));

    /* arrays of const pointers */
    int *const arr[2] = {&x, &x};
    printf("arrays %d %d\n", PP(arr), PP(&arr[1]));

    printf("params %d\n", params(&x, &cp, &x, &x));
    (void)t1; (void)t2; (void)t3; (void)t4; (void)t5; (void)t6; (void)t7; (void)t8;
    return 0;
}
