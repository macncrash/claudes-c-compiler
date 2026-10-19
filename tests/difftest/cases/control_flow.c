/* Switch (dense: jump table, sparse: compare chain), goto, computed goto,
 * recursion, loops with break/continue. */
#include <stdio.h>

static const char *dense(int x) {
    switch (x) {
    case 0: return "zero"; case 1: return "one"; case 2: return "two";
    case 3: return "three"; case 4: return "four"; case 5: return "five";
    case 6: return "six"; case 7: return "seven"; default: return "many";
    }
}
static int sparse(int x) {
    switch (x) { case -100: return 1; case 7: return 2; case 1000: return 3; case 123456: return 4; }
    return 0;
}
static int fib(int n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }
static int ack(int m, int n) { return m == 0 ? n + 1 : n == 0 ? ack(m - 1, 1) : ack(m - 1, ack(m, n - 1)); }
static int computed(int i) {
    static void *labels[] = {&&a, &&b, &&c};
    goto *labels[i];
a: return 10;
b: return 20;
c: return 30;
}

int main(void) {
    for (int i = -1; i < 10; i++) printf("%s ", dense(i));
    printf("\n%d %d %d %d %d\n", sparse(-100), sparse(7), sparse(1000), sparse(123456), sparse(5));
    printf("fib %d ack %d\n", fib(25), ack(2, 3));
    printf("computed %d %d %d\n", computed(0), computed(1), computed(2));
    int sum = 0;
    for (int i = 0; i < 100; i++) { if (i % 3 == 0) continue; if (i > 90) break; sum += i; }
    int j = 0;
loop:
    if (j < 5) { sum += j; j++; goto loop; }
    printf("sum %d\n", sum);
    return 0;
}
