/* Loops that call out on every iteration and use one or two globals only
 * on a rare path behind several tests (a counter bumped on overflow, a
 * table read for an escape), beside one using its global on most
 * iterations. A global's address stays where the rare path needs it; the
 * counts and output must come out the same either way. */
#include <stdio.h>
#include <string.h>

static struct { long used; long peak; int frees; } stat;
static const char spaces[] = "        ";
static const unsigned char weights[16] = { 3, 1, 4, 1, 5, 9, 2, 6, 5, 3, 5, 8, 9, 7, 9, 3 };
static int sink;

static int __attribute__((noinline)) cmp(int a, int b) {
    sink += a ^ b;
    return (a > b) - (a < b);
}

/* Binary search calling out per probe; the stat counters only on a probe
 * whose key overflows a byte. */
static int __attribute__((noinline)) search(const int *keys, int n, int want) {
    int lo = 0, hi = n - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (keys[mid] > 255) {
            if (keys[mid] & 1) {
                if (mid != lo) {
                    stat.used -= keys[mid];
                    stat.frees++;
                    if (stat.used < stat.peak)
                        stat.peak = stat.used;
                }
            }
        }
        int c = cmp(keys[mid], want);
        if (c == 0)
            return mid;
        if (c < 0)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return -1;
}

/* Copies text out through a call per character; spaces only for a tab. */
static int __attribute__((noinline)) expand(const char *s, char *out) {
    int n = 0;
    for (; *s; s++) {
        if (*s == '\t') {
            if (n < 200) {
                if (s[1] != '-') {
                    int w = 4 - (n & 3);
                    memcpy(out + n, spaces, w);
                    n += w;
                    continue;
                }
            }
        }
        out[n++] = (char)(cmp(*s, 'a') >= 0 ? *s - 32 : *s);
    }
    out[n] = 0;
    return n;
}

/* The table on nearly every iteration. */
static int __attribute__((noinline)) weigh(const unsigned char *p, int n) {
    int t = 0;
    for (int i = 0; i < n; i++)
        t += weights[p[i] & 15] * cmp(p[i], 7);
    return t;
}

int main(void) {
    int keys[64];
    for (int i = 0; i < 64; i++)
        keys[i] = i * 9 + (i % 5 == 3 ? 300 : 0);
    stat.used = 100000;
    stat.peak = 100000;
    int found = 0;
    for (int w = 0; w < 700; w += 7)
        found += search(keys, 64, w) + 1;
    printf("search %d used %ld peak %ld frees %d\n", found, stat.used, stat.peak, stat.frees);
    char out[256];
    int n = expand("ab\tcd\t\te-f\tg", out);
    printf("expand %d [%s]\n", n, out);
    printf("expand %d\n", expand("", out));
    unsigned char bytes[40];
    for (int i = 0; i < 40; i++)
        bytes[i] = (unsigned char)(i * 37 + 11);
    printf("weigh %d\n", weigh(bytes, 40));
    printf("sink %d\n", sink);
    return 0;
}
