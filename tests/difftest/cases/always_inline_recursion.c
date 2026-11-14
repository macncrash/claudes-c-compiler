/* always_inline functions on a call cycle (self-recursive, mutually
 * recursive) cannot be inlined everywhere; clang inlines them as it would any
 * recursive function and gcc rejects them. ccc forced them: each call was
 * re-inlined into its own body up to the 50,000-instruction budget in every
 * caller, so a handful took over a minute to compile. They now compile
 * quickly, run correctly, and keep one copy of their static locals. Found
 * by the inlining and static locals fuzzer. */
#include <stdio.h>

typedef unsigned long UL;

__attribute__((always_inline)) static inline UL fact(unsigned n) {
    static unsigned calls;
    calls++;
    return n <= 1 ? 1 + 0 * calls : n * fact(n - 1);
}

__attribute__((always_inline)) static inline UL odd(unsigned n, UL acc);
__attribute__((always_inline)) static inline UL even(unsigned n, UL acc) {
    static UL seen;
    seen += n;
    return n == 0 ? acc + seen : odd(n - 1, acc * 3 + n);
}
__attribute__((always_inline)) static inline UL odd(unsigned n, UL acc) {
    return n == 0 ? acc : even(n - 1, acc ^ (n * 7));
}

__attribute__((always_inline)) static inline UL walk(int dep, UL x, unsigned *arr) {
    static unsigned visits;
    visits++;
    UL r = x * 5 + arr[x & 7];
    if (dep > 0) r += walk(dep - 1, r ^ 1, arr) + walk(dep - 1, r + 3, arr);
    for (unsigned i = 0; i < 3; i++) {
        if (((r + i) & 7) == 5) return r ^ i ^ visits;
        r += even(i + dep, r & 0xff);
    }
    arr[r & 7] += (unsigned)r;
    return r + visits;
}

static UL (*const ptr)(int, UL, unsigned *) = walk;

static UL caller_a(unsigned *arr) { return walk(4, 1, arr) + fact(10) + even(9, 2); }
static UL caller_b(unsigned *arr) { return walk(3, 7, arr) ^ odd(12, 5) ^ fact(15); }

int main(void) {
    unsigned arr[8] = {3, 1, 4, 1, 5, 9, 2, 6};
    UL acc = 0;
    for (unsigned i = 0; i < 4; i++) {
        acc = acc * 31 + caller_a(arr);
        acc = acc * 31 + caller_b(arr);
        acc = acc * 31 + ptr((int)i, acc, arr);
        printf("%lu %u %u\n", acc, arr[0], arr[7]);
    }
    printf("%lu %lu %lu\n", fact(20), even(30, 1), odd(31, 1));
    return 0;
}
