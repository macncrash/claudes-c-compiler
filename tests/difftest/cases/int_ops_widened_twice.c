/* Int values computed by 32-bit operations, merged by phis and widened
 * for more than one use: a heap sift whose child index is a shift or an
 * increment, read at two arrays; a count of runs added to two arrays as
 * they are found (negative offsets included); and products that overflow
 * 32 bits, whose widening must see them wrapped and sign-extended. */
#include <stdio.h>

static int heap[64], depth[64];

static int __attribute__((noinline)) sift(int k, int n) {
    int v = heap[k];
    int j = k << 1;
    while (j <= n) {
        if (j < n && (heap[j + 1] < heap[j] || (heap[j + 1] == heap[j] && depth[j + 1] <= depth[j])))
            j++;
        if (v < heap[j] || (v == heap[j] && depth[k] <= depth[j]))
            break;
        heap[k] = heap[j];
        depth[k] += depth[j] & 3;
        k = j;
        j <<= 1;
    }
    heap[k] = v;
    return k;
}

/* pageFreeArray's free-run list: nFree incremented as runs are added,
 * widened to index both arrays. */
static int __attribute__((noinline)) merge_runs(const int *ofs, const int *sz, int n) {
    int aOfst[10], aAfter[10];
    int nFree = 0, total = 0;
    for (int i = 0; i < n; i++) {
        int iOfst = ofs[i], iAfter = iOfst + sz[i];
        int j;
        for (j = 0; j < nFree; j++) {
            if (aOfst[j] == iAfter) { aOfst[j] = iOfst; break; }
            else if (aAfter[j] == iOfst) { aAfter[j] = iAfter; break; }
        }
        if (j >= nFree) {
            if (nFree >= 10) {
                for (j = 0; j < nFree; j++)
                    total += aAfter[j] - aOfst[j] + j;
                nFree = 0;
            }
            aOfst[nFree] = iOfst;
            aAfter[nFree] = iAfter;
            nFree++;
        }
    }
    for (int j = 0; j < nFree; j++)
        total = total * 3 + aOfst[j] - aAfter[j];
    return total + nFree;
}

static long __attribute__((noinline)) wrapped(int x, int n) {
    long s = 0;
    int p = x;
    for (int i = 0; i < n; i++) {
        long w = p;
        s = s * 7 + w + (w >> 40);
        p = (int)((unsigned)p * 2654435761u) ^ i;
    }
    return s;
}

int main(void) {
    for (int i = 0; i < 64; i++) {
        heap[i] = (i * 37 + 11) % 50;
        depth[i] = i % 5;
    }
    int r = 0;
    for (int k = 31; k >= 1; k--)
        r = r * 3 + sift(k, 63);
    long h = 0;
    for (int i = 1; i < 64; i++)
        h = h * 31 + heap[i] * 7 + depth[i];
    printf("sift %d %ld\n", r, h);
    int ofs[40], sz[40];
    for (int i = 0; i < 40; i++) {
        ofs[i] = (i * 53) % 400 - 100;
        sz[i] = (i % 3 == 0) ? 53 : 7 + i % 5;
    }
    printf("runs %d %d\n", merge_runs(ofs, sz, 40), merge_runs(ofs + 5, sz + 5, 12));
    printf("wrapped %ld %ld\n", wrapped(123456789, 20), wrapped(-5, 9));
    return 0;
}
