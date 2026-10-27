/* A deflate-style Huffman heap: a sift-down whose child choice is a
 * short-circuit test (`j < len && smaller(...)`) adding one to j, tests that
 * compare the same pair twice (`a < b || a == b && ...`), and booleans
 * merged through phis and tested again. */
#include <stdio.h>

struct heap {
    int heap[64];
    int len;
    unsigned char depth[64];
};

#define smaller(f, n, m, d) (f[n] < f[m] || (f[n] == f[m] && d[n] <= d[m]))

__attribute__((noinline)) static void sift(struct heap *h, const unsigned short *f, int k) {
    int v = h->heap[k];
    int j = k << 1;
    while (j <= h->len) {
        if (j < h->len && smaller(f, h->heap[j + 1], h->heap[j], h->depth))
            j++;
        if (smaller(f, v, h->heap[j], h->depth))
            break;
        h->heap[k] = h->heap[j];
        k = j;
        j <<= 1;
    }
    h->heap[k] = v;
}

/* A boolean merged by a phi, then compared with 0 and with 1. */
__attribute__((noinline)) static int merged(int a, int b, int c) {
    int t = a < b || (a == b && c > 0);
    int u = t != 0;
    int w = (a > c && b > c) == 1;
    return u * 4 + w * 2 + (t == 0);
}

/* The same compare before and after a branch, then a different one. */
__attribute__((noinline)) static int classify(long a, long b) {
    if (a < b)
        return -1;
    if (a == b)
        return 0;
    if (a < b + 10)
        return 1;
    return 2;
}

int main(void) {
    unsigned short freq[64];
    struct heap h;
    unsigned x = 12345;
    for (int i = 0; i < 64; i++) {
        x = x * 1103515245 + 12345;
        freq[i] = (unsigned short)((x >> 16) % 7);
        h.depth[i] = (unsigned char)((x >> 8) % 3);
    }
    h.len = 0;
    for (int i = 0; i < 40; i++)
        h.heap[++h.len] = i;
    for (int n = h.len / 2; n >= 1; n--)
        sift(&h, freq, n);
    /* Pop everything in order. */
    printf("order:");
    while (h.len >= 1) {
        int top = h.heap[1];
        printf(" %d", top);
        h.heap[1] = h.heap[h.len--];
        sift(&h, freq, 1);
    }
    printf("\n");

    unsigned s = 0;
    for (int a = -2; a <= 2; a++)
        for (int b = -2; b <= 2; b++)
            for (int c = -1; c <= 1; c++)
                s = s * 3 + (unsigned)merged(a, b, c);
    printf("merged %u\n", s);
    printf("classify %d %d %d %d\n", classify(1, 2), classify(5, 5), classify(9, 3), classify(30, 3));
    return 0;
}
