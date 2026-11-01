/* Constant-offset addresses stay in their loops (the offset folds into the
 * access), and what used to depend on hoisting them still happens: a local
 * struct's counters kept in registers through a loop, loads of a struct's
 * fields moved out of a rotated loop, and a format-style loop that most
 * calls run once or not at all. */
#include <stdio.h>
#include <string.h>

typedef struct { int hits; int misses; long sum; long weighted; } Tally;
typedef struct { const unsigned char *data; int n; int stride; int bias; } Page;

__attribute__((noinline)) static void show(const Tally *t) {
    printf("%d %d %ld %ld\n", t->hits, t->misses, t->sum, t->weighted);
}

static Tally tally(const int *v, int n, int key) {
    Tally t = { 0, 0, 0, 0 };
    for (int i = 0; i < n; i++) {
        if (v[i] % 7 == key) {
            t.hits++;
            t.sum += v[i];
        } else {
            t.misses++;
        }
        t.weighted = t.weighted * 3 + t.hits;
    }
    show(&t);
    return t;
}

__attribute__((noinline)) static long scan(const Page *p, int rounds) {
    long acc = 0;
    for (int r = 0; r < rounds; r++) {
        for (int i = 0; i < p->n; i++) {
            acc += p->data[i * p->stride] + p->bias;
        }
    }
    return acc;
}

typedef struct { char buf[64]; int len; int cap; int flags; } Out;

__attribute__((noinline)) static void emit(Out *o, const char *fmt, int x) {
    for (const char *f = fmt; *f; f++) {
        if (*f != '%') {
            if (o->len < o->cap) {
                o->buf[o->len++] = *f;
            }
            continue;
        }
        f++;
        if (*f == 'd') {
            char tmp[16];
            int k = snprintf(tmp, sizeof tmp, "%d", x);
            for (int j = 0; j < k && o->len < o->cap; j++) {
                o->buf[o->len++] = tmp[j];
            }
            o->flags |= 1;
        } else if (*f == 0) {
            break;
        }
    }
}

int main(void) {
    int v[50];
    for (int i = 0; i < 50; i++) {
        v[i] = i * i - 17 * i + 3;
    }
    Tally a = tally(v, 50, 3);
    Tally b = tally(v, 0, 1);
    printf("%d %d\n", a.hits + b.hits, a.misses + b.misses);
    unsigned char bytes[64];
    for (int i = 0; i < 64; i++) {
        bytes[i] = (unsigned char)(i * 37);
    }
    Page p = { bytes, 16, 3, -5 };
    printf("%ld %ld\n", scan(&p, 4), scan(&p, 0));
    Out o;
    memset(&o, 0, sizeof o);
    o.cap = 40;
    emit(&o, "", 1);
    emit(&o, "x", 2);
    emit(&o, "a%db%", -42);
    emit(&o, "%d%d", 12345);
    printf("%d %d [%.*s]\n", o.len, o.flags, o.len, o.buf);
    return 0;
}
