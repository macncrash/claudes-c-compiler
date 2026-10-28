/* Plain char and short read and then masked to their width (a JSON
 * scanner's z[i] indexing a 256-entry table as unsigned char), masked to
 * fewer bits, masked into another variable while the signed value is still
 * used, and bytes with the top bit set. */
#include <stdio.h>

static const unsigned char kind[256] = {
    ['{'] = 1, ['}'] = 2, ['['] = 3, [']'] = 4, ['"'] = 5, [0xc3] = 6, [0xe2] = 7, [0xff] = 8,
};

static unsigned __attribute__((noinline)) scan(const char *z, int n) {
    unsigned h = 0;
    for (int i = 0; i < n; i++)
        h = h * 9 + kind[(unsigned char)z[i]] + ((unsigned char)z[i] & 0xc0);
    return h;
}

static long __attribute__((noinline)) both(const char *z, int n) {
    long s = 0;
    for (int i = 0; i < n; i++) {
        char c = z[i];
        unsigned u = (unsigned char)c;
        s += c * 3 + u; /* signed and unsigned views of one load */
    }
    return s;
}

static unsigned __attribute__((noinline)) halves(const short *p, int n) {
    unsigned h = 0;
    for (int i = 0; i < n; i++)
        h = h * 31 + (unsigned short)p[i] + ((unsigned short)p[i] & 0x0ff0);
    return h;
}

int main(void) {
    char text[64];
    const char *src = "{\"k\":[1,2,\xc3\xa9,\xe2\x82\xac]}\xff\x80 tail";
    int n = 0;
    while (src[n] && n < 63) {
        text[n] = src[n];
        n++;
    }
    short hw[6] = { -1, -32768, 32767, 0x1234, -0x1234, 0 };
    printf("scan %u\n", scan(text, n));
    printf("both %ld\n", both(text, n));
    printf("halves %u\n", halves(hw, 6));
    return 0;
}
