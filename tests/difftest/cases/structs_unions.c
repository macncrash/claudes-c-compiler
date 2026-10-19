/* Struct copy/assign/return, nested aggregates, flexible array members,
 * unions and type punning, compound literals, designated initializers. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct inner { short s; double d; };
struct outer { char tag; struct inner in[2]; int arr[3]; };
struct flex { int n; int data[]; };
union pun { float f; unsigned u; unsigned char b[4]; };

static struct outer make(int k) {
    struct outer o = {.tag = 'x', .in = {{1, 1.5}, {2, 2.5}}, .arr = {k, k + 1, k + 2}};
    return o;
}
static struct inner swap(struct inner a) { struct inner r = {(short)a.d, a.s}; return r; }

int main(void) {
    struct outer a = make(10), b;
    b = a; b.arr[1] = 99;
    printf("%c %d %.1f %d %d %d\n", a.tag, a.in[1].s, a.in[0].d, a.arr[0], a.arr[1], b.arr[1]);
    struct inner s = swap((struct inner){7, 3.0});
    printf("%d %.1f\n", s.s, s.d);
    struct flex *fl = malloc(sizeof *fl + 5 * sizeof(int));
    fl->n = 5; for (int i = 0; i < 5; i++) fl->data[i] = i * i;
    printf("%d %d %zu\n", fl->n, fl->data[4], sizeof(struct flex));
    free(fl);
    union pun p; p.f = 1.0f;
    printf("%08x %d\n", p.u, p.b[3]);
    int *cl = (int[]){4, 5, 6};
    printf("%d\n", cl[0] + cl[2]);
    struct outer arr[3] = {[2].arr = {1, 2, 3}, [0].tag = 'q'};
    printf("%c %d %d\n", arr[0].tag, arr[2].arr[2], arr[1].arr[0]);
    char buf[sizeof(struct outer)];
    memcpy(buf, &a, sizeof a);
    printf("%d\n", ((struct outer *)buf)->arr[2]);
    return 0;
}
