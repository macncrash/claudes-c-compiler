/* const carried by globals and typedefs:
 *  - __typeof__ of a const global (or of *p, a[i] on one) declares a const
 *    object, and a local shadowing it does not;
 *  - a typedef of a const type makes declarations using it const, through
 *    typedefs of typedefs, pointers to it and parameters, and a local
 *    typedef shadows it only within its block;
 *  - const on a pointer typedef (const IP p) makes the pointer const, not
 *    what it points to. */
#include <stdio.h>

#define IS_CONST(p) _Generic((p), const int *: 1, int *: 0, \
    const char *: 1, char *: 0, default: 9)

typedef const int CI;
typedef CI CI2;
typedef const char *CCP;
typedef int *IP;
typedef const int CIA[3];
typedef int IA[3];

const int g = 1;
int h = 2;
const int *gp = &g;
int *const gcp = &h;
extern const int ge;
const int ge = 5;
const int garr[2] = {1, 2};
CI gci = 3;
static const int sg = 4;

static int f_param(CI *p, const IP q, CCP s) {
    return IS_CONST(p) * 100 + IS_CONST(q) * 10 + IS_CONST(s);
}

int main(void) {
    /* typeof of globals */
    __typeof__(g) a = 0;
    __typeof__(h) b = 0;
    __typeof__(gp) c = 0;
    __typeof__(gcp) d = 0;
    __typeof__(ge) e = 0;
    __typeof__(garr[0]) f = 0;
    __typeof__(*gp) i = 0;
    __typeof__(gci) j = 0;
    __typeof__(sg) k = 0;
    printf("globals %d %d %d %d %d %d %d %d %d\n",
           IS_CONST(&a), IS_CONST(&b), IS_CONST(c), IS_CONST(d),
           IS_CONST(&e), IS_CONST(&f), IS_CONST(&i), IS_CONST(&j), IS_CONST(&k));

    /* typedefs */
    CI t1 = 0;
    CI2 t2 = 0;
    CI *t3 = 0;
    CCP t4 = 0;
    const IP t5 = 0;
    IP t6 = 0;
    CIA t7 = {1, 2, 3};
    const IA t8 = {1, 2, 3};
    IA t9 = {1, 2, 3};
    printf("typedefs %d %d %d %d %d %d %d %d %d\n",
           IS_CONST(&t1), IS_CONST(&t2), IS_CONST(t3), IS_CONST(t4),
           IS_CONST(t5), IS_CONST(t6), IS_CONST(&t7[0]), IS_CONST(&t8[0]),
           IS_CONST(&t9[0]));

    /* typeof of typedef'd locals, and typedefs of typeof */
    __typeof__(t1) u1 = 0;
    __typeof__(t4) u2 = 0;
    __typeof__(t5) u3 = 0;
    typedef __typeof__(g) TG;
    TG u4 = 0;
    typedef __typeof__(h) TH;
    TH u5 = 0;
    printf("mixed %d %d %d %d %d\n",
           IS_CONST(&u1), IS_CONST(u2), IS_CONST(u3), IS_CONST(&u4), IS_CONST(&u5));

    /* a local typedef shadows a const one; the const one returns after */
    {
        typedef int CI;
        CI v = 0;
        printf("shadow %d\n", IS_CONST(&v));
    }
    CI w = 0;
    printf("restored %d\n", IS_CONST(&w));

    /* a local shadows a const global */
    int g = 0;
    printf("local %d\n", IS_CONST(&g));

    printf("params %d\n", f_param(0, 0, 0));
    (void)a; (void)b; (void)e; (void)f; (void)i; (void)j; (void)k;
    (void)t1; (void)t2; (void)u1; (void)u4; (void)u5; (void)w; (void)g;
    return 0;
}
