/* The address of an external function compared on one path through an
 * interpreter-style switch (`pMem->xDel==sqlite3_free` in sqlite3VdbeExec),
 * the address coming through the GOT. Values built up before that load stay
 * in registers across it, so what is live there must be known exactly: the
 * zero-extended, narrowed and masked values here are read whole after the
 * load on some paths and only in part on others. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned long ul;
typedef void (*release_fn)(void *);

struct mem {
    unsigned flags;
    int n;
    release_fn del;
    unsigned char *z;
};

static void __attribute__((noinline)) keep(void *p)
{
    (void)p;
}

static ul __attribute__((noinline)) step(struct mem *m, int op, unsigned a, unsigned b)
{
    unsigned sum = a + b;
    ul wide = (ul)sum * 3;
    unsigned short half = (unsigned short)(m->z[0] << 8 | m->z[1]);
    int flagged = (m->flags & 0x9000) != 0;
    ul r = 0;
    switch (op) {
    case 0:
        if (m->del == free)
            r = sum;
        else
            r = wide;
        break;
    case 1:
        if (m->del == free)
            r = wide + half;
        else
            r = (unsigned)half;
        m->n = (int)sum;
        break;
    case 2:
        if (m->del != (release_fn)free && flagged)
            r = (ul)sum << 33 | half;
        else
            r = (ul)(sum & 0xffff);
        break;
    case 3:
        m->flags = sum;
        r = m->del == (release_fn)memset ? 1 : wide;
        break;
    default:
        r = (ul)m->n + flagged;
        break;
    }
    return r;
}

int main(void)
{
    static unsigned char bytes[4] = {0xfe, 0x01, 0x7f, 0x80};
    static const unsigned vals[] = {0, 1, 0x7fffffffu, 0x80000000u, 0xffffffffu, 0x12345678u};
    release_fn dels[3] = {free, keep, NULL};
    ul h = 0;
    for (int op = 0; op < 5; op++) {
        for (int d = 0; d < 3; d++) {
            for (unsigned i = 0; i < 6; i++) {
                for (unsigned j = 0; j < 6; j++) {
                    struct mem m = {(i & 1) ? 0x1000 : (j & 1) ? 0x8000 : 0x6fff, (int)i, dels[d], bytes + (i & 2)};
                    h = h * 31 + step(&m, op, vals[i], vals[j]);
                    h = h * 31 + (ul)(unsigned)m.n + m.flags;
                }
            }
        }
    }
    printf("%lu\n", h);
    return 0;
}
