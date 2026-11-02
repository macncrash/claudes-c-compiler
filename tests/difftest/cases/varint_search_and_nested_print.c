/* A varint decoder called only from a binary search over encoded keys
 * (inlined there although larger than the usual limits), and a recursive
 * printer of nested lists that appends one character at a time through a
 * small helper with a grow path (inlined into the recursive caller). */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static uint8_t keys[64][10];

static int put_varint(uint8_t *p, uint64_t v) {
    uint8_t tmp[10];
    int n = 0;
    do {
        tmp[n++] = (uint8_t)(v & 0x7f);
        v >>= 7;
    } while (v && n < 9);
    for (int i = 0; i < n; i++)
        p[i] = tmp[n - 1 - i] | (i + 1 < n ? 0x80 : 0);
    return n;
}

static int64_t varint_value(const uint8_t *p) {
    uint64_t k = p[0];
    if (k >= 0x80) {
        uint8_t x;
        k = ((k & 0x7f) << 7) | ((x = *++p) & 0x7f);
        if (x >= 0x80) {
            k = (k << 7) | ((x = *++p) & 0x7f);
            if (x >= 0x80) {
                k = (k << 7) | ((x = *++p) & 0x7f);
                if (x >= 0x80) {
                    k = (k << 7) | ((x = *++p) & 0x7f);
                    if (x >= 0x80) {
                        k = (k << 7) | ((x = *++p) & 0x7f);
                        if (x >= 0x80)
                            k = (k << 7) | (*++p & 0x7f);
                    }
                }
            }
        }
    }
    return (int64_t)k;
}

static int __attribute__((noinline)) search(int n, int64_t want) {
    int lwr = 0, upr = n - 1;
    while (lwr <= upr) {
        int idx = (lwr + upr) >> 1;
        int64_t k = varint_value(keys[idx]);
        if (k < want) lwr = idx + 1;
        else if (k > want) upr = idx - 1;
        else return idx;
    }
    return -1 - lwr;
}

struct out { char *buf; int used, alloc; int grows; };

static void out_grow(struct out *o, char c) {
    o->grows++;
    o->buf[o->used % o->alloc] = c;
    o->used++;
}

static void out_char(struct out *o, char c) {
    if (o->used >= o->alloc) {
        out_grow(o, c);
    } else {
        o->buf[o->used++] = c;
    }
}

static int __attribute__((noinline)) print_tree(struct out *o, int depth, int seed) {
    int n = 0;
    out_char(o, '[');
    for (int i = 0; i < 3; i++) {
        if (i)
            out_char(o, ',');
        if (depth > 0 && (seed + i) % 2)
            n += print_tree(o, depth - 1, seed * 3 + i);
        else {
            out_char(o, (char)('a' + (seed + i) % 26));
            n++;
        }
    }
    out_char(o, ']');
    return n;
}

int main(void) {
    uint64_t v = 1, vals[64];
    for (int i = 0; i < 64; i++) {
        memset(keys[i], 0, sizeof keys[i]);
        put_varint(keys[i], v);
        vals[i] = v;
        v = v * 3 + (uint64_t)i * 1000;
    }
    unsigned long s = 0;
    for (int i = 0; i < 64; i++)
        s = s * 7 + (unsigned long)(search(64, (int64_t)vals[i]) + search(64, (int64_t)vals[i] + 1) * 3);
    printf("search %lu %d %d\n", s, search(64, 0), search(64, (int64_t)1 << 50));
    char big[4096], small[16];
    struct out a = { big, 0, sizeof big, 0 }, b = { small, 0, sizeof small, 0 };
    int na = print_tree(&a, 5, 1), nb = print_tree(&b, 4, 2);
    printf("tree %d %d %.60s\n", na, a.used, big);
    printf("small %d %d %d %.16s\n", nb, b.used, b.grows, small);
    return 0;
}
