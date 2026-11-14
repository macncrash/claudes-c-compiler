/* A switch whose cases span a whole 64-bit type (case LONG_MIN and case
 * LONG_MAX): the backend measured the span for a jump table as
 * max - min + 1 in 64 bits, which wrapped to 0, and the density check
 * divided by it (ccc panicked). Found by the switch fuzzer. */
#include <stdio.h>
#include <limits.h>

__attribute__((noinline)) static int sl(long k) {
    switch (k) {
    case LONG_MIN: return 1;
    case -1: return 2;
    case 0: return 3;
    case 1: return 4;
    case LONG_MAX: return 5;
    default: return 0;
    }
}

__attribute__((noinline)) static int sll(long long k) {
    switch (k) {
    case LLONG_MIN: return 10;
    case LLONG_MIN + 1: return 11;
    case LLONG_MAX - 1: return 12;
    case LLONG_MAX: return 13;
    }
    return -1;
}

__attribute__((noinline)) static int sul(unsigned long k) {
    switch (k) {
    case 0: return 20;
    case 1: return 21;
    case ULONG_MAX / 2: return 22;
    case ULONG_MAX / 2 + 1: return 23;
    case ULONG_MAX: return 24;
    default: return 25;
    }
}

/* Nearly full spans, and a dense block next to a far value. */
__attribute__((noinline)) static int sn(long k) {
    switch (k) {
    case LONG_MIN + 1 ... LONG_MIN + 3: return 30;
    case 5: case 6: case 7: case 8: return 31;
    case LONG_MAX - 2 ... LONG_MAX: return 32;
    default: return 33;
    }
}

int main(void) {
    static const long lv[] = {LONG_MIN, LONG_MIN + 1, LONG_MIN + 3, LONG_MIN + 4, -2, -1, 0, 1, 2, 5, 8, 9,
                              LONG_MAX - 3, LONG_MAX - 2, LONG_MAX - 1, LONG_MAX};
    static const unsigned long uv[] = {0, 1, 2, ULONG_MAX / 2 - 1, ULONG_MAX / 2, ULONG_MAX / 2 + 1, ULONG_MAX - 1, ULONG_MAX};
    for (unsigned i = 0; i < sizeof lv / sizeof lv[0]; i++)
        printf("%ld: %d %d %d\n", lv[i], sl(lv[i]), sll(lv[i]), sn(lv[i]));
    for (unsigned i = 0; i < sizeof uv / sizeof uv[0]; i++)
        printf("%lu: %d\n", uv[i], sul(uv[i]));
    return 0;
}
