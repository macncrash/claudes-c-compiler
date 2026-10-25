/* Loop counters carried from one loop into the next, and addresses a loop
 * scans and then uses after it.
 *
 * `partition`: Hoare partition, as a quicksort runs it. `i` and `j` are
 * each stepped by their own scan and carried across the swaps, and the
 * element each scan stops at is swapped after it.
 *
 * `late`: a counter compared after its loop with a value computed only
 * after the loop.
 *
 * `stop_*`: scans over elements of each size whose stopping element is read
 * and written after the loop, through a scaled index, through a byte offset
 * and from a struct field (the address not a plain element). `keep_phi`
 * merges the stopping address with another one after the loop. `upto_*`
 * are the same scans as do-whiles ended by a sentinel, so the loop's own
 * load of the element dominates the code after it. */
#include <stdint.h>
#include <stdio.h>

static uint32_t seed = 12345;
static int32_t next(void) {
    seed = seed * 1664525u + 1013904223u;
    return (int32_t)(seed >> 8) % 1000 - 500;
}

__attribute__((noinline)) static void quicksort(int32_t *a, int n) {
    while (n > 8) {
        int32_t x = a[0], y = a[n / 2], z = a[n - 1];
        int32_t p = x < y ? (y < z ? y : x < z ? z : x) : (x < z ? x : y < z ? z : y);
        int i = -1, j = n;
        for (;;) {
            do i++; while (a[i] < p);
            do j--; while (a[j] > p);
            if (i >= j)
                break;
            int32_t t = a[i];
            a[i] = a[j];
            a[j] = t;
        }
        int left = j + 1;
        if (left < n - left) {
            quicksort(a, left);
            a += left;
            n -= left;
        } else {
            quicksort(a + left, n - left);
            n = left;
        }
    }
    for (int i = 1; i < n; i++) {
        int32_t x = a[i];
        int j = i - 1;
        while (j >= 0 && a[j] > x) {
            a[j + 1] = a[j];
            j--;
        }
        a[j + 1] = x;
    }
}

/* Returns where the partition split, with the counters' last values. */
__attribute__((noinline)) static long partition(int32_t *a, int n, int32_t p) {
    int i = -1, j = n, swaps = 0;
    for (;;) {
        do i++; while (i < n && a[i] < p);
        do j--; while (j >= 0 && a[j] > p);
        if (i >= j)
            break;
        int32_t t = a[i];
        a[i] = a[j];
        a[j] = t;
        swaps++;
    }
    return (long)i * 1000000 + (long)j * 1000 + swaps;
}

__attribute__((noinline)) static int limit(int k) { return k * 3 - 1; }

__attribute__((noinline)) static int late(const int *a, int k) {
    int i = 0;
    while (a[i] != 0)
        i++;
    int m = limit(k);
    return (i < m) + 2 * (i == m) + 4 * (i > m - 5);
}

__attribute__((noinline)) static int stop_i8(int8_t *a, int n, int8_t v) {
    int i = 0;
    while (i < n && a[i] != v)
        i++;
    if (i < n)
        a[i] = (int8_t)(a[i] + 1);
    return i;
}

__attribute__((noinline)) static int stop_i16(int16_t *a, int n, int16_t v) {
    int i = n;
    do i--; while (i > 0 && a[i] > v);
    a[i] = (int16_t)(a[i] * 2);
    return i;
}

__attribute__((noinline)) static long stop_i64(int64_t *a, long n, int64_t v) {
    long i = 0;
    while (i < n - 1 && a[i] < v)
        i++;
    int64_t old = a[i];
    a[i] = v;
    return old;
}

__attribute__((noinline)) static double stop_f64(double *a, int n, double v) {
    int i = 0;
    while (i < n - 1 && a[i] < v)
        i++;
    double old = a[i];
    a[i] = v;
    return old + i;
}

/* The index is a byte offset: the scale (1) does not match the access (4). */
__attribute__((noinline)) static int stop_bytes(int32_t *a, int n, int32_t v) {
    char *base = (char *)a;
    long off = 0;
    while (off < (long)(n - 1) * 4 && *(int32_t *)(base + off) < v)
        off += 4;
    *(int32_t *)(base + off) += 100;
    return (int)(off / 4);
}

typedef struct { int16_t tag; int32_t val; } Item;

__attribute__((noinline)) static int stop_field(Item *a, int n, int32_t v) {
    int i = 0;
    while (i < n - 1 && a[i].val < v)
        i++;
    a[i].val = -a[i].val;
    a[i].tag++;
    return i;
}

__attribute__((noinline)) static int32_t keep_phi(int32_t *a, int n, int32_t v, int c) {
    int i = 0;
    while (i < n - 1 && a[i] < v)
        i++;
    int32_t *q = c ? &a[i] : &a[0];
    *q += 7;
    return *q + a[i];
}

/* The element type's scan up to the first element >= v (a[n - 1] is a
 * sentinel), which is then replaced; returns its index and old value. */
#define UPTO(name, T)                                                      \
    __attribute__((noinline)) static long name(T *a, int start, T v) {     \
        int i = start - 1;                                                 \
        do i++; while (a[i] < v);                                          \
        T old = a[i];                                                      \
        a[i] = (T)(old + 1);                                               \
        return i * 1000L + (long)old;                                      \
    }
UPTO(upto_i8, int8_t)
UPTO(upto_u16, uint16_t)
UPTO(upto_i32, int32_t)
UPTO(upto_i64, int64_t)
UPTO(upto_f32, float)
UPTO(upto_f64, double)

__attribute__((noinline)) static long upto_field(Item *a, int start, int32_t v) {
    int i = start - 1;
    do i++; while (a[i].val < v);
    a[i].tag = (int16_t)(a[i].tag + 50);
    return i * 1000L + a[i].tag;
}

__attribute__((noinline)) static long upto_bytes(int32_t *a, long start, int32_t v) {
    char *base = (char *)a;
    long off = start * 4 - 4;
    do off += 4; while (*(int32_t *)(base + off) < v);
    *(int32_t *)(base + off) -= 3;
    return off * 1000L + *(int32_t *)(base + off);
}

/* Down from the top, the stopping element read again after the loop. */
__attribute__((noinline)) static long down_i32(int32_t *a, int n, int32_t v) {
    int j = n;
    do j--; while (a[j] > v);
    return j * 1000L + a[j] + a[j / 2];
}

int main(void) {
    static int32_t big[5000];
    unsigned long sum = 0;
    for (int r = 0; r < 4; r++) {
        int n = r == 3 ? 5000 : 37 * (r + 1);
        for (int i = 0; i < n; i++)
            big[i] = r == 2 ? i % 7 : next();
        quicksort(big, n);
        int ok = 1;
        for (int i = 0; i < n; i++) {
            ok &= i == 0 || big[i - 1] <= big[i];
            sum = sum * 31 + (unsigned long)big[i];
        }
        printf("sort %d %d %lu\n", n, ok, sum);
    }

    int32_t part[50];
    for (int k = 0; k < 4; k++) {
        for (int i = 0; i < 50; i++)
            part[i] = next() / 10;
        int32_t p = k == 0 ? -1000 : k == 1 ? 1000 : part[k * 7];
        printf("partition %ld", partition(part, 50, p));
        for (int i = 0; i < 50; i += 7)
            printf(" %d", part[i]);
        printf("\n");
    }

    int z[] = { 4, 3, 2, 1, 9, 0 };
    printf("late %d %d %d %d\n", late(z, 1), late(z, 2), late(z, 3), late(z + 5, 0));

    int8_t b8[20];
    int16_t b16[20];
    int64_t b64[20];
    double bd[20];
    int32_t b32[20];
    Item items[20];
    for (int i = 0; i < 20; i++) {
        b8[i] = (int8_t)(i * 13 - 100);
        b16[i] = (int16_t)(i * 300 - 2000);
        b64[i] = (int64_t)i * 0x100000001ll - 7;
        bd[i] = i * 1.5 - 4.0;
        b32[i] = i * 11 - 50;
        items[i].tag = (int16_t)i;
        items[i].val = i * 9 - 40;
    }
    printf("i8 %d %d %d\n", stop_i8(b8, 20, (int8_t)(5 * 13 - 100)), stop_i8(b8, 20, 99), b8[5]);
    printf("i16 %d %d %d\n", stop_i16(b16, 20, 1000), stop_i16(b16, 20, -5000), b16[0]);
    printf("i64 %ld %ld %ld\n", stop_i64(b64, 20, 0x500000000ll), stop_i64(b64, 20, 1ll << 62), (long)b64[19]);
    printf("f64 %g %g %g\n", stop_f64(bd, 20, 3.0), stop_f64(bd, 20, 1e9), bd[19]);
    printf("bytes %d %d %d\n", stop_bytes(b32, 20, 40), stop_bytes(b32, 20, 1000), b32[19]);
    printf("field %d %d %d %d\n", stop_field(items, 20, 50), items[10].val, items[10].tag, stop_field(items, 20, 9999));
    printf("phi %d %d\n", keep_phi(b32, 20, 30, 1), keep_phi(b32, 20, 30, 0));

    int8_t s8[16];
    uint16_t s16[16];
    int32_t s32[16];
    int64_t s64[16];
    float sf[16];
    double sd[16];
    Item si[16];
    for (int i = 0; i < 16; i++) {
        s8[i] = (int8_t)(i * 8 - 64);
        s16[i] = (uint16_t)(i * 4000);
        s32[i] = i * 100 - 700;
        s64[i] = (int64_t)i << 33;
        sf[i] = i * 0.75f - 3;
        sd[i] = i * 2.5 - 10;
        si[i].tag = (int16_t)(i * 3);
        si[i].val = i * 10 - 30;
    }
    s8[15] = 120;
    s16[15] = 65535;
    s32[15] = 1000000;
    s64[15] = 1ll << 50;
    sf[15] = 1e6f;
    sd[15] = 1e6;
    si[15].val = 1000000;
    printf("upto %ld %ld %ld %ld\n", upto_i8(s8, 0, 0), upto_i8(s8, 3, 1), upto_i8(s8, 0, -128), upto_i8(s8, 9, 120));
    printf("upto %ld %ld %ld\n", upto_u16(s16, 0, 30000), upto_u16(s16, 2, 0), upto_u16(s16, 0, 65535));
    printf("upto %ld %ld %ld\n", upto_i32(s32, 0, 0), upto_i32(s32, 5, -10000), upto_i32(s32, 1, 800));
    printf("upto %ld %ld\n", upto_i64(s64, 0, 5ll << 33), upto_i64(s64, 4, 1ll << 40));
    printf("upto %ld %ld %ld\n", upto_f32(sf, 0, 1.0f), upto_f32(sf, 2, -100.0f), upto_f64(sd, 0, 7.5));
    printf("upto %ld %ld %ld\n", upto_f64(sd, 6, 1e5), upto_field(si, 0, 41), upto_field(si, 3, 0));
    printf("upto %ld %ld %ld %ld\n", upto_bytes(s32, 0, 0), upto_bytes(s32, 4, -10000), down_i32(s32, 16, 0), down_i32(s32, 16, -700));
    return 0;
}
