/* 32-bit compares against constants no compare immediate encodes, whose
 * low 32 bits load in fewer instructions zero-extended than sign-extended
 * (INT_MIN, 0x80001234), and others, in an abs in the shape of
 * sqlite3AbsInt32 inside a search for the nearest value. */
#include <stdio.h>

static int __attribute__((noinline)) abs32(int x)
{
    if (x >= 0)
        return x;
    if (x == (int)0x80000000)
        return 0x7fffffff;
    return -x;
}

static int __attribute__((noinline)) nearest(const int *v, int n, int target)
{
    int best = 0, dist = abs32(v[0] - target);
    for (int i = 1; i < n; i++) {
        int d = v[i] - target;
        if (d < 0)
            d = d == (int)0x80000000 ? 0x7fffffff : -d;
        if (d < dist) {
            dist = d;
            best = i;
        }
    }
    return best * 1000 + (dist & 0xff);
}

static unsigned __attribute__((noinline)) classify(int s, unsigned u)
{
    unsigned r = 0;
    r = r * 3 + (s == (int)0x80001234);
    r = r * 3 + (s < (int)0x80001234);
    r = r * 3 + (s > (int)0xfffe0001);
    r = r * 3 + (s == (int)0xfffe0001);
    r = r * 3 + (u >= 0x80000000u);
    r = r * 3 + (u == 0x90000000u);
    r = r * 3 + (u < 0x12345678u);
    r = r * 3 + (s != -2147483647 - 1);
    return r;
}

int main(void)
{
    int v[64];
    unsigned x = 12345;
    for (int i = 0; i < 64; i++) {
        x = x * 1103515245u + 12345u;
        v[i] = (int)(x >> 2) - (1 << 29);
    }
    int w[3] = {5, (int)0x80000000, -7};
    printf("%d %d %d %d\n", abs32(5), abs32(-5), abs32((int)0x80000000), abs32((int)0x80000001));
    printf("%d %d %d %d\n", nearest(v, 64, 0), nearest(v, 64, -(1 << 29)), nearest(v, 64, 1 << 29), nearest(w, 3, 0));
    int samples[] = {0, -1, 1, (int)0x80000000, (int)0x80001234, (int)0x80001233, (int)0x80001235,
                     (int)0xfffe0001, (int)0xfffe0000, (int)0xfffe0002, 0x7fffffff, 0x12345678};
    unsigned h = 0;
    for (unsigned i = 0; i < sizeof samples / sizeof samples[0]; i++)
        for (unsigned j = 0; j < sizeof samples / sizeof samples[0]; j++)
            h = h * 31 + classify(samples[i], (unsigned)samples[j] + (j == 3 ? 0x10000000u : 0));
    printf("%u\n", h);
    return 0;
}
