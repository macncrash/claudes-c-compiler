/* A deflate-style symbol writer: a 16-bit bit buffer or-ed in int and
 * carried from one send to the next, its bytes stored through an index
 * computed before them, an unsigned distance minus one widened as an index,
 * and int operations whose sign later is (or is not) read. */
#include <stdio.h>
#include <string.h>

typedef struct { unsigned short code, len; } ct;

struct state {
    unsigned char *out;
    unsigned long pending;
    unsigned short bi_buf;
    int bi_valid;
    unsigned short *dist;
    unsigned char *lit;
    unsigned n;
};

#define SEND(s, value, length) do { \
    int len_ = (length); \
    if ((s)->bi_valid > 16 - len_) { \
        int val_ = (int)(value); \
        (s)->bi_buf |= (unsigned short)val_ << (s)->bi_valid; \
        (s)->out[(s)->pending++] = (unsigned char)((s)->bi_buf & 0xff); \
        (s)->out[(s)->pending++] = (unsigned char)((unsigned short)(s)->bi_buf >> 8); \
        (s)->bi_buf = (unsigned short)val_ >> (16 - (s)->bi_valid); \
        (s)->bi_valid += len_ - 16; \
    } else { \
        (s)->bi_buf |= (unsigned short)(value) << (s)->bi_valid; \
        (s)->bi_valid += len_; \
    } \
} while (0)

static const unsigned char extra_bits[8] = {0, 0, 1, 1, 2, 3, 4, 5};
static const int base[8] = {0, 1, 2, 4, 8, 12, 20, 36};
static unsigned char dcode[512];

__attribute__((noinline)) static void send_all(struct state *s, const ct *lt, const ct *dt) {
    unsigned sx = 0;
    if (s->n != 0) do {
        unsigned d = s->dist[sx];
        int lc = s->lit[sx++];
        if (d == 0) {
            SEND(s, lt[lc].code, lt[lc].len);
        } else {
            unsigned code = lc & 7;
            SEND(s, lt[code + 257].code, lt[code + 257].len);
            int extra = extra_bits[code];
            if (extra != 0) {
                lc -= base[code];
                lc &= (1 << extra) - 1;
                SEND(s, lc, extra);
            }
            d--;
            code = d < 256 ? dcode[d] : dcode[256 + (d >> 7)];
            SEND(s, dt[code].code, dt[code].len);
            extra = extra_bits[code & 7];
            if (extra != 0) {
                d -= (unsigned)base[code & 7];
                d &= (1u << extra) - 1;
                SEND(s, d, extra);
            }
        }
    } while (sx < s->n);
    SEND(s, lt[256].code, lt[256].len);
}

/* An int or-ed with a negative byte, carried around the loop and read with
 * its sign at the end; and one that only ever reaches 16-bit stores. */
__attribute__((noinline)) static long carried(const signed char *b, int n, unsigned short *low) {
    int t = 0, u = 0;
    for (int i = 0; i < n; i++) {
        t = (t & 0x0f) | b[i];
        u = (u & 0x3fff) << 2 ^ b[i];
        low[i] = (unsigned short)u;
    }
    return (long)t * 3;
}

/* A field loaded once, used both narrowed and widened after a store. */
__attribute__((noinline)) static long field(struct state *s, int len) {
    int v = s->bi_valid;
    long w = v;
    s->bi_valid = 16 - v;
    s->bi_valid += len;
    return w * 1000 + s->bi_valid;
}

/* An unsigned minus one, widened (at 0 it wraps to 2^32 - 1). */
__attribute__((noinline)) static long widened(unsigned x, unsigned y) {
    unsigned d = x - 1;
    unsigned e = x * y + 7;
    return (long)d + (long)(e >> 1);
}

/* Stores whose address is computed before the value they store. */
__attribute__((noinline)) static void put_bytes(unsigned char *out, unsigned long *pos, const unsigned short *v, int n) {
    for (int i = 0; i < n; i++) {
        out[(*pos)++] = (unsigned char)(v[i] & 0xff);
        out[(*pos)++] = (unsigned char)(v[i] >> 8);
    }
}

int main(void) {
    static unsigned char outbuf[4096];
    static unsigned short dist[600];
    static unsigned char lit[600];
    ct lt[286], dt[30];
    unsigned x = 2463534242u;
    for (int i = 0; i < 286; i++) {
        lt[i].len = (unsigned short)(i % 9 + 1);
        lt[i].code = (unsigned short)(i * 37 & ((1 << lt[i].len) - 1));
    }
    for (int i = 0; i < 30; i++) {
        dt[i].len = (unsigned short)(i % 5 + 1);
        dt[i].code = (unsigned short)(i * 11 & ((1 << dt[i].len) - 1));
    }
    for (int i = 0; i < 512; i++)
        dcode[i] = (unsigned char)(i < 256 ? i % 30 : (i - 256) % 30);
    for (int i = 0; i < 600; i++) {
        x ^= x << 13, x ^= x >> 17, x ^= x << 5;
        lit[i] = (unsigned char)x;
        dist[i] = (unsigned short)(x >> 20 & 3 ? 0 : (x >> 8 & 0x7fff) + 1);
    }
    struct state s = {outbuf, 0, 0, 0, dist, lit, 600};
    send_all(&s, lt, dt);
    unsigned long h = 0;
    for (unsigned long i = 0; i < s.pending; i++)
        h = h * 131 + outbuf[i];
    printf("sent %lu bytes hash %lu buf %u valid %d\n", s.pending, h, s.bi_buf, s.bi_valid);

    signed char b[20];
    unsigned short low[20];
    for (int i = 0; i < 20; i++)
        b[i] = (signed char)(i * 29 - 200);
    long c = carried(b, 20, low);
    printf("carried %ld %u %u\n", c, low[7], low[19]);
    printf("carried short %ld\n", carried(b, 3, low));

    s.bi_valid = -5;
    printf("field %ld %d\n", field(&s, 3), s.bi_valid);
    s.bi_valid = 9;
    printf("field %ld %d\n", field(&s, 7), s.bi_valid);

    printf("widened %ld %ld %ld\n", widened(0, 5), widened(1, 3), widened(4000000000u, 3));

    unsigned short v[5] = {0x1234, 0xfffe, 0x8001, 7, 0x7f80};
    unsigned long pos = 3;
    memset(outbuf, 0, 32);
    put_bytes(outbuf, &pos, v, 5);
    printf("put %lu:", pos);
    for (int i = 0; i < 14; i++)
        printf(" %02x", outbuf[i]);
    printf("\n");
    return 0;
}
