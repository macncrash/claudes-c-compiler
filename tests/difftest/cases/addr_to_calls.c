/* Locals whose address is passed to calls: kept in registers between the
 * calls when the callee only reads or writes through the pointer, and left
 * in memory when it keeps the pointer (here in a global another function
 * then writes through). */
#include <stdio.h>

__attribute__((noinline)) static void bump(int *p, int k) { *p += k; }
__attribute__((noinline)) static int peek(const int *p) { return *p * 3; }
__attribute__((noinline)) static void set_if(long *p, int c) { if (c) *p = -7; }

static int *kept;
__attribute__((noinline)) static void keep(int *p) { kept = p; }
__attribute__((noinline)) static void poke(int v) { if (kept) *kept += v; }

__attribute__((noinline)) static int loop_rc(int n) {
    int rc = 0, sum = 0;
    for (int i = 0; i < n; i++) {
        rc = rc * 5 + i;
        if (i % 7 == 3)
            bump(&rc, i);
        if (i % 11 == 5)
            sum += peek(&rc);
        rc &= 0xffff;
        sum ^= rc;
    }
    return sum + rc;
}

__attribute__((noinline)) static long maybe(int n) {
    long v = n;
    for (int i = 0; i < n; i++) {
        set_if(&v, i == n / 2);
        v += i;
    }
    return v;
}

__attribute__((noinline)) static int escaped(int n) {
    int x = n;
    keep(&x);
    for (int i = 0; i < n; i++) {
        x += 2;
        poke(i);
    }
    kept = 0;
    return x;
}

/* The same variable passed twice, and to a call in a loop with no other
 * accesses. */
__attribute__((noinline)) static void add2(int *a, int *b) { *a += *b + 1; }
__attribute__((noinline)) static int twice(int n) {
    int t = 1;
    for (int i = 0; i < n; i++)
        add2(&t, &t);
    return t;
}

/* A status assigned in many places (more than the pass promotes). */
#define S4(k) case k: st = k * 3; break; case k + 1: st = k - 1; break; \
              case k + 2: st ^= k; break; case k + 3: st += k; break;
__attribute__((noinline)) static int status(int n) {
    int st = 0;
    for (int i = 0; i < n; i++) {
        switch (i % 40) {
            S4(0) S4(4) S4(8) S4(12) S4(16) S4(20) S4(24) S4(28) S4(32) S4(36)
        }
        if (i % 9 == 0)
            bump(&st, 1);
    }
    return st;
}

int main(void) {
    for (int n = 0; n < 40; n += 13)
        printf("%d %ld %d %d %d\n", loop_rc(n), maybe(n), escaped(n), twice(n % 20), status(n * 3));
    return 0;
}
