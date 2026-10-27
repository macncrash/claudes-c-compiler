/* Type-based alias analysis: stores through 16-bit arrays leave int and
 * pointer fields of a struct in registers (zlib's hash insertion), while
 * every access C allows to alias still sees the latest value: character
 * accesses, members of a union, memcpy, and allocated memory whose
 * effective type changes with each store. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct state {
    unsigned short *head, *prev;
    unsigned char *window;
    unsigned ins_h, mask, w_mask, strstart, count;
};

__attribute__((noinline)) static unsigned insert_run(struct state *s, unsigned n) {
    unsigned last = 0;
    do {
        s->strstart++;
        s->ins_h = ((s->window[s->strstart] * 31u) ^ s->ins_h) & s->mask;
        last = s->prev[s->strstart & s->w_mask] = s->head[s->ins_h];
        s->head[s->ins_h] = (unsigned short)s->strstart;
        s->count += s->ins_h & 1;
    } while (--n);
    return last + s->count;
}

union pun { unsigned u; float f; unsigned short h[2]; };

__attribute__((noinline)) static unsigned through_union(union pun *p, float f) {
    p->u = 0;
    p->f = f;
    unsigned a = p->u;
    p->h[1] = 0x4000;
    return a ^ p->u;
}

__attribute__((noinline)) static unsigned through_chars(unsigned *x, unsigned char *c) {
    *x = 0x01020304u;
    c[0] = 0xff;
    return *x;
}

__attribute__((noinline)) static unsigned through_memcpy(unsigned *x, float f) {
    *x = 7;
    memcpy(x, &f, sizeof f);
    return *x;
}

/* Allocated memory takes the type of each store: reading it back with
 * the type last stored is defined. */
__attribute__((noinline)) static float retyped(void *m, int i) {
    int *ip = m;
    float *fp = m;
    *ip = i;
    int seen = *ip;
    *fp = (float)seen * 0.5f;
    return *fp;
}

__attribute__((noinline)) static unsigned short shorts_then_ints(unsigned *w, unsigned short *h, int n) {
    unsigned total = 0;
    for (int i = 0; i < n; i++) {
        w[0] += 3;
        h[i] = (unsigned short)w[0];
        total += w[1];
    }
    return (unsigned short)(total + w[0]);
}

int main(void) {
    struct state s;
    s.head = calloc(1u << 12, sizeof *s.head);
    s.prev = calloc(1u << 10, sizeof *s.prev);
    s.window = malloc(1u << 14);
    for (unsigned i = 0; i < 1u << 14; i++)
        s.window[i] = (unsigned char)(i * 2654435761u >> 13);
    s.ins_h = 0, s.mask = (1u << 12) - 1, s.w_mask = (1u << 10) - 1, s.strstart = 0, s.count = 0;
    unsigned h = 0;
    for (int k = 0; k < 2000; k++)
        h = h * 33 + insert_run(&s, 1 + k % 7);
    printf("insert %u %u %u\n", h, s.strstart, s.ins_h);

    union pun p;
    printf("union %08x\n", through_union(&p, 1.5f));
    unsigned x;
    printf("chars %08x\n", through_chars(&x, (unsigned char *)&x));
    printf("memcpy %08x\n", through_memcpy(&x, 2.0f));
    void *m = malloc(sizeof(double));
    printf("retyped %.1f\n", retyped(m, 9));
    unsigned w[2] = {1, 5};
    unsigned short hs[16];
    printf("mixed %u %u\n", shorts_then_ints(w, hs, 16), hs[15]);
    free(m);
    free(s.head), free(s.prev), free(s.window);
    return 0;
}
