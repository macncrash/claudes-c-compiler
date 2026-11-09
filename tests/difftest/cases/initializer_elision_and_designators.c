/* Brace elision and designators in aggregate initializers, each applied to
 * a static object and to a local one: values without braces fill a 2-D
 * member leaf by leaf; a braced element after elided ones is the next
 * element whole; a designator ends an elided run and may jump back; a
 * designated row or struct element continues by elision. */
#include <stdio.h>

struct P { char c; int a[3]; short s; };
struct R { struct P p[2]; int z; };
struct G { double d; short g[2][3]; int k; };
struct PT { short x, y; };
struct Q { long l; struct PT pt; double d; int z; };

static void pr(const struct R *r)
{
    for (int i = 0; i < 2; i++)
        printf("%d %d %d %d %d | ", r->p[i].c, r->p[i].a[0], r->p[i].a[1], r->p[i].a[2], r->p[i].s);
    printf("%d\n", r->z);
}

static void pg(const struct G *g)
{
    printf("%g | %d %d %d | %d %d %d | %d\n", g->d, g->g[0][0], g->g[0][1], g->g[0][2],
           g->g[1][0], g->g[1][1], g->g[1][2], g->k);
}

static void pq(const struct Q *q)
{
    printf("%ld %d %d %g %d\n", q->l, q->pt.x, q->pt.y, q->d, q->z);
}

#define BOTH(T, P, NAME, ...) do {                                        \
        static T s_##NAME = __VA_ARGS__;                                  \
        T l_##NAME = __VA_ARGS__;                                         \
        printf("%-10s ", #NAME); P(&s_##NAME);                            \
        printf("%-10s ", ""); P(&l_##NAME);                               \
    } while (0)

int main(void)
{
    /* A 2-D member initialized without inner braces. */
    BOTH(struct G, pg, flat2d, { 3, 1, 2, 3, 4, 5, 6, 7 });
    BOTH(struct G, pg, braced2d, { .d = 3, { 1, 2, 3, 4 }, 9 });
    BOTH(struct G, pg, rowdesig, { .g = { [1] = { [1] = -72, 255 } } });
    BOTH(struct G, pg, rowinner, { .g[1] = { -83, [2] = -27 } });
    BOTH(struct G, pg, rowcont, { .g[0] = { 1, 2 }, 3, 4 });
    BOTH(struct G, pg, leafcont, { .g[1][1] = 5, 6, 7 });

    /* Arrays of structs: elided elements, braced elements, designators. */
    BOTH(struct R, pr, mixed, { 1, 2, 3, 4, 5, { 6, 7, 8, 9, 10 }, 11 });
    BOTH(struct R, pr, elidedz, { 1, 2, 3, 4, 5, .z = 11 });
    BOTH(struct R, pr, jumpback, { { [1] = 6, 7, [0].s = 5 }, 11 });
    BOTH(struct R, pr, elemcont, { .p[1] = 6, 7, 8, .z = 11 });
    BOTH(struct R, pr, inlist, { { [1] = { .s = -128 } }, 4 });
    BOTH(struct R, pr, nested, { { { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 } }, 11 });

    /* A designator after an elided struct belongs to the outer list. */
    BOTH(struct Q, pq, afterelide, { .l = 300, -2, .d = -2.25 });
    BOTH(struct Q, pq, desigelide, { .pt = 1, 2, 3, 4 });

    /* Arrays of these as whole objects. */
    static struct R sa[2] = { { { 1 } }, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, { 13 } };
    struct R la[2] = { [1] = { { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 }, 11 } };
    pr(&sa[0]); pr(&sa[1]); pr(&la[0]); pr(&la[1]);
    return 0;
}
