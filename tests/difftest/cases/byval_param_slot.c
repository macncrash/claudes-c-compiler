/* Parameters whose stack slot the backend fills from the argument registers
 * (structs passed by value, wide scalars), modified through their first
 * field or an inlined helper given their address, then returned or read.
 * Once the field's zero-offset address folds to the slot itself, the slot
 * is only loaded and stored, but its first value is the argument's, which
 * the IR never stores. */
#include <stdint.h>
#include <stdio.h>

struct bits { unsigned long long a : 8, b : 32; };
struct pair { int32_t x, y; };
struct wide { uint64_t lo, hi; };

__attribute__((noinline)) static struct bits set_b(struct bits s) {
    s.b = 0xcdef1234;
    return s;
}

__attribute__((noinline)) static struct pair bump_x(struct pair p) {
    p.x += 5;
    return p;
}

static void add_to(int32_t *q, int32_t k) { *q += k; }

__attribute__((noinline)) static int32_t through_helper(struct pair p, int n) {
    for (int i = 0; i < n; i++)
        add_to(&p.x, i);
    return p.x * 100 + p.y;
}

__attribute__((noinline)) static uint64_t wide_first(struct wide w) {
    w.lo ^= 0xff;
    return w.lo + w.hi;
}

__attribute__((noinline)) static __int128 wide_scalar(__int128 v, int k) {
    if (k)
        v += 7;
    return v;
}

int main(void) {
    static struct bits g;
    g.a = 12;
    g = set_b(g);
    printf("bits %u %llx\n", (unsigned)g.a, (unsigned long long)g.b);
    struct pair p = { 10, 20 };
    struct pair q = bump_x(p);
    printf("pair %d %d %d\n", q.x, q.y, through_helper(p, 4));
    struct wide w = { 0x1234, 0x10000 };
    printf("wide %llx\n", (unsigned long long)wide_first(w));
    __int128 r = wide_scalar(((__int128)3 << 64) | 9, 1);
    printf("int128 %llx %llx\n", (unsigned long long)(r >> 64), (unsigned long long)r);
    return 0;
}
