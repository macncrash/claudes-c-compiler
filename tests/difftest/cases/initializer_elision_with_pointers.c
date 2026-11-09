/* Static initializers of structs holding pointers, which take a separate
 * path from those without: values without inner braces fill struct
 * elements member by member (an int a[3] takes three), a braced element
 * after elided ones is the next element whole, [i].f designators continue
 * through element i, a 2-D member fills leaf by leaf, and { "yz" } fills a
 * char array member. Each element's members are printed. */
#include <stdio.h>

struct P { char c; int a[3]; const char *q; short s; };
union U { int i; unsigned char b[4]; };
struct Q { struct P p[2]; union U u; char name[7]; double d; short g[2][3]; };

static void pp(const struct P *x)
{
    printf("%d %d %d %d %s %d | ", x->c, x->a[0], x->a[1], x->a[2], x->q ? x->q : "-", x->s);
}

static void pq(const struct Q *x)
{
    pp(&x->p[0]); pp(&x->p[1]);
    printf("%d %s %g | %d %d %d %d %d %d\n", x->u.i, x->name, x->d,
           x->g[0][0], x->g[0][1], x->g[0][2], x->g[1][0], x->g[1][1], x->g[1][2]);
}

static struct P a[3] = { 50, 65537, 1, 100, "x", 7, { 25, .s = 95 } };
static struct P b[3] = { { 1 }, 2, 3, 4, 5, "y", 6, { 7, { 8 }, "z", 9 } };
static struct P c[3] = { 1, 2, 3, 4, "w", 5, [2] = { 7 }, [0].s = 8 };
static struct P d[3] = { [1] = 2, 3, 4, 5, "v", 6, { 7 } };
static struct P e[2] = { [1].q = "e", 9, [0].c = 3 };

static struct Q q1 = { { -1, 65537, -128, 65537, "x", -1 }, { .i = 255 }, { "yz" } };
static struct Q q2 = { 39, -1, 255, 1, "s1", -25, 71, -128, 30, -1, "lit" + 1, 38 };
static struct Q q3 = { { { -1 }, 2, 3, 4, 5, "y" }, 7, .d = 0.5, 1, 2, 3, 4 };
static struct Q q4[2] = { [1] = { .u = { .b = { [2] = 255, [3] = 188 } }, { "zbyx" }, .d = -2.25,
                                  { { -58, 1 }, -42, -72, -63 } } };
static struct Q q5 = { .g[1] = { 7, [2] = 6 }, .name = "ab", .p[1] = 4, 5, 6, 7, "p", 8 };

int main(void)
{
    for (int i = 0; i < 3; i++) pp(&a[i]);
    printf("\n");
    for (int i = 0; i < 3; i++) pp(&b[i]);
    printf("\n");
    for (int i = 0; i < 3; i++) pp(&c[i]);
    printf("\n");
    for (int i = 0; i < 3; i++) pp(&d[i]);
    printf("\n");
    for (int i = 0; i < 2; i++) pp(&e[i]);
    printf("\n");
    pq(&q1); pq(&q2); pq(&q3); pq(&q4[0]); pq(&q4[1]); pq(&q5);
    return 0;
}
