/* Range tests written as two branches (`lo <= x && x <= hi`, and the
 * negated `x < lo || x > hi`), which become one unsigned compare of
 * x - lo. Signed and unsigned, 32 and 64 bits, bounds written either way
 * round, strict and inclusive, at the ends of the type's range, empty
 * ranges, a value merged differently on the two failing edges (stays two
 * branches), and mixed signedness (stays two branches). */
#include <limits.h>
#include <stdint.h>
#include <stdio.h>

__attribute__((noinline)) static int lower(int c) {
    int n = 0;
    if (c >= 'a' && c <= 'z')
        n += 1;
    return n;
}

__attribute__((noinline)) static int outside(int x) {
    if (x < -5 || x > 7)
        return 1;
    return 2;
}

__attribute__((noinline)) static int strict(long x) {
    if (-100 < x && x < 100)
        return 3;
    return 4;
}

__attribute__((noinline)) static int swapped(unsigned x) {
    if (10u <= x && 20u >= x)
        return 5;
    return 6;
}

__attribute__((noinline)) static int ends(int x) {
    int r = 0;
    if (x >= INT_MIN + 1 && x <= INT_MAX - 1)
        r |= 1;
    if (x > INT_MIN && x <= 0)
        r |= 2;
    if (x >= 0 && x < INT_MAX)
        r |= 4;
    return r;
}

__attribute__((noinline)) static int ends64(uint64_t x) {
    int r = 0;
    if (x >= 1 && x <= UINT64_MAX - 1)
        r |= 1;
    if (x > 0x8000000000000000ull && x < UINT64_MAX)
        r |= 2;
    return r;
}

__attribute__((noinline)) static int empty(int x) {
    if (x > 5 && x < 6)
        return 7;
    return 8;
}

/* The failing edges carry different values into the merge. */
__attribute__((noinline)) static int which_fail(int x) {
    int r;
    if (x >= 10) {
        if (x <= 20)
            r = 1;
        else
            r = 2;
    } else {
        r = 3;
    }
    return r;
}

__attribute__((noinline)) static int mixed(int x) {
    if (x >= 0 && (unsigned)x <= 100u)
        return 9;
    return 10;
}

__attribute__((noinline)) static int count_letters(const char *s) {
    int n = 0;
    for (; *s; s++)
        if (*s >= 'A' && *s <= 'Z')
            n++;
        else if (*s >= '0' && *s <= '9')
            n += 100;
    return n;
}

int main(void) {
    int xs[] = { INT_MIN, INT_MIN + 1, -101, -100, -99, -6, -5, -1, 0, 1, 5, 6, 7, 8, 9, 10, 15, 20, 21,
                 96, 97, 98, 121, 122, 123, 99, 100, 101, INT_MAX - 1, INT_MAX };
    unsigned long sig = 0;
    for (unsigned i = 0; i < sizeof xs / sizeof xs[0]; i++) {
        int x = xs[i];
        int v = lower(x) + 2 * outside(x) + 4 * strict(x) + 8 * swapped((unsigned)x) + 16 * ends(x)
            + 256 * empty(x) + 4096 * which_fail(x) + 65536 * mixed(x);
        printf("%d:%d ", x, v);
        sig = sig * 31 + (unsigned)v;
    }
    printf("\n%lu\n", sig);
    uint64_t ys[] = { 0, 1, 2, 0x7fffffffffffffffull, 0x8000000000000000ull, 0x8000000000000001ull,
                      UINT64_MAX - 1, UINT64_MAX };
    for (unsigned i = 0; i < sizeof ys / sizeof ys[0]; i++)
        printf("%d ", ends64(ys[i]));
    printf("\n%d %d\n", count_letters("Hello World 2026, ABC xyz 0 9 :@[`"), count_letters(""));
    return 0;
}
