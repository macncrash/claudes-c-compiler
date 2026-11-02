/* Loops counting k from 0 below a bound beside an index stepping alike,
 * the test moved onto the index: an LZ77 back-reference copy (overlapping
 * when the distance is below the length, none when the length is 0), an
 * index starting near SIZE_MAX that wraps, a counter from 1 (which must
 * keep its own test), and a copy whose distance is also read in the loop. */
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>

static uint8_t out[512];

static size_t __attribute__((noinline)) back_copy(size_t o, size_t dist, size_t len) {
    for (size_t k = 0; k < len; k++, o++)
        out[o] = out[o - dist];
    return o;
}

static size_t __attribute__((noinline)) wrap_sum(size_t o, size_t n) {
    size_t s = 0;
    for (size_t k = 0; k < n; k++, o++)
        s = s * 31 + o;
    return s ^ o;
}

static size_t __attribute__((noinline)) from_one(size_t o, size_t n) {
    size_t s = 0;
    for (size_t k = 1; k < n; k++, o++)
        s += o * k;
    return s + o;
}

static size_t __attribute__((noinline)) dist_read(size_t o, size_t dist, size_t len) {
    size_t s = 0;
    for (size_t k = 0; k < len; k++, o++) {
        out[o] = (uint8_t)(out[o - dist] + dist);
        s += out[o];
    }
    return s;
}

int main(void) {
    for (int i = 0; i < 16; i++)
        out[i] = (uint8_t)("abcdefghijklmnop"[i]);
    size_t o = 16;
    o = back_copy(o, 3, 10);
    o = back_copy(o, 1, 5);
    o = back_copy(o, 20, 0);
    o = back_copy(o, 16, 16);
    o = back_copy(o, 7, 130);
    unsigned h = 0;
    for (size_t i = 0; i < o; i++)
        h = h * 33 + out[i];
    printf("copy %zu %u %.20s\n", o, h, (const char *)out + 16);
    printf("wrap %zu %zu %zu\n", wrap_sum(SIZE_MAX - 2, 6), wrap_sum(5, 0), wrap_sum(SIZE_MAX, 1));
    printf("from_one %zu %zu %zu\n", from_one(10, 5), from_one(10, 0), from_one(SIZE_MAX - 1, 4));
    printf("dist_read %zu %zu\n", dist_read(300, 4, 9), dist_read(320, 1, 0));
    return 0;
}
