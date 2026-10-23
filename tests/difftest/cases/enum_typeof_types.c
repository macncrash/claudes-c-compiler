/* Types of enums, their constants, and __typeof__ with const:
 *  - an enum with no negative value is unsigned int, so its arithmetic and
 *    comparisons are unsigned (e - 1 < 0 is false);
 *  - its constants are int while every value fits int, else they have the
 *    enum's type (unsigned long for 0x100000000; long when a negative value
 *    needs it too, which also makes the enum 8 bytes);
 *  - __typeof__ of a const expression or type declares a const object, and
 *    a cast to a const type in an initializer does not. */
#include <stdio.h>

#define T(x) _Generic((x), int: "int", unsigned: "unsigned", long: "long", \
    unsigned long: "ulong", long long: "llong", unsigned long long: "ullong", \
    int *: "int*", const int *: "const int*", int **: "int**", \
    const int **: "const int**", default: "other")

enum pos { P0, P1, P2 };
enum neg { N0 = -1, N1 };
enum big { B0 = 1, B1 = 0x80000000u };
enum wide { W0 = 1, W = 0x100000000 };
enum mixed { M0 = -1, M1 = 0xffffffff };

static unsigned classify(enum pos e) {
    switch (e) {
    case P0: return 10;
    case P2: return 30;
    default: return 20;
    }
}

int main(void) {
    enum pos e = P0;
    enum neg n = N0;
    enum big b = B1;
    enum wide w = W;
    enum mixed m = M1;
    printf("1 %s %s %s %s %s\n", T(e), T(n), T(b), T(w), T(m));
    printf("2 %s %s %s %s %s %s\n", T(P0), T(N0), T(B1), T(W0), T(W), T(M0));
    printf("3 %d %d %d %d %d\n", (int)sizeof e, (int)sizeof n, (int)sizeof b,
           (int)sizeof w, (int)sizeof m);
    printf("4 %d %d %d %d\n", e - 1 < 0, n - 1 < 0, e > -1, n > -2);
    printf("5 %u %ld %lld %lld\n", e - 1, (long)(e - 3) / 2, (long long)m, (long long)M0);
    e = P2;
    printf("6 %u %u %u\n", e / 2, (e - 5) % 7, classify(e) + classify(P1) + classify(P0));
    unsigned u = 7;
    printf("7 %s %s %s %s\n", T(e + u), T(e + 1L), T(e + n), T(w + 1));
    printf("8 %llx %lx %d\n", (unsigned long long)W, (unsigned long)(W0 - 2), W0 - 2 < 0);

    int x = 1;
    const int *cp = &x;
    const int **cpp = &cp;
    __typeof__(cp) t = cp;
    __typeof__(*cp) ci = 2;
    __typeof__(const int) cj = 3;
    __typeof__(const int *) ck = &x;
    const int arr[2] = { 4, 5 };
    __typeof__(arr[1]) cl = 6;
    __typeof__(*cpp) cm = cp;
    printf("9 %s %s %s %s %s %s %s\n", T(t), T(&ci), T(&cj), T(ck), T(&cl), T(cm), T(&*cp));

    int *p = (const int *)&x;
    int *q = (__typeof__(const int) *)&x;
    int *const pc = &x;
    __typeof__(pc) s = &x;
    __typeof__(int *const) v = &x;
    int y = 8;
    __typeof__(y) z = 9;
    printf("10 %s %s %s %s %s\n", T(p), T(q), T(s), T(v), T(&z));
    z += *t + ci + cj + *ck + cl + *cm + *p + *q + *s + *v;
    printf("11 %d\n", z);
    return 0;
}
