/* Loads kept or reloaded across calls, depending on what the callee may
 * write: through its pointer arguments, through pointers it captured
 * earlier, through returned pointers, or to globals; and loop-invariant
 * loads that must not run before the loop's first test or before a call
 * that may exit. */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

struct pos { int side; int n; long v[4]; };

static int *saved;
static int counter;

__attribute__((noinline)) static void bump(int *p) { (*p)++; }
__attribute__((noinline)) static void keep(int *p) { saved = p; }
__attribute__((noinline)) static void poke(void) { (*saved) += 10; }
__attribute__((noinline)) static int *same(int *p) { return p; }
__attribute__((noinline)) static int peek(const struct pos *p, int k) { return p->side * 3 + k; }
__attribute__((noinline)) static void tick(void) { counter++; }
__attribute__((noinline)) static void set_side(struct pos *p, int s) { p->side = s; }
__attribute__((noinline)) static void copy_into(struct pos *d, const struct pos *s) { *d = *s; d->n++; }

/* Mutual recursion: only odd() writes the global, through even(). */
static int odd(int n);
__attribute__((noinline)) static int even(int n) { return n == 0 ? 1 : odd(n - 1); }
__attribute__((noinline)) static int odd(int n) { counter += n; return n == 0 ? 0 : even(n - 1); }

__attribute__((noinline)) static void add_all(int count, ...) {
    va_list ap;
    va_start(ap, count);
    for (int i = 0; i < count; i++)
        *va_arg(ap, int *) += i + 1;
    va_end(ap);
}

static void (*hook)(int *) = bump;

/* p may point to the same int as q: q's writes must be seen through p. */
__attribute__((noinline)) static int alias_loop(int *p, int *q, int n) {
    int s = 0;
    for (int i = 0; i < n; i++) {
        bump(q);
        s += *p;
    }
    return s;
}

/* The local's address escapes into `saved`; poke() writes it. */
__attribute__((noinline)) static int captured_local(int n) {
    int x = 1, s = 0;
    keep(&x);
    for (int i = 0; i < n; i++) {
        poke();
        s += x;
    }
    return s;
}

/* Writes through a returned copy of the pointer. */
__attribute__((noinline)) static int returned_alias(int n) {
    int x = 5, s = 0;
    for (int i = 0; i < n; i++) {
        *same(&x) += 2;
        s += x;
    }
    return s;
}

/* A readonly call between loads; then a writing call. */
__attribute__((noinline)) static int readonly_between(struct pos *p) {
    int a = p->side;
    int b = peek(p, 1);
    int c = p->side;
    set_side(p, a + 7);
    int d = p->side;
    return a * 1000 + b * 100 + c * 10 + d;
}

/* Global written by a callee inside the loop. */
__attribute__((noinline)) static int global_loop(int n) {
    int s = 0;
    for (int i = 0; i < n; i++) {
        tick();
        s += counter;
    }
    return s;
}

/* p is NULL when n == 0: the load must stay inside the loop. */
__attribute__((noinline)) static long null_when_empty(const struct pos *p, int n) {
    long s = 0;
    for (int i = 0; i < n; i++)
        s += p->v[i & 3] + p->side * 5 + peek(p, i);
    return s;
}

__attribute__((noinline)) static void check(const struct pos *p, int i) {
    if (!p) {
        printf("check exits at %d\n", i);
        fflush(stdout);
        exit(0);
    }
}

/* The struct written through a param each iteration. */
__attribute__((noinline)) static long copy_loop(struct pos *d, const struct pos *s, int n) {
    long t = 0;
    for (int i = 0; i < n; i++) {
        copy_into(d, s);
        t += d->n + d->side;
        d->side++;
    }
    return t;
}

__attribute__((noinline)) static int recursion_loop(int n) {
    int s = 0;
    for (int i = 0; i < n; i++) {
        even(i);
        s += counter;
    }
    return s;
}

__attribute__((noinline)) static int vararg_loop(int n) {
    int a = 0, b = 100, s = 0;
    for (int i = 0; i < n; i++) {
        add_all(2, &a, &b);
        s += a + b;
    }
    return s;
}

__attribute__((noinline)) static int indirect_loop(int n) {
    int x = 3, s = 0;
    for (int i = 0; i < n; i++) {
        hook(&x);
        s += x;
    }
    return s;
}

/* The load follows a call that exits when p is NULL. */
__attribute__((noinline)) static long exit_before_load(const struct pos *p, int n) {
    long s = 0;
    for (int i = 0; i < n; i++) {
        check(p, i);
        s += p->side;
    }
    return s;
}

int main(void) {
    int shared = 1, other = 1;
    printf("alias same %d\n", alias_loop(&shared, &shared, 5));
    printf("alias distinct %d\n", alias_loop(&other, &shared, 5));
    printf("captured %d\n", captured_local(4));
    printf("returned %d\n", returned_alias(4));
    struct pos p = { 2, 0, { 1, 2, 3, 4 } };
    printf("readonly %d\n", readonly_between(&p));
    counter = 0;
    printf("global %d\n", global_loop(5));
    printf("empty %ld\n", null_when_empty(NULL, 0));
    printf("full %ld\n", null_when_empty(&p, 6));
    struct pos d = { 0 }, s = { 4, 1, { 0 } };
    printf("copy %ld\n", copy_loop(&d, &s, 3));
    counter = 0;
    printf("recursion %d\n", recursion_loop(6));
    printf("vararg %d\n", vararg_loop(3));
    printf("indirect %d\n", indirect_loop(3));
    printf("exit %ld\n", exit_before_load(&p, 3));
    printf("exit %ld\n", exit_before_load(NULL, 3));
    printf("not reached\n");
    return 1;
}
