/* A counter copied and then incremented in place under heavy register
 * pressure (sqlite3VdbeExec's OP_Column: `i = p2; p2 = i + 1`, then
 * aOffset[p2] read). In sqlite3VdbeExec p2 is spilled, and its reload
 * through x0 now goes straight to the copy's register, the increment
 * reading that; here ccc keeps p2 in a register, so this checks the
 * surrounding code generation rather than that fold (the peephole unit
 * test covers it). */
#include <stdio.h>

static unsigned __attribute__((noinline)) mix(unsigned a, unsigned b) {
    return a * 2654435761u ^ (b + 0x9e3779b9u);
}

struct cur { unsigned n; unsigned off[40]; };

static unsigned long __attribute__((noinline)) walk(struct cur *c, int n, unsigned seed) {
    unsigned a = seed, b = seed * 3, d = seed * 7, e = seed * 11, f = seed * 13, g = seed * 17;
    unsigned h = seed * 19, k = seed * 23, m = seed * 29, q = seed * 31, r = seed * 37, s = seed * 41;
    unsigned t = seed * 43, u = seed * 47, v = seed * 53, w = seed * 59;
    unsigned long sum = 0;
    unsigned p2 = 0;
    for (int round = 0; round < n; round++) {
        if (c->n <= p2 + 1)
            p2 = 0;
        unsigned i = p2;
        p2 = i + 1;
        unsigned lo = c->off[p2];
        a = mix(a, lo); b = mix(b, i); d = mix(d, b); e = mix(e, d);
        f = mix(f, e); g = mix(g, f); h = mix(h, g); k = mix(k, h);
        m = mix(m, k); q = mix(q, m); r = mix(r, q); s = mix(s, r);
        t = mix(t, s); u = mix(u, t); v = mix(v, u); w = mix(w, v);
        c->off[i] = lo + (a & 7);
        sum = sum * 31 + (unsigned long)(a ^ b ^ d ^ e ^ f ^ g ^ h ^ k ^ m ^ q ^ r ^ s ^ t ^ u ^ v ^ w) + i;
    }
    return sum + p2;
}

int main(void) {
    struct cur c;
    c.n = 40;
    for (int i = 0; i < 40; i++)
        c.off[i] = (unsigned)(i * 37 + 11) % 23;
    for (unsigned s = 1; s < 5; s++)
        printf("walk %u %lu\n", s, walk(&c, 60 + (int)s, s));
    return 0;
}
