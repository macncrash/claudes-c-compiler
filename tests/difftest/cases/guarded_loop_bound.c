/* A loop bound read through a pointer the code checks first:
 *
 *   if (p) for (i = 0; i < p->n; i++) ...
 *
 * The test branches straight to the loop header (the counter's start is a
 * phi constant), so the test's block is the loop's only way in. Moving the
 * load of p->n there would read through p when it is null. From SQLite's
 * heightOfExprList, inlined into a walk over a chain of selects whose
 * lists are often absent. */
#include <stdio.h>
#include <stdlib.h>

typedef struct { int n; int v[8]; } List;
typedef struct Sel { List *a, *b; struct Sel *prior; } Sel;

static void max_of(const List *p, int *m) {
    if (p) {
        for (int i = 0; i < p->n; i++)
            if (p->v[i] > *m)
                *m = p->v[i];
    }
}

__attribute__((noinline)) static int height(const Sel *s) {
    int m = 0;
    for (; s; s = s->prior) {
        max_of(s->a, &m);
        max_of(s->b, &m);
    }
    return m;
}

/* The same with the count read through two pointers, and a while loop. */
__attribute__((noinline)) static long sum_counts(List *const *lists, int k) {
    long s = 0;
    for (int j = 0; j < k; j++) {
        const List *p = lists[j];
        int i = 0;
        if (p)
            while (i < p->n)
                s += p->v[i++];
    }
    return s;
}

int main(void) {
    List x = { 3, { 4, 9, 2 } }, y = { 0, { 0 } }, z = { 5, { 1, 2, 30, 4, 5 } };
    Sel s3 = { 0, &z, 0 }, s2 = { &y, 0, &s3 }, s1 = { 0, 0, &s2 }, s0 = { &x, 0, &s1 };
    printf("%d %d %d\n", height(&s0), height(&s1), height(&s3));
    Sel empty = { 0, 0, 0 };
    printf("%d\n", height(&empty));
    List *lists[] = { &x, 0, &z, 0, &y };
    printf("%ld %ld\n", sum_counts(lists, 5), sum_counts(lists + 1, 1));
    return 0;
}
