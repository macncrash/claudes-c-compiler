/* Loops entered in two places (a goto into the middle of the body, so the
 * control flow is irreducible), alone and inside a dispatch loop, plus a
 * hot copy loop after them whose values compete for registers. */
#include <stdio.h>

static unsigned __attribute__((noinline)) resume(const unsigned char *p, int n, int skip_first)
{
    unsigned h = 5381;
    int i = 0;
    if (skip_first)
        goto mid;
    while (i < n) {
        h = h * 33 + p[i];
mid:
        h ^= h >> 7;
        i++;
    }
    return h;
}

static unsigned long __attribute__((noinline)) run(const unsigned char *code, int len, unsigned long *out, int nout)
{
    unsigned long acc = 0;
    long k = 0;
    int pc = 0, w = 0;
    while (pc < len) {
        switch (code[pc++] & 3) {
        case 0:
            acc += pc;
            goto inner;
        case 1:
            k = (long)(acc & 7);
            while (k > 0) {
                acc = acc * 3 + 1;
inner:
                k--;
            }
            break;
        case 2:
            acc ^= acc >> 3;
            break;
        default:
            if (w < nout)
                out[w++] = acc;
            break;
        }
    }
    for (int i = 0; i < w; i++)
        acc += out[i] * (unsigned long)(i + 1);
    return acc;
}

static void __attribute__((noinline)) fill(long *dst, const unsigned short *src, int n, long base, long mask)
{
    for (int i = 0; i < n; i++)
        dst[i] = base + (mask & (long)(unsigned short)((src[i] >> 8) | (src[i] << 8)));
}

int main(void)
{
    unsigned char buf[200];
    for (int i = 0; i < 200; i++)
        buf[i] = (unsigned char)(i * 37 + 11);
    printf("%u %u %u\n", resume(buf, 200, 0), resume(buf, 200, 1), resume(buf, 1, 1));
    unsigned long out[16];
    printf("%lu\n", run(buf, 200, out, 16));
    unsigned short cells[64];
    long ptrs[64];
    for (int i = 0; i < 64; i++)
        cells[i] = (unsigned short)(i * 977);
    fill(ptrs, cells, 64, 1000, 0xfff);
    unsigned long s = 0;
    for (int i = 0; i < 64; i++)
        s = s * 31 + (unsigned long)ptrs[i];
    printf("%lu\n", s);
    return 0;
}
