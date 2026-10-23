/* Loop bounds read from memory each iteration (i < l.n). When nothing in
 * the loop may write the bound, its load moves before the loop and the
 * loop keeps one counter; when a call may write it through a stashed
 * pointer, or the loop writes it, it must still be read every iteration.
 * Also a loop that may exit the program, a bound through a parameter, and
 * loops that run zero times. */
#include <stdio.h>
#include <stdlib.h>

typedef struct { int m[64]; int n; } List;

static List *stash;
static long sink;

__attribute__((noinline)) static void fill(List *l, int n) {
    for (int i = 0; i < n; i++)
        l->m[i] = i * 3 + 1;
    l->n = n;
}

__attribute__((noinline)) static void fill_and_stash(List *l, int n) {
    fill(l, n);
    stash = l;
}

__attribute__((noinline)) static long work(int v) { sink += v; return v & 7; }

/* Shrinks the list through the pointer fill_and_stash kept. */
__attribute__((noinline)) static long shrink(int v) {
    if (stash->n > 3)
        stash->n--;
    return v;
}

/* The bound is read through l.n each iteration; the calls cannot write l. */
__attribute__((noinline)) static long untouched(int n) {
    List l;
    long s = 0;
    fill(&l, n);
    for (int i = 0; i < l.n; i++)
        s += work(l.m[i]);
    return s;
}

/* A call shrinks the list through a stashed pointer: the bound changes. */
__attribute__((noinline)) static long stashed(int n) {
    List l;
    long s = 0;
    fill_and_stash(&l, n);
    for (int i = 0; i < l.n; i++)
        s += shrink(l.m[i]);
    return s * 100 + l.n;
}

/* The loop pops from the list itself. */
__attribute__((noinline)) static long popping(int n) {
    List l;
    long s = 0;
    fill(&l, n);
    for (int i = 0; i < l.n; i++) {
        s += l.m[i];
        if (l.m[i] % 2 == 0)
            l.n--;
    }
    return s * 100 + l.n;
}

/* A bound through a pointer parameter, with a call that cannot write it. */
__attribute__((noinline)) static long through_param(const List *l) {
    long s = 0;
    for (int i = 0; i < l->n; i++)
        s += work(l->m[i]) * i;
    return s;
}

/* The loop may exit the program before reading the bound again. */
__attribute__((noinline)) static void maybe_exit(int v) {
    if (v < 0) {
        printf("exit %d\n", v);
        exit(0);
    }
}

__attribute__((noinline)) static long exits(int n) {
    List l;
    long s = 0;
    fill(&l, n);
    for (int i = 0; i < l.n; i++) {
        maybe_exit(l.m[i]);
        s += l.m[i];
    }
    return s;
}

int main(void) {
    List p;
    fill(&p, 9);
    for (int n = 0; n <= 10; n += 5)
        printf("%d: %ld %ld %ld %ld %ld\n", n, untouched(n), stashed(n), popping(n),
               through_param(&p), exits(n));
    printf("sink %ld\n", sink);
    return 0;
}
