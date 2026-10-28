/* A local filled by memcpy from a double on one arm and assigned on the
 * other (SQLite's OP_MakeRecord: memcpy(&v, &pRec->u.r, 8) or v = u.i),
 * then byte-swapped and shifted. A branch dead from the start copies from
 * inside it, so it stays in memory until late; then every read of it is
 * forwarded from the stores, which are left writing a local nothing reads. */
#include <stdio.h>
#include <string.h>

#define OVERRUN 7

typedef struct { union { double r; long long i; } u; unsigned type; } Mem;

static unsigned char sizes[8] = { 0, 1, 2, 3, 4, 6, 8, 8 };

static unsigned __attribute__((noinline)) put_record(unsigned char *out, const Mem *recs, int n) {
    unsigned char *z = out;
    for (int k = 0; k < n; k++) {
        unsigned t = recs[k].type;
        *z++ = (unsigned char)t;
        if (t == 0)
            continue;
        unsigned long long v = 0;
        if (t == 7)
            memcpy(&v, &recs[k].u.r, sizeof v);
        else
            v = (unsigned long long)recs[k].u.i;
        unsigned len = sizes[t];
        v = __builtin_bswap64(v);
        if (OVERRUN) {
            v >>= 8 * (8 - len);
            memcpy(z, &v, 8);
        } else {
            memcpy(z, (unsigned char *)&v + 8 - len, len);
        }
        z += len;
    }
    return (unsigned)(z - out);
}

int main(void) {
    Mem recs[40];
    for (int k = 0; k < 40; k++) {
        recs[k].type = (unsigned)(k * 5 % 8);
        if (recs[k].type == 7)
            recs[k].u.r = k * 1.25 - 7.5;
        else
            recs[k].u.i = (long long)k * 0x10203040506ll - 99;
    }
    unsigned char out[40 * 9 + 8];
    memset(out, 0, sizeof out);
    unsigned len = put_record(out, recs, 40);
    unsigned long h = 5381;
    for (unsigned i = 0; i < len; i++)
        h = h * 33 + out[i];
    printf("len %u hash %lx\n", len, h);
    return 0;
}
