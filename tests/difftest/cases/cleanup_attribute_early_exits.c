/* __attribute__((cleanup)) on every way out of a scope. Found by the scope
 * cleanup fuzzer:
 * - return, break, continue and goto out of scopes ran the cleanups in
 *   declaration order (the list was reversed twice); only falling off the
 *   end of a block had them in reverse order, as they must be;
 * - a backward goto to a label before a cleanup variable's declaration
 *   ends its lifetime, so its cleanup runs on each jump back; ccc ran only
 *   those of scopes the goto left;
 * - a computed goto ran every open scope's cleanups on each jump, even
 *   within the scope (gcc runs none; clang rejects one leaving the scope).
 * Every loop here is bounded by a counter. */
#include <stdio.h>
static void cl(int *p) { printf(" %d", *p); }
static int a(int k) {
    int x1 __attribute__((cleanup(cl))) = 1;
    int x2 __attribute__((cleanup(cl))) = 2;
    if (k) return 10;
    int x3 __attribute__((cleanup(cl))) = 3;
    return 20 + x3;
}
static void b(void) {
    int x1 __attribute__((cleanup(cl))) = 1;
    {
        int x2 __attribute__((cleanup(cl))) = 2;
        int x3 __attribute__((cleanup(cl))) = 3;
    }
    int x4 __attribute__((cleanup(cl))) = 4;
}
static void c(int k) {
    int x1 __attribute__((cleanup(cl))) = 1;
    int x2 __attribute__((cleanup(cl))) = 2;
    {
        int x3 __attribute__((cleanup(cl))) = 3;
        int x4 __attribute__((cleanup(cl))) = 4;
        if (k) goto out;
    }
out:
    (void)0;
}
static void d(void) {
    for (int i = 0; i < 2; i++) {
        int x1 __attribute__((cleanup(cl))) = 10 + i;
        int x2 __attribute__((cleanup(cl))) = 20 + i;
        if (i == 0) continue;
        int x3 __attribute__((cleanup(cl))) = 30 + i;
        break;
    }
}
static void e(void) {
    int x1 __attribute__((cleanup(cl))) = 1;
    int x2 __attribute__((cleanup(cl))) = 2;
}
static void f(void) {
    int n = 0;
    int x1 __attribute__((cleanup(cl))) = 1;
again:
    (void)0;
    int x2 __attribute__((cleanup(cl))) = 20 + n;
    {
        int x3 __attribute__((cleanup(cl))) = 30 + n;
        if (n++ < 2) goto again;
    }
    int x4 __attribute__((cleanup(cl))) = 4;
    if (n++ < 4) goto again;
}
static void g(void) {
    int n = 0;
    {
        int x1 __attribute__((cleanup(cl))) = 1;
    top:
        (void)0;
        int x2 __attribute__((cleanup(cl))) = 20 + n;
        n++;
        if (n < 3) goto top;
    }
    {
        int x5 __attribute__((cleanup(cl))) = 5;
        if (n++ < 5) goto top2;
        return;
    }
top2:
    (void)0;
    int x6 __attribute__((cleanup(cl))) = 60 + n;
    if (n++ < 7) goto top2;
}
static int h(int k) {
    int x1 __attribute__((cleanup(cl))) = 1;
    static void *const t[] = {&&a, &&b, &&done};
    int i = 0;
    goto *t[k];
a:
    printf(" a");
    if (++i < 3) goto *t[0];
b:
    printf(" b");
    if (++i < 5) goto *t[1];
done:
    return i;
}
int main(void) {
    printf("f"); f(); printf("\n");
    printf("g"); g(); printf("\n");
    printf("a0"); printf(" =%d\n", a(0));
    printf("a1"); printf(" =%d\n", a(1));
    printf("b"); b(); printf("\n");
    printf("c0"); c(0); printf("\n");
    printf("c1"); c(1); printf("\n");
    printf("d"); d(); printf("\n");
    printf("e"); e(); printf("\n");
    printf("h0"); printf(" =%d\n", h(0));
    printf("h1"); printf(" =%d\n", h(1));
    printf("h2"); printf(" =%d\n", h(2));
    return 0;
}
