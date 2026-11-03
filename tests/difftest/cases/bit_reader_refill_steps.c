/* A decoder loop in the shape of inflate_fast: the input pointer refilled
 * two bytes at a time when bits run short (the second byte read at in + 1
 * after the pointer steps), and the output pointer stepped for a literal on
 * one path of several that merge before the loop's test. */
#include <stdio.h>

static unsigned __attribute__((noinline)) decode(const unsigned char *in, const unsigned char *last,
                                                 unsigned char *out, unsigned char *end)
{
    unsigned long hold = 0;
    unsigned bits = 0;
    unsigned char *start = out;
    do {
        if (bits < 15) {
            hold += (unsigned long)in[0] << bits;
            hold += (unsigned long)in[1] << (bits + 8);
            in += 2;
            bits += 16;
        }
        unsigned code = hold & 0x3f;
        hold >>= 6;
        bits -= 6;
        if (code < 40) {
            *out++ = (unsigned char)(code + 'A');
        } else if (code < 56) {
            unsigned n = code - 37;
            if (out - start >= 2) {
                const unsigned char *from = out - 2;
                while (n-- && out < end)
                    *out++ = *from++;
            }
        } else {
            *out++ = (unsigned char)hold;
            hold >>= 2;
            bits -= 2;
        }
    } while (in < last && out < end);
    unsigned h = (unsigned)(out - start);
    for (unsigned char *p = start; p < out; p++)
        h = h * 31 + *p;
    return h + bits;
}

int main(void)
{
    unsigned char src[3000], dst[8000];
    unsigned x = 7;
    for (int i = 0; i < 3000; i++) {
        x = x * 1664525u + 1013904223u;
        src[i] = (unsigned char)(x >> 24);
    }
    for (int k = 0; k < 4; k++)
        printf("%u\n", decode(src, src + 3000 - 2 - k * 500, dst, dst + 8000 - k * 1000));
    return 0;
}
