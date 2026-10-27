/* A flag set by the same compare on two paths and tested where they meet;
 * truncations whose uses read only the kept bits (a 16-bit distance stored
 * and decremented, a byte sign-extended again); masks and 32-bit shifts of
 * 64-bit values widened again; multiplies and divides by constants in
 * loops; a doubled table index read and written; a counter stepped in a
 * loop's latch and read after it; a shift feeding a mask loaded later. */
#include <stdio.h>
#include <stdint.h>

struct tally {
    unsigned short dist[64];
    unsigned char len[64];
    unsigned next, end, strstart, match_start;
    unsigned dfreq[32];
};

static const unsigned char dcode[768] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19 };

__attribute__((noinline)) static unsigned tally(struct tally *t, unsigned *lens, int n) {
    unsigned flushes = 0;
    for (int i = 0; i < n; i++) {
        int flush;
        if (lens[i] < 3) {
            t->len[t->next] = (unsigned char)lens[i];
            t->dist[t->next++] = 0;
            flush = t->next == t->end;
        } else {
            unsigned short d = (unsigned short)(t->strstart - t->match_start);
            t->dist[t->next] = d;
            t->len[t->next++] = (unsigned char)(lens[i] - 3);
            d--;
            t->dfreq[(d < 256 ? dcode[d] : dcode[256 + (d >> 7)]) & 31]++;
            flush = t->next == t->end;
            t->strstart += lens[i];
        }
        t->match_start += 7;
        if (flush) {
            flushes++;
            t->next = 0;
        }
    }
    return flushes;
}

/* Flags from compares of different operands, and one sharing a constant. */
__attribute__((noinline)) static int pick(int a, int b, int c, int sel) {
    int f;
    if (sel & 1)
        f = a < b;
    else
        f = c < b;
    int g;
    if (sel & 2)
        g = a == 5;
    else
        g = c == 5;
    return (f ? 10 : 20) + (g ? 1 : 2);
}

__attribute__((noinline)) static int sext_byte(int a, int b) {
    signed char s = (signed char)(a + b);
    return s * 3;
}

static uint64_t rng = 0x9e3779b97f4a7c15ull;
static unsigned next(void) {
    rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
    return (unsigned)(rng >> 32);
}

__attribute__((noinline)) static unsigned words(unsigned char *out, unsigned cap) {
    static const char *w[] = { "alpha", "be", "", "gamma", "delta", "e", "zeta" };
    unsigned i = 0, sum = 0;
    while (i < cap) {
        unsigned r = next();
        const char *p = w[(r % 7) * (r % 7) / 7];
        for (; *p && i < cap; p++)
            out[i++] = (unsigned char)*p;
        if (i < cap)
            out[i++] = next() % 12 == 0 ? '\n' : ' ';
        sum += (unsigned)((rng & 0xffff) / 3) + (unsigned)(rng >> 40) % 1000;
    }
    return sum + i;
}

__attribute__((noinline)) static unsigned hashes(unsigned short *head, unsigned short *prev, const unsigned char *win,
                                                 unsigned n, const unsigned *maskp) {
    unsigned acc = 0;
    for (unsigned s = 0; s + 4 <= n; s++) {
        uint32_t v;
        __builtin_memcpy(&v, win + s, 4);
        unsigned h = ((v * 66521u + 66521u) >> 16) & *maskp;
        unsigned short m = head[h];
        prev[s & 63] = m;
        head[h] = (unsigned short)s;
        acc = acc * 31 + m;
    }
    return acc;
}

int main(void) {
    struct tally t = { .end = 5, .strstart = 70000, .match_start = 1 };
    unsigned lens[40];
    for (int i = 0; i < 40; i++)
        lens[i] = (unsigned)(i * 7 % 11);
    unsigned fl = tally(&t, lens, 40);
    unsigned s = 0;
    for (int i = 0; i < 64; i++)
        s = s * 33 + t.dist[i] + t.len[i];
    for (int i = 0; i < 32; i++)
        s = s * 5 + t.dfreq[i];
    printf("tally %u %u %u %u\n", fl, s, t.next, t.strstart);

    unsigned p = 0;
    for (int sel = 0; sel < 4; sel++)
        for (int a = 3; a < 8; a++)
            p = p * 7 + (unsigned)pick(a, 5, 9 - a, sel);
    printf("pick %u\n", p);
    printf("sext %d %d %d\n", sext_byte(100, 100), sext_byte(-200, 1), sext_byte(0x1234, 0x56));

    static unsigned char buf[3000];
    unsigned ws = words(buf, sizeof buf);
    unsigned h = 0;
    for (unsigned i = 0; i < sizeof buf; i++)
        h = h * 31 + buf[i];
    printf("words %u %u\n", ws, h);

    static unsigned short head[1 << 12], prev[64];
    unsigned mask = (1 << 12) - 1;
    printf("hashes %u %u\n", hashes(head, prev, buf, sizeof buf, &mask), prev[17] + head[mask / 3]);
    return 0;
}
