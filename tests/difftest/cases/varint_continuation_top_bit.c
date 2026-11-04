/* Bytes and halfwords compared against their top bit (`>= 0x80`,
 * `< 0x8000`), in the shape of SQLite's varint decoding in
 * btreeParseCellPtr: each loaded byte's continuation bit decides whether
 * another byte follows. The compares may become tests of bit 7 or 15 only
 * because the values were loaded zero-extended; the signed-byte and
 * full-word versions here must stay compares. */
#include <stdio.h>

typedef unsigned char u8;
typedef unsigned short u16;

static unsigned __attribute__((noinline)) get_varint(const u8 *p, unsigned long long *v)
{
    unsigned long long x = *p;
    if (x < 0x80) {
        *v = x;
        return 1;
    }
    x &= 0x7f;
    unsigned n = 1;
    do {
        x = (x << 7) | (p[n] & 0x7f);
    } while (p[n++] >= 0x80 && n < 9);
    *v = x;
    return n;
}

static int __attribute__((noinline)) halves(const u16 *h, int n)
{
    int c = 0;
    for (int i = 0; i < n; i++)
        if (h[i] < 0x8000)
            c += h[i] & 7;
        else
            c -= 1;
    return c;
}

static int __attribute__((noinline)) signed_bytes(const signed char *s, int n)
{
    int c = 0;
    for (int i = 0; i < n; i++)
        if (s[i] >= 0x80 - 256 + 128) /* s[i] >= 0: a sign test, not bit 7 of a byte */
            c++;
    return c;
}

static int __attribute__((noinline)) words(const unsigned *w, int n)
{
    int c = 0;
    for (int i = 0; i < n; i++)
        if (w[i] < 0x80)
            c++;
    return c;
}

int main(void)
{
    static const u8 bytes[] = {
        0x00, 0x7f, 0x80, 0x01, 0xff, 0x7f, 0x81, 0x80, 0x00, 0x7e,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x05, 0x80, 0x7f,
    };
    static const u16 hw[] = {0, 1, 0x7fff, 0x8000, 0x8001, 0xffff, 0x80, 0x7f80};
    static const unsigned w[] = {0, 0x7f, 0x80, 0x17f, 0x10000, 0xffffff7f, 5};
    unsigned long h = 0;
    for (unsigned i = 0; i < sizeof bytes - 9; i++) {
        unsigned long long v;
        unsigned n = get_varint(bytes + i, &v);
        h = h * 31 + n;
        h = h * 31 + (unsigned long)v;
    }
    h = h * 31 + (unsigned)halves(hw, 8);
    h = h * 31 + (unsigned)signed_bytes((const signed char *)bytes, (int)sizeof bytes);
    h = h * 31 + (unsigned)words(w, 7);
    printf("%lu\n", h);
    return 0;
}
