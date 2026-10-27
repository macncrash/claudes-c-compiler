/* Selects reading their operands from registers: several selects on one
 * condition (the last of them reusing the condition's register for its
 * result), selects of constants and of narrowed values, and a hash step
 * whose 32-bit multiplier and addend are built once outside its loop
 * (sqlite3_str_vappendf's `c == 'l'` flags, deflate's insert_string). */
#include <stdio.h>
#include <stdint.h>

__attribute__((noinline)) static long two_on_one(const char *p, long w) {
    int c = *p;
    int is_l = c == 'l';
    const char *q = is_l ? p - 1 : p;
    long k = is_l ? w : 1;
    return (q - p) * 100 + k;
}

__attribute__((noinline)) static unsigned three_on_one(unsigned a, unsigned b, unsigned c) {
    int big = a > b;
    unsigned x = big ? a : b;
    unsigned y = big ? c : a + c;
    unsigned z = big ? 7u : c * 3;
    return x * 31 + y * 7 + z;
}

__attribute__((noinline)) static int narrow_select(unsigned v, int neg) {
    unsigned short h = (unsigned short)v;
    signed char s = (signed char)v;
    int r = neg ? -(int)h : (int)s;
    return r + (unsigned short)(v >> 3);
}

__attribute__((noinline)) static uint32_t hash_run(const unsigned char *w, uint16_t *head, uint16_t *prev, unsigned start, unsigned n) {
    uint32_t h = 0;
    for (unsigned i = 0; i < n; i++) {
        unsigned str = start + i;
        uint32_t v;
        __builtin_memcpy(&v, w + (uint16_t)str, 4);
        h = ((v * 66521 + 66521) >> 16) & 0xfff;
        prev[str & 0x3ff] = head[h];
        head[h] = (uint16_t)str;
    }
    return h;
}

int main(void) {
    char s[4] = "xlq";
    printf("two %ld %ld\n", two_on_one(s + 1, 7), two_on_one(s + 2, 7));
    unsigned t = 0;
    for (unsigned a = 0; a < 40; a++)
        for (unsigned b = 0; b < 40; b += 3)
            t = t * 33 + three_on_one(a * 2654435761u, b * 40503u, a ^ b);
    printf("three %u\n", t);
    unsigned ns = 0;
    for (unsigned v = 0; v < 70000; v += 77)
        ns = ns * 3 + (unsigned)narrow_select(v * 2246822519u, v & 1);
    printf("narrow %u\n", ns);
    static unsigned char win[70000];
    static uint16_t head[4096], prev[1024];
    for (unsigned i = 0; i < sizeof win; i++)
        win[i] = (unsigned char)(i * 2654435761u >> 11);
    uint32_t hh = 0;
    for (unsigned k = 0; k < 300; k++)
        hh = hh * 131 + hash_run(win, head, prev, k * 211, 1 + k % 9);
    unsigned sum = 0;
    for (unsigned i = 0; i < 4096; i++)
        sum = sum * 7 + head[i];
    for (unsigned i = 0; i < 1024; i++)
        sum = sum * 5 + prev[i];
    printf("hash %u %u\n", hh, sum);
    return 0;
}
