/* Address constants in static initializers built from chains of members,
 * subscripts, decays and pointer arithmetic: each was emitted as a null
 * pointer when no special case matched its shape. Offsets from the base
 * object are printed. */
#include <stdio.h>

struct In { char c; short s[3]; int i; };
struct Out { long l; struct In in[2]; char tail[5]; int m[2][3]; };

static struct Out so = { 7, { { 'x', { 1, 2, 3 }, 4 }, { 'y', { 5, 6, 7 }, 8 } }, "wxyz", { { 1, 2, 3 }, { 4, 5, 6 } } };
static struct Out sarr[3];
static int ia[4][5];

static const void *const ptrs[] = {
    so.in[1].s,                 /* array member of an array element */
    &so.in[1].s[1],
    so.m[1],                    /* row of a 2-D member */
    &sarr[2].in[1].i,           /* member of an element of a member of an element */
    &sarr[1].m[1][2],
    &so.in[0].s[0] + 2,
    (char *)so.m + 7,
    &(sarr + 2)->tail[3],
    &(&sarr[1])->m[0][2],
    &((struct In *)&so.in[0])[1].s[2],
    *(ia + 3),
    &ia[2][1] - 3,
};
static const void *const bases[] = {
    &so, &so, &so, sarr, sarr, &so, &so, sarr, sarr, &so, ia, ia,
};

static const short *single = &so.in[1].s[2];

int main(void)
{
    static struct In loc[2][2];
    static short *lp = &loc[1][0].s[2];

    for (unsigned i = 0; i < sizeof ptrs / sizeof ptrs[0]; i++)
        printf("%u %ld\n", i, (long)((const char *)ptrs[i] - (const char *)bases[i]));
    printf("single %ld %d\n", (long)((const char *)single - (const char *)&so), *single);
    printf("local %ld\n", (long)((char *)lp - (char *)loc));
    printf("values %d %d %d\n", *(const short *)ptrs[1], *(const int *)ptrs[2], *(const short *)ptrs[5]);
    return 0;
}
