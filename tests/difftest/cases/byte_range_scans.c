/* Scans testing a byte against a range, as lengthFunc counts UTF-8
 * characters with `(u8)(c - 1) < 0x80 - 1` (1 <= c <= 127): a subtract,
 * a byte mask and an unsigned compare, whose mask goes when the
 * constants keep the difference from wrapping into the range. Every byte
 * value is fed through ranges near both ends. */
#include <stdio.h>

/* SQLite's lengthFunc loop, word for word. */
static long __attribute__((noinline)) utf8_len(const unsigned char *z) {
    const unsigned char *z0 = z;
    while (1) {
        if ((unsigned char)(z[0] - 1) < (0x80 - 1)) {
            z++;
        } else if (z[0] == 0) {
            break;
        } else {
            z++;
            while ((z[0] & 0xc0) == 0x80) { z++; z0++; }
        }
    }
    return (long)(z - z0);
}

/* The same idiom with other constants: digits, and a range whose constants
 * reach 256 (the mask must stay). */
static int __attribute__((noinline)) idioms(const unsigned char *z, int n) {
    int c = 0;
    for (int i = 0; i < n; i++) {
        if ((unsigned char)(z[i] - '0') < 10)
            c += 1;
        if ((unsigned char)(z[i] - 200) < 100)
            c += 1000;
    }
    return c;
}

static int __attribute__((noinline)) ascii_prefix(const unsigned char *z, int n) {
    int i = 0;
    while (i < n && z[i] >= 1 && z[i] <= 127)
        i++;
    return i;
}

static int __attribute__((noinline)) count_in(const unsigned char *z, int n, int lo, int hi) {
    int c = 0;
    for (int i = 0; i < n; i++) {
        unsigned char b = z[i];
        if (b >= 32 && b <= 126)
            c += 1;
        if (b >= 200 && b <= 250)
            c += 100;
        if (b >= lo && b <= hi)
            c += 10000;
    }
    return c;
}

int main(void) {
    unsigned char all[257];
    for (int i = 0; i < 256; i++)
        all[i] = (unsigned char)(255 - i);
    all[256] = 0;
    const unsigned char text[] = "plain ascii, caf\xc3\xa9, \xe2\x82\xac 5, \xf0\x9f\x98\x80!";
    printf("len %ld %ld\n", utf8_len(text), utf8_len(all));
    for (int s = 0; s < 256; s += 17)
        printf("prefix %d %d\n", s, ascii_prefix(all + s, 256 - s));
    printf("ascii prefix of text %d\n", ascii_prefix(text, (int)sizeof text - 1));
    printf("count %d %d\n", count_in(all, 256, 0, 0), count_in(all, 256, 128, 255));
    printf("idioms %d %d\n", idioms(all, 256), idioms(text, (int)sizeof text - 1));
    return 0;
}
