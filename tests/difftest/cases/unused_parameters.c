/* Parameters a function never reads (sqlite3BtreeNext's `flags`): no
 * stack slot and no store for them, while the parameters around them, in
 * registers and on the stack, still arrive intact. Unused ints, a double,
 * structs by value, more arguments than registers, a variadic function's
 * unused named parameter, and one that is only cast to void. */
#include <stdarg.h>
#include <stdio.h>

struct pair { long a, b; };
struct big { long v[5]; };

static int __attribute__((noinline)) next(int *cursor, int flags) {
    (void)flags;
    return ++*cursor;
}

static long __attribute__((noinline)) middle(long a, int unused1, long b, double unused2, long c) {
    return a * 100 + b * 10 + c;
}

static long __attribute__((noinline)) structs(struct pair p, struct big unused, struct pair q, int unused3) {
    return p.a - p.b + q.a * q.b;
}

static long __attribute__((noinline)) many(long a0, long a1, long a2, long a3, long a4, long a5, long a6,
                                           long a7, long a8, long a9, long a10) {
    (void)a1; (void)a3;
    return a0 + a2 * 2 + a4 * 3 + a5 * 5 + a6 * 7 + a7 * 11 + a9 * 13 + a10 * 17;
}

static long __attribute__((noinline)) sum_va(int unused, int n, ...) {
    va_list ap;
    va_start(ap, n);
    long s = 0;
    for (int i = 0; i < n; i++)
        s = s * 3 + va_arg(ap, long);
    va_end(ap);
    return s;
}

static void __attribute__((noinline)) nothing(long a, long b) {
}

int main(void) {
    int c = 40;
    for (int i = 0; i < 3; i++)
        printf("next %d\n", next(&c, i * 7));
    printf("middle %ld\n", middle(5, 99, -7, 2.5, 3));
    struct pair p = { 9, 4 }, q = { -3, 6 };
    struct big b = { { 1, 2, 3, 4, 5 } };
    printf("structs %ld\n", structs(p, b, q, 77));
    printf("many %ld\n", many(1, 1000, 2, 1000, 3, 4, 5, 6, 1000, 7, 8));
    printf("va %ld %ld\n", sum_va(123, 4, 1L, 2L, 3L, 4L), sum_va(0, 0));
    nothing(1, 2);
    printf("done\n");
    return 0;
}
