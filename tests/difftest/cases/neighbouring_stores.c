/* Stores to neighbouring words become store pairs: struct fields set
 * together, locals spilled side by side as sqlite3Int64ToText and
 * sqlite3FpDecode keep them, and stores through a pointer that may alias
 * a value read between them, which must stay in order. */
#include <stdio.h>
#include <string.h>

struct rec { long a, b, c, d; int e, f, g, h; };

static void __attribute__((noinline)) fill(struct rec *r, long x, int y) {
    r->a = x;
    r->b = x * 3;
    r->c = 0;
    r->d = x - 7;
    r->e = y;
    r->f = 0;
    r->g = y * 5;
    r->h = y - 2;
}

/* r->b is read through q between the two stores: if q points at r->b the
 * second store's value depends on the first. */
static long __attribute__((noinline)) aliasing(struct rec *r, long *q, long x) {
    r->a = x;
    long seen = *q;
    r->b = seen + 1;
    return seen;
}

static int __attribute__((noinline)) to_text(long v, char *out) {
    char buf[24];
    unsigned long u = v < 0 ? 0UL - (unsigned long)v : (unsigned long)v;
    int i = 23;
    buf[i] = 0;
    do {
        buf[--i] = (char)('0' + u % 10);
        u /= 10;
    } while (u);
    if (v < 0)
        buf[--i] = '-';
    memcpy(out, buf + i, (size_t)(24 - i));
    return 23 - i;
}

static long __attribute__((noinline)) locals(long a, long b) {
    volatile long s[4];
    s[0] = a;
    s[1] = b;
    s[2] = a ^ b;
    s[3] = a + b;
    return s[0] * 7 + s[1] * 5 + s[2] * 3 + s[3];
}

int main(void) {
    struct rec r;
    memset(&r, 0, sizeof r);
    fill(&r, 41, -9);
    printf("fill %ld %ld %ld %ld %d %d %d %d\n", r.a, r.b, r.c, r.d, r.e, r.f, r.g, r.h);
    long s1 = aliasing(&r, &r.b, 100);
    printf("alias same %ld %ld %ld\n", s1, r.a, r.b);
    long other = 5;
    long s2 = aliasing(&r, &other, 200);
    printf("alias other %ld %ld %ld\n", s2, r.a, r.b);
    long s3 = aliasing(&r, &r.a, 300);
    printf("alias first %ld %ld %ld\n", s3, r.a, r.b);
    char out[24];
    long vals[] = { 0, 7, -7, 1234567890123L, -9223372036854775807L - 1 };
    for (int i = 0; i < 5; i++) {
        int n = to_text(vals[i], out);
        printf("text %d %s\n", n, out);
    }
    printf("locals %ld %ld\n", locals(3, 4), locals(-100, 7));
    return 0;
}
