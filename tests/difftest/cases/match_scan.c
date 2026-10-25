/* A deflate-style match search: short-circuit tests that also merge the
 * pointers they advance, an exit that reads the address the scan reached,
 * a loop condition with a side effect (`--chain`), and a loop that stores
 * one field of a struct while reading another, or the same bytes through a
 * union. */
#include <stdio.h>
#include <string.h>

struct state {
    unsigned char *window;
    unsigned match_start;
    unsigned prev_length;
    unsigned short *prev;
    unsigned wmask;
};

__attribute__((noinline)) static unsigned longest(struct state *s, unsigned cur, unsigned strstart, unsigned chain) {
    unsigned char *scan = s->window + strstart;
    unsigned char *strend = s->window + strstart + 32;
    int best = (int)s->prev_length;
    unsigned char end1 = scan[best - 1], end = scan[best];
    unsigned limit = strstart > 64 ? strstart - 64 : 0;
    do {
        unsigned char *match = s->window + cur;
        if (match[best] != end || match[best - 1] != end1 || *match != *scan || *++match != scan[1])
            continue;
        scan += 2, match++;
        do {
        } while (*++scan == *++match && *++scan == *++match &&
                 *++scan == *++match && *++scan == *++match &&
                 scan < strend);
        int len = 32 - (int)(strend - scan);
        scan = strend - 32;
        if (len > best) {
            s->match_start = cur;
            best = len;
            if (len >= 30)
                break;
            end1 = scan[best - 1];
            end = scan[best];
        }
    } while ((cur = s->prev[cur & s->wmask]) > limit && --chain != 0);
    return (unsigned)best;
}

/* The first index at which a and b differ, scanning two bytes per test. */
__attribute__((noinline)) static long first_diff(const unsigned char *a, const unsigned char *b, const unsigned char *end) {
    const unsigned char *p = a;
    while (*p == *b && *++p == *++b && p < end) {
        p++, b++;
    }
    return p - a;
}

union word {
    unsigned u;
    unsigned char c[4];
};

struct mixed {
    unsigned *data;
    long n;
    union word w;
    unsigned hits;
};

/* Stores to one field, reads of another (the load of m->data may leave the
 * loop) and of bytes the stores overlap (they may not). */
__attribute__((noinline)) static unsigned fields(struct mixed *m) {
    unsigned sum = 0;
    for (long i = 0; i < m->n; i++) {
        m->hits += m->data[i] & 1;
        m->w.u = m->data[i];
        sum += m->w.c[0] + m->w.c[3];
    }
    return sum + m->hits;
}

int main(void) {
    static unsigned char win[512];
    static unsigned short prev[256];
    unsigned x = 12345;
    for (int i = 0; i < 512; i++) {
        x = x * 1103515245 + 12345;
        win[i] = "abcab"[(x >> 16) % 5];
    }
    for (int i = 0; i < 256; i++)
        prev[i] = (unsigned short)(i > 3 ? i - 3 : 0);
    struct state s = {win, 0, 2, prev, 255};
    unsigned total = 0;
    for (unsigned start = 100; start < 400; start += 7) {
        s.match_start = 0;
        unsigned len = longest(&s, start - 5, start, 16);
        total = total * 31 + len * 1000 + s.match_start;
    }
    printf("longest %u\n", total);

    unsigned char a[64], b[64];
    for (int i = 0; i < 64; i++)
        a[i] = b[i] = (unsigned char)(i * 7);
    for (int k = 0; k < 60; k += 9) {
        b[k] ^= 1;
        printf("diff %d %ld\n", k, first_diff(a, b, a + 62));
        b[k] ^= 1;
    }
    printf("same %ld\n", first_diff(a, b, a + 62));

    unsigned data[40];
    for (int i = 0; i < 40; i++)
        data[i] = 0x01020304u * (unsigned)i + 0x10000001u;
    struct mixed m = {data, 40, {0}, 0};
    printf("fields %u %u\n", fields(&m), m.w.u);
    return 0;
}
