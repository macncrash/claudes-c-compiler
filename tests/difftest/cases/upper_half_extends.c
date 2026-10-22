/* Sign and zero extensions of 32-bit values whose upper half may or may not
 * be read later: shifts by an int amount, int indexes that go negative,
 * int results widened on only some paths, and pointer arguments that the
 * register allocator should keep in registers across loops. */
#include <stdio.h>
#include <stdint.h>

__attribute__((noinline)) static uint64_t shift_by_int(uint64_t x, int s, int t) {
    int a = s - t;                  /* negative before masking */
    return (x << (a & 63)) ^ (x >> ((s + t) & 63));
}

__attribute__((noinline)) static uint32_t shift32(uint32_t x, int s) {
    int a = s * 3 - 7;
    return (x << (a & 31)) | (x >> ((32 - a) & 31));
}

__attribute__((noinline)) static long widen_some_paths(int a, int b, int which) {
    int d = a - b;                  /* may be negative */
    if (which & 1)
        return d;                   /* sign extended to long */
    if (which & 2)
        return (unsigned)d;         /* zero extended */
    return d & 0xff;                /* only low bits */
}

__attribute__((noinline)) static long index_negative(const long *base, int i, int j) {
    int k = i - j;                  /* negative index into the middle */
    return base[k] + base[k + 1] * 3;
}

__attribute__((noinline)) static int32_t mix_loop(const int32_t *v, int n, int32_t seed) {
    int32_t h = seed;
    for (int i = 0; i < n; i++) {
        int32_t x = v[i] - h;       /* wraps negative */
        h = (h ^ x) * 16777619;
        h += (int32_t)((int64_t)x * 7 >> 3);
    }
    return h;
}

struct board { uint64_t bb[12]; int side; int count; };

__attribute__((noinline)) static uint64_t walk(struct board *b, const int *idx, int n) {
    uint64_t acc = 0;
    for (int i = 0; i < n; i++) {
        int sq = idx[i] - 3;        /* negative for small entries */
        int piece = b->side * 6 + (i % 6);
        acc += b->bb[piece] >> (sq & 63);
        acc ^= (uint64_t)(int64_t)sq;
        b->count += sq;
    }
    return acc;
}

int main(void) {
    printf("%llx\n", (unsigned long long)shift_by_int(0x0123456789abcdefULL, 3, 10));
    printf("%llx\n", (unsigned long long)shift_by_int(0xfedcba9876543210ULL, 40, 2));
    printf("%x %x\n", shift32(0x80000001u, 1), shift32(0x12345678u, 9));
    for (int w = 0; w < 4; w++)
        printf("%ld %ld\n", widen_some_paths(5, 9, w), widen_some_paths(-7, 300, w));

    long arr[20];
    for (int i = 0; i < 20; i++)
        arr[i] = i * 11 - 50;
    printf("%ld %ld\n", index_negative(arr + 10, 2, 7), index_negative(arr + 10, 8, 1));

    int32_t v[16];
    for (int i = 0; i < 16; i++)
        v[i] = (i * 0x2545F491) ^ (i << 28);
    printf("%d\n", mix_loop(v, 16, -12345));

    struct board b;
    for (int i = 0; i < 12; i++)
        b.bb[i] = 0x9e3779b97f4a7c15ULL * (i + 1);
    b.side = 1;
    b.count = -100;
    int idx[10] = { 0, 1, 2, 3, 4, 50, 63, 64, 66, 7 };
    uint64_t r = walk(&b, idx, 10);
    printf("%llx %d\n", (unsigned long long)r, b.count);
    return 0;
}
