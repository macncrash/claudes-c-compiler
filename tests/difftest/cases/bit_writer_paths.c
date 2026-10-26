/* zlib's compress_block and the shapes in it: a bit writer whose fields
 * are read after both arms of an if store them (and after stores through
 * a pointer that may point into the writer itself), int shifts of 16-bit
 * codes truncated back to 16 bits, unsigned distances, a table index
 * picked by a ternary, constants on the left of subtracts and shifts in
 * loops, table entries at a constant past a variable index, and unsigned
 * loop counters. */
#include <stdint.h>
#include <stdio.h>

static uint32_t seed = 12345;
static uint32_t rnd(void) {
    seed = seed * 1103515245u + 12345u;
    return seed >> 8;
}

static uint32_t hash(const void *p, unsigned n) {
    const uint8_t *b = p;
    uint32_t h = 2166136261u;
    for (unsigned i = 0; i < n; i++)
        h = (h ^ b[i]) * 16777619u;
    return h;
}

typedef struct { uint16_t code, len; } ct;

struct writer {
    uint8_t *out;
    unsigned long pending;
    uint16_t buf;
    int valid;
    uint16_t *dist;
    uint8_t *lit;
    unsigned count;
};

#define SEND(s, value, length) {                                      \
    int len_ = (length);                                              \
    if ((s)->valid > 16 - len_) {                                     \
        int val_ = (int)(value);                                      \
        (s)->buf |= (uint16_t)val_ << (s)->valid;                     \
        (s)->out[(s)->pending++] = (uint8_t)((s)->buf & 0xff);        \
        (s)->out[(s)->pending++] = (uint8_t)((uint16_t)(s)->buf >> 8); \
        (s)->buf = (uint16_t)val_ >> (16 - (s)->valid);               \
        (s)->valid += len_ - 16;                                      \
    } else {                                                          \
        (s)->buf |= (uint16_t)(value) << (s)->valid;                  \
        (s)->valid += len_;                                           \
    }                                                                 \
}

static uint8_t len_code[256];
static uint8_t dist_code[512];
static int extra_l[29], base_l[29], extra_d[30], base_d[30];

__attribute__((noinline)) static void compress(struct writer *s, const ct *lt, const ct *dt) {
    unsigned sx = 0;
    if (s->count != 0) do {
        unsigned dist = s->dist[sx];
        int lc = s->lit[sx++];
        if (dist == 0) {
            SEND(s, lt[lc].code, lt[lc].len);
        } else {
            unsigned code = len_code[lc];
            SEND(s, lt[code + 257].code, lt[code + 257].len);
            int extra = extra_l[code];
            if (extra != 0) {
                lc = (lc - base_l[code]) & ((1 << extra) - 1);
                SEND(s, lc, extra);
            }
            dist--;
            code = dist < 256 ? dist_code[dist] : dist_code[256 + (dist >> 7)];
            SEND(s, dt[code].code, dt[code].len);
            extra = extra_d[code];
            if (extra != 0) {
                dist = (dist - (unsigned)base_d[code]) & ((1u << extra) - 1);
                SEND(s, dist, extra);
            }
        }
    } while (sx < s->count);
}

/* Fields read on both sides of a merge, one of them written on one path
 * only, and a loop that never writes the field it reads. */
struct pair { int a, b; long c; };

__attribute__((noinline)) static long merge_reads(struct pair *p, int *q, int n) {
    long t = 0;
    for (int i = 0; i < n; i++) {
        if (i & 1) p->a = i; else p->b = -i;
        t += p->a * 3 + p->b;
        if (i % 5 == 0) *q += 7;   /* q may be &p->a */
        t += p->a + p->c;
    }
    return t;
}

/* Low bits of truncated values reused, and full values that must not be. */
__attribute__((noinline)) static uint32_t truncs(long a, long b, int sh, uint16_t *o) {
    uint16_t x = (uint16_t)(a - b);
    o[0] = (uint16_t)((x << sh) | (uint16_t)a);
    int y = (uint16_t)(a + b);
    uint32_t d = (uint32_t)a - (uint32_t)b;
    o[1] = (uint16_t)d;
    o[2] = (uint16_t)(d << 3);
    return (uint32_t)(y + 70000) ^ d;
}

__attribute__((noinline)) static long left_consts(const int *v, int n) {
    long t = 0;
    for (int i = 0; i < n; i++)
        t += (16 - v[i]) + (1 << (v[i] & 15)) - (100000 - v[i]) + (int)(3u >> (v[i] & 1));
    return t;
}

/* Table entries at a constant past a variable index (`tree[code + 257]`),
 * scaled and not, negative, and one whose address escapes. */
struct pt { short a, b; };
__attribute__((noinline)) static long index_consts(const struct pt *t, const short *h, const unsigned char *k, int n, const short **last) {
    long s = 0;
    for (int i = 0; i < n; i++) {
        unsigned c = k[i];
        s += t[c + 3].a * 5 + t[c + 3].b + h[c + 7] - h[(long)c - 2 + 4];
        *last = &h[c + 1];
    }
    return s;
}

/* Unsigned counters indexing memory, bounded by an unsigned test (the
 * bound reloaded each time), and started near 2^32 and read after the
 * loop. */
struct cnt { unsigned n; unsigned char *p; };
__attribute__((noinline)) static unsigned long counters(struct cnt *c, unsigned start, unsigned n2) {
    unsigned long s = 0;
    unsigned i = 0;
    if (c->n != 0) do {
        s = s * 31 + c->p[i++];
        if (s & 1) c->n -= (c->n > 3);
    } while (i < c->n);
    unsigned j = start;
    do {
        s += j;
    } while (++j < n2);
    unsigned k = 0;
    do {
        s += c->p[k] * (k * 2654435761u);
    } while (++k <= 5);
    return s + j * 3u + k;
}

int main(void) {
    for (int i = 0; i < 256; i++) len_code[i] = (uint8_t)(i * 29 / 256);
    for (int i = 0; i < 512; i++) dist_code[i] = (uint8_t)(i * 30 / 512);
    for (int i = 0; i < 29; i++) { extra_l[i] = i < 8 ? 0 : (i - 4) / 4; base_l[i] = i * 3; }
    for (int i = 0; i < 30; i++) { extra_d[i] = i < 4 ? 0 : (i - 2) / 2; base_d[i] = i * 5; }
    ct lt[286], dt[30];
    /* Codes fit their lengths, as zlib's do. */
    for (int i = 0; i < 286; i++) { lt[i].len = (uint16_t)(1 + rnd() % 15); lt[i].code = (uint16_t)(rnd() & ((1u << lt[i].len) - 1)); }
    for (int i = 0; i < 30; i++) { dt[i].len = (uint16_t)(1 + rnd() % 15); dt[i].code = (uint16_t)(rnd() & ((1u << dt[i].len) - 1)); }

    static uint8_t out[1 << 16];
    static uint16_t dist[4096];
    static uint8_t lit[4096];
    for (int round = 0; round < 4; round++) {
        unsigned n = 1 + rnd() % 4096;
        for (unsigned i = 0; i < n; i++) {
            dist[i] = (uint16_t)(rnd() % 3 ? 0 : 1 + rnd() % 32768);
            lit[i] = (uint8_t)rnd();
        }
        struct writer s = {out, 0, 0, 0, dist, lit, n};
        compress(&s, lt, dt);
        printf("compress %u: %lu %d %04x %08x\n", n, s.pending, s.valid, s.buf, hash(out, (unsigned)s.pending));
    }

    /* The first output byte lands on the writer's own buf, which the
     * second byte is read from: one 15-bit literal flushes once. */
    struct writer w = {0, 0, 0x1234, 3, dist, lit, 1};
    w.out = (uint8_t *)&w.buf + 1;
    dist[0] = 0;
    lit[0] = 7;
    lt[7].code = 0x5a5b;
    lt[7].len = 15;
    compress(&w, lt, dt);
    printf("self %lu %d %04x %02x\n", w.pending, w.valid, w.buf, ((uint8_t *)&w.buf)[2]);

    struct pair p = {1, 2, 3};
    int other = 5;
    printf("merge %ld %ld\n", merge_reads(&p, &other, 50), merge_reads(&p, &p.a, 50));

    uint16_t o[3];
    long vals[] = {0, 1, -1, 65535, 65536, -70000, 0x123456789L, -0x7fffffffL};
    uint32_t acc = 0;
    for (int i = 0; i < 8; i++)
        for (int j = 0; j < 8; j++) {
            acc = acc * 31 + truncs(vals[i], vals[j], (i + j) % 16, o);
            acc = acc * 31 + o[0] + o[1] * 7u + o[2] * 13u;
        }
    printf("truncs %08x\n", acc);

    int v[100];
    for (int i = 0; i < 100; i++) v[i] = (int)rnd() - (1 << 23);
    printf("left %ld\n", left_consts(v, 100));

    static struct pt pts[300];
    static short hs[300];
    unsigned char ks[64];
    for (int i = 0; i < 300; i++) { pts[i].a = (short)rnd(); pts[i].b = (short)rnd(); hs[i] = (short)rnd(); }
    for (int i = 0; i < 64; i++) ks[i] = (uint8_t)rnd();
    const short *last = 0;
    long r = index_consts(pts, hs, ks, 64, &last);
    printf("index %ld %ld\n", r, (long)(last - hs));

    static unsigned char bytes[200];
    for (int i = 0; i < 200; i++) bytes[i] = (uint8_t)rnd();
    struct cnt c = {150, bytes};
    printf("counters %lu", counters(&c, 0xfffffffdu, 0xffffffffu));
    c.n = 7;
    printf(" %lu", counters(&c, 3, 2));
    c.n = 1;
    printf(" %lu %u\n", counters(&c, 0, 100), c.n);
    return 0;
}
