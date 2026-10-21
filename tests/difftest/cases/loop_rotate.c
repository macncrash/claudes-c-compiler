/* Loops whose condition the optimizer copies to the end of the body (loop
 * rotation): zero-trip loops, conditions that load memory or call nothing but
 * do arithmetic, several back edges (continue), loops whose counter or
 * condition value is used after the loop, nested loops, while(1) with break,
 * and loops built from goto. */
#include <stdio.h>

static int data[32];
static char text[] = "rotate the loop, then test it";

__attribute__((noinline)) static int zero_trip(int n) {
    int s = 100;
    for (int i = 0; i < n; i++)
        s += i;
    return s;
}

/* The condition loads from memory. */
__attribute__((noinline)) static int length(const char *p) {
    int n = 0;
    while (p[n] != 0)
        n++;
    return n;
}

__attribute__((noinline)) static int count_spaces(const char *p) {
    int c = 0;
    while (*p) {
        if (*p++ == ' ')
            c++;
    }
    return c;
}

/* continue gives the header several back edges. */
__attribute__((noinline)) static long skip_odd(int n) {
    long s = 0;
    for (int i = 0; i < n; i++) {
        if (data[i & 31] & 1)
            continue;
        if (i % 7 == 3)
            continue;
        s = s * 3 + data[i & 31];
    }
    return s;
}

/* The counter and the last value tested are used after the loop. */
__attribute__((noinline)) static int last_test(int n, int *out) {
    int i = 0, v = 0;
    while (i < n && (v = data[i & 31]) < 900)
        i++;
    *out = v;
    return i;
}

/* Unsigned counter counting down past zero. */
__attribute__((noinline)) static unsigned countdown(unsigned n) {
    unsigned s = 0;
    while (n-- > 0)
        s += n * n;
    return s + n;
}

__attribute__((noinline)) static long nested(int n, int m) {
    long s = 0;
    for (int i = 0; i < n; i++)
        for (int j = i; j < m; j++)
            for (int k = 0; k < j - i; k++)
                s += i ^ j ^ k;
    return s;
}

__attribute__((noinline)) static int forever(int x) {
    int steps = 0;
    while (1) {
        if (x == 1)
            break;
        x = (x & 1) ? 3 * x + 1 : x / 2;
        steps++;
    }
    return steps;
}

__attribute__((noinline)) static int with_goto(int n) {
    int i = 0, s = 0;
top:
    if (i >= n)
        goto done;
    s += i * i;
    i += 2;
    goto top;
done:
    return s + i;
}

/* Pointer-bounded loop with the end computed in the condition. */
__attribute__((noinline)) static int sum_range(const int *p, int n) {
    int s = 0;
    for (const int *q = p; q != p + n; q++)
        s += *q;
    return s;
}

/* Floating-point condition. */
__attribute__((noinline)) static int halvings(double x) {
    int n = 0;
    while (x > 1.0) {
        x *= 0.5;
        n++;
    }
    return n;
}

int main(void) {
    for (int i = 0; i < 32; i++)
        data[i] = (i * 37 + 11) % 1000;
    int v = -1;
    printf("zero %d %d %d\n", zero_trip(0), zero_trip(-5), zero_trip(10));
    printf("length %d %d\n", length(text), length(""));
    printf("spaces %d %d\n", count_spaces(text), count_spaces(""));
    printf("skip %ld %ld\n", skip_odd(100), skip_odd(0));
    int i = last_test(32, &v);
    printf("last %d %d", i, v);
    i = last_test(0, &v);
    printf(" %d %d\n", i, v);
    printf("countdown %u %u\n", countdown(10), countdown(0));
    printf("nested %ld %ld\n", nested(9, 12), nested(0, 3));
    printf("forever %d %d\n", forever(27), forever(1));
    printf("goto %d %d\n", with_goto(11), with_goto(0));
    printf("range %d %d\n", sum_range(data, 32), sum_range(data, 0));
    printf("halvings %d %d\n", halvings(1000.0), halvings(0.5));
    return 0;
}
