/* A jump to a short block that only tests and branches becomes a copy of
 * it: rotated loop latches counting down (substrFunc's UTF-8 skip), a
 * binary search's bounds test (sqlite3BtreeIndexMoveto), a byte scan's
 * range test (lengthFunc), single-bit tests, and a loop back to a switch.
 * Each walks the edges: zero and negative counts, empty strings, bytes at
 * 0x7f/0x80/0xc0/0xff, and keys before, between and after the table. */
#include <stdio.h>
#include <string.h>

typedef unsigned char u8;

static const u8 *__attribute__((noinline)) skip_chars(const u8 *z, long n) {
    while (*z && n) {
        if (*(z++) >= 0xc0) {
            while ((*z & 0xc0) == 0x80)
                z++;
        }
        n--;
    }
    return z;
}

static int __attribute__((noinline)) char_count(const u8 *z) {
    int len = 0;
    while (*z) {
        len++;
        if (*(z++) >= 0xc0)
            while ((*z & 0xc0) == 0x80)
                z++;
    }
    return len;
}

static int __attribute__((noinline)) bsearch_le(const int *a, int n, int key) {
    int lo = 0, hi = n - 1, best = -1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        if (a[mid] <= key) {
            best = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return best;
}

static unsigned __attribute__((noinline)) bit_walk(unsigned long v, int n) {
    unsigned acc = 0;
    for (int i = 0; i < n; i++) {
        if (v & 8)
            acc = acc * 3 + (unsigned)i;
        else if ((long)v < 0)
            acc ^= 0x55u;
        v = (v >> 1) | (v << 63);
    }
    return acc;
}

static long __attribute__((noinline)) run(const u8 *code, int len) {
    long acc = 0;
    int pc = 0;
    for (;;) {
        u8 op = code[pc];
        switch (op & 3) {
        case 0: acc += op; break;
        case 1: acc -= op >> 2; break;
        case 2: acc *= 3; break;
        default:
            if (acc < 0)
                acc = -acc;
            break;
        }
        if (++pc >= len)
            break;
    }
    return acc;
}

int main(void) {
    static const u8 s1[] = "h\xc3\xa9llo w\xe2\x82\xacrld \x7f\x80\xff\xc0";
    static const u8 s0[] = "";
    long counts[] = { -3, -1, 0, 1, 2, 5, 9, 100 };
    for (int i = 0; i < 8; i++)
        printf("%d %d\n", (int)(skip_chars(s1, counts[i]) - s1), (int)(skip_chars(s0, counts[i]) - s0));
    printf("%d %d %d\n", char_count(s1), char_count(s0), (int)strlen((const char *)s1));
    int a[] = { -50, -7, 0, 3, 3, 9, 41, 1000 };
    for (int key = -60; key <= 1010; key += 13)
        printf("%d:%d ", key, bsearch_le(a, 8, key));
    printf("%d %d\n", bsearch_le(a, 0, 5), bsearch_le(a, 1, -50));
    printf("%u %u %u\n", bit_walk(0, 10), bit_walk(0x8000000000000009ul, 70), bit_walk(~0ul, 3));
    u8 code[64];
    for (int i = 0; i < 64; i++)
        code[i] = (u8)(i * 37 + 11);
    printf("%ld %ld %ld\n", run(code, 1), run(code, 17), run(code, 64));
    return 0;
}
