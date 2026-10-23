/* const of struct members, seen through _Generic and __typeof__:
 *  - a field declared const (also after the type, or through a const
 *    typedef) is const, and a field of a const struct is const unless it
 *    is a pointer, whose own const that is;
 *  - as in clang, const on an anonymous member does not reach its fields;
 *  - const before the struct keyword belongs to the declaration, and does
 *    not leak into the fields inside or out of them. */
#include <stdio.h>

#define IS_CONST(p) _Generic((p), const int *: 1, int *: 0, \
    const char *: 1, char *: 0, int **: 2, int *const *: 3, \
    const int **: 4, default: 9)

typedef const int CI;
typedef int *IP;

struct in { int v; const int cv; };
struct s {
    int x;
    const int cx;
    int const cx2;
    int *p;
    const int *cp;
    int *const pc;
    int a[2];
    const int ca[2];
    int *pa[2];
    CI tx;
    CI *tp;
    const IP tip;
    struct in in;
    const struct in cin;
    struct { int ax; const int acx; };
    const struct { int bx; int *bp; };
    char c;
    const char cc;
};

struct s gs;
const struct s gcs;

static void members(struct s *ms, const struct s *mcs) {
    /* fields of a non-const struct */
    printf("plain %d %d %d %d %d %d %d %d %d %d %d %d %d\n",
           IS_CONST(&ms->x), IS_CONST(&ms->cx), IS_CONST(&ms->cx2),
           IS_CONST(ms->p), IS_CONST(ms->cp), IS_CONST(ms->pc),
           IS_CONST(&ms->a[0]), IS_CONST(&ms->ca[0]), IS_CONST(ms->pa[0]),
           IS_CONST(&ms->tx), IS_CONST(ms->tp), IS_CONST(ms->tip),
           IS_CONST(&ms->c));
    printf("nested %d %d %d %d %d %d %d %d\n",
           IS_CONST(&ms->in.v), IS_CONST(&ms->in.cv), IS_CONST(&ms->cin.v),
           IS_CONST(&ms->ax), IS_CONST(&ms->acx), IS_CONST(&ms->bx),
           IS_CONST(ms->bp), IS_CONST(&ms->cc));
    /* fields through a pointer to const */
    printf("ptrconst %d %d %d %d %d %d %d %d %d\n",
           IS_CONST(&mcs->x), IS_CONST(mcs->p), IS_CONST(mcs->cp),
           IS_CONST(&mcs->a[0]), IS_CONST(mcs->pa[0]), IS_CONST(&mcs->in.v),
           IS_CONST(&mcs->ax), IS_CONST(&mcs->bx), IS_CONST(&(*mcs).x));
    /* pointer-to-pointer views */
    printf("pp %d %d\n", IS_CONST(&ms->p), IS_CONST(&ms->cp));
}

int main(void) {
    struct s ls = {0};
    const struct s lcs = {0};
    members(&ls, &lcs);

    /* typeof of members */
    __typeof__(ls.x) t1 = 0;
    __typeof__(ls.cx) t2 = 0;
    __typeof__(lcs.x) t3 = 0;
    __typeof__(lcs.p) t4 = 0;
    __typeof__(lcs.cp) t5 = 0;
    __typeof__(gcs.in.v) t6 = 0;
    __typeof__(gs.in.v) t7 = 0;
    __typeof__(gcs.a[1]) t8 = 0;
    __typeof__((&lcs)->x) t9 = 0;
    printf("typeof %d %d %d %d %d %d %d %d %d\n",
           IS_CONST(&t1), IS_CONST(&t2), IS_CONST(&t3), IS_CONST(t4),
           IS_CONST(t5), IS_CONST(&t6), IS_CONST(&t7), IS_CONST(&t8),
           IS_CONST(&t9));

    /* const before the struct keyword applies to the declaration, not to
       the fields inside */
    const struct { int y; } anon = {1};
    struct { int z; } anon2 = {2};
    const int after = 0;
    int plain = 0;
    printf("decl %d %d %d %d\n", IS_CONST(&anon.y), IS_CONST(&anon2.z),
           IS_CONST(&after), IS_CONST(&plain));
    (void)t1; (void)t2; (void)t3; (void)t6; (void)t7; (void)t8; (void)t9;
    return 0;
}
