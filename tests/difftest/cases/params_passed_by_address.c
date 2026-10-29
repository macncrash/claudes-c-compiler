/* Parameters whose address goes to a helper (SQLite's patternCompare reads
 * zPattern and zString with sqlite3Utf8Read(&zString) when a byte is not
 * ASCII, and with *(z++) otherwise), and parameters of every width whose
 * address is taken, written before and after, or kept by the callee. */
#include <stdio.h>

static unsigned __attribute__((noinline)) read_multi(const unsigned char **pz) {
    const unsigned char *z = *pz;
    unsigned c = *z++ & 0x3f;
    while ((*z & 0xc0) == 0x80)
        c = (c << 6) | (*z++ & 0x3f);
    *pz = z;
    return c;
}

#define READ(z) ((z)[0] < 0x80 ? *((z)++) : read_multi(&(z)))

static int __attribute__((noinline)) match(const unsigned char *pat, const unsigned char *str, int depth) {
    unsigned c, d;
    while ((c = READ(pat)) != 0) {
        if (c == '*') {
            if (*pat == 0)
                return 1;
            while (*str) {
                if (depth < 8 && match(pat, str, depth + 1))
                    return 1;
                READ(str);
            }
            return 0;
        }
        d = READ(str);
        if (c != '?' && c != d)
            return 0;
        if (d == 0)
            return 0;
    }
    return *str == 0;
}

static void __attribute__((noinline)) bump(long *p, long k) { *p += k; }
static void __attribute__((noinline)) halve(float *p) { *p /= 2; }

static long __attribute__((noinline)) widths(signed char c, short s, unsigned u, long l, float f) {
    bump(&l, c);
    long t = l;
    s = (short)(s * 3);
    bump((long *)&t, s);
    halve(&f);
    unsigned *pu = &u;
    *pu ^= 0x55;
    return t + l + (long)(f * 4) + u + s + c;
}

static long *kept;
static void __attribute__((noinline)) keep(long *p) { kept = p; }
static long __attribute__((noinline)) captured(long x) {
    keep(&x);
    *kept += 7; /* writes x through the kept pointer */
    return x * 2;
}

int main(void) {
    const unsigned char *pats[] = { (const unsigned char *)"a*c", (const unsigned char *)"h\xc3\xa9*o",
                                    (const unsigned char *)"?\xe2\x82\xac?", (const unsigned char *)"*x*", (const unsigned char *)"" };
    const unsigned char *strs[] = { (const unsigned char *)"abbbc", (const unsigned char *)"h\xc3\xa9llo",
                                    (const unsigned char *)"a\xe2\x82\xac" "b", (const unsigned char *)"yyxyy", (const unsigned char *)"" };
    for (int i = 0; i < 5; i++)
        for (int j = 0; j < 5; j++)
            printf("%d", match(pats[i], strs[j], 0));
    printf("\n");
    printf("widths %ld %ld\n", widths(-5, 300, 0xf0f0u, 1000000, 2.5f), widths(127, -32768, 0, -1, -0.75f));
    printf("captured %ld\n", captured(20));
    return 0;
}
