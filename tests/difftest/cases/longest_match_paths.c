/* zlib's longest_match and the shapes in it: bytes (signed and unsigned)
 * and 16-bit values loaded before a loop and again in it, compared after
 * widening; unsigned positions added to a window pointer and shifted; a
 * field loaded before a loop that also reads it; and returning the smaller
 * of two values with two returns. */
#include <stdint.h>
#include <stdio.h>

static uint32_t seed = 4242;
static uint32_t rnd(void) {
    seed = seed * 1103515245u + 12345u;
    return seed >> 8;
}

struct st {
    unsigned char *window;
    unsigned short *prev;
    unsigned strstart, prev_length, lookahead, max_chain, good, w_mask, match_start;
    int nice;
};

#define MAX_MATCH 258

__attribute__((noinline)) static unsigned longest(struct st *s, unsigned cur) {
    unsigned chain = s->max_chain;
    unsigned char *scan = s->window + s->strstart;
    int best = (int)s->prev_length;
    int nice = s->nice;
    unsigned limit = s->strstart > 200 ? s->strstart - 200 : 0;
    unsigned char *strend = s->window + s->strstart + MAX_MATCH;
    unsigned char end1 = scan[best - 1];
    unsigned char end = scan[best];
    if (s->prev_length >= s->good) chain >>= 2;
    if ((unsigned)nice > s->lookahead) nice = (int)s->lookahead;
    do {
        unsigned char *match = s->window + cur;
        if (match[best] != end || match[best - 1] != end1 || *match != *scan || match[1] != scan[1])
            continue;
        unsigned char *sc = scan + 2;
        match += 2;
        while (*++sc == *++match && *++sc == *++match && *++sc == *++match && *++sc == *++match && sc < strend)
            ;
        int len = MAX_MATCH - (int)(strend - sc);
        if (len > best) {
            s->match_start = cur;
            best = len;
            if (len >= nice) break;
            end1 = scan[best - 1];
            end = scan[best];
        }
    } while ((cur = s->prev[cur & s->w_mask]) > limit && --chain != 0);
    if ((unsigned)best <= s->lookahead) return (unsigned)best;
    return s->lookahead;
}

/* Signed bytes and halves carried around a loop, compared and summed wide. */
__attribute__((noinline)) static long signed_runs(const signed char *b, const short *h, int n) {
    signed char last = b[0];
    short lh = h[0];
    long s = 0;
    for (int i = 1; i < n; i++) {
        if (b[i] < last) s += last * 3;
        else last = b[i];
        if (h[i] > lh) { s -= lh; lh = h[i]; }
        s += (long)last + lh;
    }
    return s;
}

__attribute__((noinline)) static long smaller(long a, long b) {
    if (a <= b) return a;
    return b;
}

__attribute__((noinline)) static unsigned scaled(const unsigned *t, unsigned i, unsigned k) {
    return t[i] + t[k >> 3] + (unsigned)((i >> 2) & 7);
}

int main(void) {
    static unsigned char window[1 << 15];
    static unsigned short prev[1 << 12];
    for (unsigned i = 0; i < sizeof window; i++)
        window[i] = (unsigned char)"abcab"[rnd() % 5];
    for (unsigned i = 0; i < 1 << 12; i++)
        prev[i] = (unsigned short)(rnd() % 4096);
    struct st s = {window, prev, 0, 2, 0, 32, 8, 4095, 0, 64};
    unsigned long acc = 0;
    for (unsigned pos = 300; pos < 4300; pos += 7) {
        s.strstart = pos;
        s.prev_length = 2 + pos % 5;
        s.lookahead = 1 + pos % 300;
        unsigned len = longest(&s, pos - 1 - pos % 250);
        acc = acc * 31 + len * 977 + s.match_start;
    }
    printf("longest %lu\n", acc);

    signed char b[64];
    short h[64];
    for (int i = 0; i < 64; i++) { b[i] = (signed char)rnd(); h[i] = (short)rnd(); }
    printf("signed %ld %ld\n", signed_runs(b, h, 64), signed_runs(b + 5, h + 9, 40));
    printf("smaller %ld %ld %ld\n", smaller(-5, 3), smaller(1L << 40, -(1L << 40)), smaller(7, 7));
    unsigned t[600];
    for (int i = 0; i < 600; i++) t[i] = rnd();
    printf("scaled %u %u\n", scaled(t, 5, 4000), scaled(t, 599, 7));
    return 0;
}
