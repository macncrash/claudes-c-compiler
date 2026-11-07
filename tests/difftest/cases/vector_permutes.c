/* __builtin_shufflevector in each pattern with an instruction of its own, at each
 * width, and lists that come close: one lane out, the two sources swapped, a lane
 * left free, a result of another length. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define NI __attribute__((noinline))

static unsigned long long hash;

static NI void note(const void *p, unsigned n)
{
    const unsigned char *b = p;
    for (unsigned i = 0; i < n; i++)
        hash = hash * 1099511628211ull + b[i];
    printf("%016llx\n", hash);
}

typedef uint8_t uint8_16 __attribute__((vector_size(16)));
static NI void fill_uint8_16(uint8_16 *a, uint8_16 *b, int k)
{
    for (int i = 0; i < 16; i++) {
        (*a)[i] = (uint8_t)(i * 3 + k + 1);
        (*b)[i] = (uint8_t)(i * 7 + k + 100);
    }
}
static NI void test_uint8_16(void)
{
    uint8_16 a, b, r;
    fill_uint8_16(&a, &b, 1);
    r = __builtin_shufflevector(a, b, 0, 16, 2, 18, 4, 20, 6, 22, 8, 24, 10, 26, 12, 28, 14, 30);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 0, 16, 2, 18, 4, 20, 6, 22, 8, 24, 10, 26, 12, 28, 14, 30);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 0, 16, 2, 18, 4, 20, 6, 22, 8, 24, 10, 26, 12, 28, 14, 30);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 0, 16, 2, 18, 4, 20, 6, 22, 8, 24, 10, 26, 12, 28, 14, 31);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 1, 17, 3, 19, 5, 21, 7, 23, 9, 25, 11, 27, 13, 29, 15, 31);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 1, 17, 3, 19, 5, 21, 7, 23, 9, 25, 11, 27, 13, 29, 15, 31);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 1, 17, 3, 19, 5, 21, 7, 23, 9, 25, 11, 27, 13, 29, 15, 31);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 1, 17, 3, 19, 5, 21, 7, 23, 9, 25, 11, 27, 13, 29, 15, 0);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 0, 16, 1, 17, 2, 18, 3, 19, 4, 20, 5, 21, 6, 22, 7, 23);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 0, 16, 1, 17, 2, 18, 3, 19, 4, 20, 5, 21, 6, 22, 7, 23);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 0, 16, 1, 17, 2, 18, 3, 19, 4, 20, 5, 21, 6, 22, 7, 23);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 0, 16, 1, 17, 2, 18, 3, 19, 4, 20, 5, 21, 6, 22, 7, 24);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 8, 24, 9, 25, 10, 26, 11, 27, 12, 28, 13, 29, 14, 30, 15, 31);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 8, 24, 9, 25, 10, 26, 11, 27, 12, 28, 13, 29, 14, 30, 15, 31);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 8, 24, 9, 25, 10, 26, 11, 27, 12, 28, 13, 29, 14, 30, 15, 31);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 8, 24, 9, 25, 10, 26, 11, 27, 12, 28, 13, 29, 14, 30, 15, 0);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 31);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 1, 3, 5, 7, 9, 11, 13, 15, 17, 19, 21, 23, 25, 27, 29, 31);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 1, 3, 5, 7, 9, 11, 13, 15, 17, 19, 21, 23, 25, 27, 29, 31);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 1, 3, 5, 7, 9, 11, 13, 15, 17, 19, 21, 23, 25, 27, 29, 31);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 1, 3, 5, 7, 9, 11, 13, 15, 17, 19, 21, 23, 25, 27, 29, 0);
    note(&r, sizeof r);
    /* In place, and fed back. */
    a = __builtin_shufflevector(a, b, 0, 16, 1, 17, 2, 18, 3, 19, 4, 20, 5, 21, 6, 22, 7, 23);
    b = __builtin_shufflevector(b, a, 1, 17, 3, 19, 5, 21, 7, 23, 9, 25, 11, 27, 13, 29, 15, 31);
    note(&a, sizeof a);
    note(&b, sizeof b);
}

typedef uint8_t uint8_8 __attribute__((vector_size(8)));
static NI void fill_uint8_8(uint8_8 *a, uint8_8 *b, int k)
{
    for (int i = 0; i < 8; i++) {
        (*a)[i] = (uint8_t)(i * 3 + k + 1);
        (*b)[i] = (uint8_t)(i * 7 + k + 100);
    }
}
static NI void test_uint8_8(void)
{
    uint8_8 a, b, r;
    fill_uint8_8(&a, &b, 1);
    r = __builtin_shufflevector(a, b, 0, 8, 2, 10, 4, 12, 6, 14);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 0, 8, 2, 10, 4, 12, 6, 14);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 0, 8, 2, 10, 4, 12, 6, 14);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 0, 8, 2, 10, 4, 12, 6, 15);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 1, 9, 3, 11, 5, 13, 7, 15);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 1, 9, 3, 11, 5, 13, 7, 15);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 1, 9, 3, 11, 5, 13, 7, 15);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 1, 9, 3, 11, 5, 13, 7, 0);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 0, 8, 1, 9, 2, 10, 3, 11);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 0, 8, 1, 9, 2, 10, 3, 11);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 0, 8, 1, 9, 2, 10, 3, 11);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 0, 8, 1, 9, 2, 10, 3, 12);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 4, 12, 5, 13, 6, 14, 7, 15);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 4, 12, 5, 13, 6, 14, 7, 15);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 4, 12, 5, 13, 6, 14, 7, 15);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 4, 12, 5, 13, 6, 14, 7, 0);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 0, 2, 4, 6, 8, 10, 12, 14);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 0, 2, 4, 6, 8, 10, 12, 14);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 0, 2, 4, 6, 8, 10, 12, 14);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 0, 2, 4, 6, 8, 10, 12, 15);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 1, 3, 5, 7, 9, 11, 13, 15);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 1, 3, 5, 7, 9, 11, 13, 15);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 1, 3, 5, 7, 9, 11, 13, 15);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 1, 3, 5, 7, 9, 11, 13, 0);
    note(&r, sizeof r);
    /* In place, and fed back. */
    a = __builtin_shufflevector(a, b, 0, 8, 1, 9, 2, 10, 3, 11);
    b = __builtin_shufflevector(b, a, 1, 9, 3, 11, 5, 13, 7, 15);
    note(&a, sizeof a);
    note(&b, sizeof b);
}

typedef uint16_t uint16_8 __attribute__((vector_size(16)));
static NI void fill_uint16_8(uint16_8 *a, uint16_8 *b, int k)
{
    for (int i = 0; i < 8; i++) {
        (*a)[i] = (uint16_t)(i * 3 + k + 1);
        (*b)[i] = (uint16_t)(i * 7 + k + 100);
    }
}
static NI void test_uint16_8(void)
{
    uint16_8 a, b, r;
    fill_uint16_8(&a, &b, 1);
    r = __builtin_shufflevector(a, b, 0, 8, 2, 10, 4, 12, 6, 14);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 0, 8, 2, 10, 4, 12, 6, 14);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 0, 8, 2, 10, 4, 12, 6, 14);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 0, 8, 2, 10, 4, 12, 6, 15);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 1, 9, 3, 11, 5, 13, 7, 15);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 1, 9, 3, 11, 5, 13, 7, 15);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 1, 9, 3, 11, 5, 13, 7, 15);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 1, 9, 3, 11, 5, 13, 7, 0);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 0, 8, 1, 9, 2, 10, 3, 11);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 0, 8, 1, 9, 2, 10, 3, 11);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 0, 8, 1, 9, 2, 10, 3, 11);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 0, 8, 1, 9, 2, 10, 3, 12);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 4, 12, 5, 13, 6, 14, 7, 15);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 4, 12, 5, 13, 6, 14, 7, 15);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 4, 12, 5, 13, 6, 14, 7, 15);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 4, 12, 5, 13, 6, 14, 7, 0);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 0, 2, 4, 6, 8, 10, 12, 14);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 0, 2, 4, 6, 8, 10, 12, 14);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 0, 2, 4, 6, 8, 10, 12, 14);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 0, 2, 4, 6, 8, 10, 12, 15);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 1, 3, 5, 7, 9, 11, 13, 15);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 1, 3, 5, 7, 9, 11, 13, 15);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 1, 3, 5, 7, 9, 11, 13, 15);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 1, 3, 5, 7, 9, 11, 13, 0);
    note(&r, sizeof r);
    /* In place, and fed back. */
    a = __builtin_shufflevector(a, b, 0, 8, 1, 9, 2, 10, 3, 11);
    b = __builtin_shufflevector(b, a, 1, 9, 3, 11, 5, 13, 7, 15);
    note(&a, sizeof a);
    note(&b, sizeof b);
}

typedef uint16_t uint16_4 __attribute__((vector_size(8)));
static NI void fill_uint16_4(uint16_4 *a, uint16_4 *b, int k)
{
    for (int i = 0; i < 4; i++) {
        (*a)[i] = (uint16_t)(i * 3 + k + 1);
        (*b)[i] = (uint16_t)(i * 7 + k + 100);
    }
}
static NI void test_uint16_4(void)
{
    uint16_4 a, b, r;
    fill_uint16_4(&a, &b, 1);
    r = __builtin_shufflevector(a, b, 0, 4, 2, 6);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 0, 4, 2, 6);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 0, 4, 2, 6);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 0, 4, 2, 7);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 1, 5, 3, 7);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 1, 5, 3, 7);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 1, 5, 3, 7);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 1, 5, 3, 0);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 0, 4, 1, 5);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 0, 4, 1, 5);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 0, 4, 1, 5);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 0, 4, 1, 6);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 2, 6, 3, 7);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 2, 6, 3, 7);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 2, 6, 3, 7);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 2, 6, 3, 0);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 0, 2, 4, 6);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 0, 2, 4, 6);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 0, 2, 4, 6);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 0, 2, 4, 7);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 1, 3, 5, 7);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 1, 3, 5, 7);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 1, 3, 5, 7);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 1, 3, 5, 0);
    note(&r, sizeof r);
    /* In place, and fed back. */
    a = __builtin_shufflevector(a, b, 0, 4, 1, 5);
    b = __builtin_shufflevector(b, a, 1, 5, 3, 7);
    note(&a, sizeof a);
    note(&b, sizeof b);
}

typedef uint32_t uint32_4 __attribute__((vector_size(16)));
static NI void fill_uint32_4(uint32_4 *a, uint32_4 *b, int k)
{
    for (int i = 0; i < 4; i++) {
        (*a)[i] = (uint32_t)(i * 3 + k + 1);
        (*b)[i] = (uint32_t)(i * 7 + k + 100);
    }
}
static NI void test_uint32_4(void)
{
    uint32_4 a, b, r;
    fill_uint32_4(&a, &b, 1);
    r = __builtin_shufflevector(a, b, 0, 4, 2, 6);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 0, 4, 2, 6);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 0, 4, 2, 6);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 0, 4, 2, 7);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 1, 5, 3, 7);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 1, 5, 3, 7);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 1, 5, 3, 7);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 1, 5, 3, 0);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 0, 4, 1, 5);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 0, 4, 1, 5);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 0, 4, 1, 5);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 0, 4, 1, 6);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 2, 6, 3, 7);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 2, 6, 3, 7);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 2, 6, 3, 7);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 2, 6, 3, 0);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 0, 2, 4, 6);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 0, 2, 4, 6);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 0, 2, 4, 6);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 0, 2, 4, 7);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 1, 3, 5, 7);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 1, 3, 5, 7);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 1, 3, 5, 7);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 1, 3, 5, 0);
    note(&r, sizeof r);
    /* In place, and fed back. */
    a = __builtin_shufflevector(a, b, 0, 4, 1, 5);
    b = __builtin_shufflevector(b, a, 1, 5, 3, 7);
    note(&a, sizeof a);
    note(&b, sizeof b);
}

typedef uint32_t uint32_2 __attribute__((vector_size(8)));
static NI void fill_uint32_2(uint32_2 *a, uint32_2 *b, int k)
{
    for (int i = 0; i < 2; i++) {
        (*a)[i] = (uint32_t)(i * 3 + k + 1);
        (*b)[i] = (uint32_t)(i * 7 + k + 100);
    }
}
static NI void test_uint32_2(void)
{
    uint32_2 a, b, r;
    fill_uint32_2(&a, &b, 1);
    r = __builtin_shufflevector(a, b, 0, 2);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 0, 2);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 0, 2);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 0, 3);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 1, 3);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 1, 3);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 1, 3);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 1, 0);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 0, 2);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 0, 2);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 0, 2);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 0, 3);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 1, 3);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 1, 3);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 1, 3);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 1, 0);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 0, 2);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 0, 2);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 0, 2);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 0, 3);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 1, 3);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 1, 3);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 1, 3);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 1, 0);
    note(&r, sizeof r);
    /* In place, and fed back. */
    a = __builtin_shufflevector(a, b, 0, 2);
    b = __builtin_shufflevector(b, a, 1, 3);
    note(&a, sizeof a);
    note(&b, sizeof b);
}

typedef uint64_t uint64_2 __attribute__((vector_size(16)));
static NI void fill_uint64_2(uint64_2 *a, uint64_2 *b, int k)
{
    for (int i = 0; i < 2; i++) {
        (*a)[i] = (uint64_t)(i * 3 + k + 1);
        (*b)[i] = (uint64_t)(i * 7 + k + 100);
    }
}
static NI void test_uint64_2(void)
{
    uint64_2 a, b, r;
    fill_uint64_2(&a, &b, 1);
    r = __builtin_shufflevector(a, b, 0, 2);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 0, 2);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 0, 2);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 0, 3);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 1, 3);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 1, 3);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 1, 3);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 1, 0);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 0, 2);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 0, 2);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 0, 2);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 0, 3);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 1, 3);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 1, 3);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 1, 3);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 1, 0);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 0, 2);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 0, 2);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 0, 2);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 0, 3);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 1, 3);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 1, 3);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 1, 3);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 1, 0);
    note(&r, sizeof r);
    /* In place, and fed back. */
    a = __builtin_shufflevector(a, b, 0, 2);
    b = __builtin_shufflevector(b, a, 1, 3);
    note(&a, sizeof a);
    note(&b, sizeof b);
}

typedef int8_t int8_16 __attribute__((vector_size(16)));
static NI void fill_int8_16(int8_16 *a, int8_16 *b, int k)
{
    for (int i = 0; i < 16; i++) {
        (*a)[i] = (int8_t)(i * 3 + k + 1);
        (*b)[i] = (int8_t)(i * 7 + k + 100);
    }
}
static NI void test_int8_16(void)
{
    int8_16 a, b, r;
    fill_int8_16(&a, &b, 1);
    r = __builtin_shufflevector(a, b, 0, 16, 2, 18, 4, 20, 6, 22, 8, 24, 10, 26, 12, 28, 14, 30);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 0, 16, 2, 18, 4, 20, 6, 22, 8, 24, 10, 26, 12, 28, 14, 30);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 0, 16, 2, 18, 4, 20, 6, 22, 8, 24, 10, 26, 12, 28, 14, 30);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 0, 16, 2, 18, 4, 20, 6, 22, 8, 24, 10, 26, 12, 28, 14, 31);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 1, 17, 3, 19, 5, 21, 7, 23, 9, 25, 11, 27, 13, 29, 15, 31);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 1, 17, 3, 19, 5, 21, 7, 23, 9, 25, 11, 27, 13, 29, 15, 31);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 1, 17, 3, 19, 5, 21, 7, 23, 9, 25, 11, 27, 13, 29, 15, 31);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 1, 17, 3, 19, 5, 21, 7, 23, 9, 25, 11, 27, 13, 29, 15, 0);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 0, 16, 1, 17, 2, 18, 3, 19, 4, 20, 5, 21, 6, 22, 7, 23);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 0, 16, 1, 17, 2, 18, 3, 19, 4, 20, 5, 21, 6, 22, 7, 23);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 0, 16, 1, 17, 2, 18, 3, 19, 4, 20, 5, 21, 6, 22, 7, 23);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 0, 16, 1, 17, 2, 18, 3, 19, 4, 20, 5, 21, 6, 22, 7, 24);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 8, 24, 9, 25, 10, 26, 11, 27, 12, 28, 13, 29, 14, 30, 15, 31);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 8, 24, 9, 25, 10, 26, 11, 27, 12, 28, 13, 29, 14, 30, 15, 31);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 8, 24, 9, 25, 10, 26, 11, 27, 12, 28, 13, 29, 14, 30, 15, 31);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 8, 24, 9, 25, 10, 26, 11, 27, 12, 28, 13, 29, 14, 30, 15, 0);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 31);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 1, 3, 5, 7, 9, 11, 13, 15, 17, 19, 21, 23, 25, 27, 29, 31);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 1, 3, 5, 7, 9, 11, 13, 15, 17, 19, 21, 23, 25, 27, 29, 31);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 1, 3, 5, 7, 9, 11, 13, 15, 17, 19, 21, 23, 25, 27, 29, 31);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 1, 3, 5, 7, 9, 11, 13, 15, 17, 19, 21, 23, 25, 27, 29, 0);
    note(&r, sizeof r);
    /* In place, and fed back. */
    a = __builtin_shufflevector(a, b, 0, 16, 1, 17, 2, 18, 3, 19, 4, 20, 5, 21, 6, 22, 7, 23);
    b = __builtin_shufflevector(b, a, 1, 17, 3, 19, 5, 21, 7, 23, 9, 25, 11, 27, 13, 29, 15, 31);
    note(&a, sizeof a);
    note(&b, sizeof b);
}

typedef int16_t int16_8 __attribute__((vector_size(16)));
static NI void fill_int16_8(int16_8 *a, int16_8 *b, int k)
{
    for (int i = 0; i < 8; i++) {
        (*a)[i] = (int16_t)(i * 3 + k + 1);
        (*b)[i] = (int16_t)(i * 7 + k + 100);
    }
}
static NI void test_int16_8(void)
{
    int16_8 a, b, r;
    fill_int16_8(&a, &b, 1);
    r = __builtin_shufflevector(a, b, 0, 8, 2, 10, 4, 12, 6, 14);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 0, 8, 2, 10, 4, 12, 6, 14);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 0, 8, 2, 10, 4, 12, 6, 14);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 0, 8, 2, 10, 4, 12, 6, 15);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 1, 9, 3, 11, 5, 13, 7, 15);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 1, 9, 3, 11, 5, 13, 7, 15);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 1, 9, 3, 11, 5, 13, 7, 15);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 1, 9, 3, 11, 5, 13, 7, 0);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 0, 8, 1, 9, 2, 10, 3, 11);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 0, 8, 1, 9, 2, 10, 3, 11);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 0, 8, 1, 9, 2, 10, 3, 11);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 0, 8, 1, 9, 2, 10, 3, 12);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 4, 12, 5, 13, 6, 14, 7, 15);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 4, 12, 5, 13, 6, 14, 7, 15);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 4, 12, 5, 13, 6, 14, 7, 15);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 4, 12, 5, 13, 6, 14, 7, 0);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 0, 2, 4, 6, 8, 10, 12, 14);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 0, 2, 4, 6, 8, 10, 12, 14);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 0, 2, 4, 6, 8, 10, 12, 14);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 0, 2, 4, 6, 8, 10, 12, 15);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 1, 3, 5, 7, 9, 11, 13, 15);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 1, 3, 5, 7, 9, 11, 13, 15);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 1, 3, 5, 7, 9, 11, 13, 15);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 1, 3, 5, 7, 9, 11, 13, 0);
    note(&r, sizeof r);
    /* In place, and fed back. */
    a = __builtin_shufflevector(a, b, 0, 8, 1, 9, 2, 10, 3, 11);
    b = __builtin_shufflevector(b, a, 1, 9, 3, 11, 5, 13, 7, 15);
    note(&a, sizeof a);
    note(&b, sizeof b);
}

typedef int32_t int32_4 __attribute__((vector_size(16)));
static NI void fill_int32_4(int32_4 *a, int32_4 *b, int k)
{
    for (int i = 0; i < 4; i++) {
        (*a)[i] = (int32_t)(i * 3 + k + 1);
        (*b)[i] = (int32_t)(i * 7 + k + 100);
    }
}
static NI void test_int32_4(void)
{
    int32_4 a, b, r;
    fill_int32_4(&a, &b, 1);
    r = __builtin_shufflevector(a, b, 0, 4, 2, 6);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 0, 4, 2, 6);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 0, 4, 2, 6);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 0, 4, 2, 7);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 1, 5, 3, 7);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 1, 5, 3, 7);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 1, 5, 3, 7);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 1, 5, 3, 0);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 0, 4, 1, 5);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 0, 4, 1, 5);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 0, 4, 1, 5);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 0, 4, 1, 6);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 2, 6, 3, 7);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 2, 6, 3, 7);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 2, 6, 3, 7);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 2, 6, 3, 0);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 0, 2, 4, 6);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 0, 2, 4, 6);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 0, 2, 4, 6);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 0, 2, 4, 7);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 1, 3, 5, 7);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 1, 3, 5, 7);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 1, 3, 5, 7);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 1, 3, 5, 0);
    note(&r, sizeof r);
    /* In place, and fed back. */
    a = __builtin_shufflevector(a, b, 0, 4, 1, 5);
    b = __builtin_shufflevector(b, a, 1, 5, 3, 7);
    note(&a, sizeof a);
    note(&b, sizeof b);
}

typedef int64_t int64_2 __attribute__((vector_size(16)));
static NI void fill_int64_2(int64_2 *a, int64_2 *b, int k)
{
    for (int i = 0; i < 2; i++) {
        (*a)[i] = (int64_t)(i * 3 + k + 1);
        (*b)[i] = (int64_t)(i * 7 + k + 100);
    }
}
static NI void test_int64_2(void)
{
    int64_2 a, b, r;
    fill_int64_2(&a, &b, 1);
    r = __builtin_shufflevector(a, b, 0, 2);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 0, 2);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 0, 2);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 0, 3);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 1, 3);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 1, 3);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 1, 3);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 1, 0);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 0, 2);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 0, 2);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 0, 2);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 0, 3);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 1, 3);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 1, 3);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 1, 3);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 1, 0);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 0, 2);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 0, 2);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 0, 2);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 0, 3);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 1, 3);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 1, 3);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 1, 3);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 1, 0);
    note(&r, sizeof r);
    /* In place, and fed back. */
    a = __builtin_shufflevector(a, b, 0, 2);
    b = __builtin_shufflevector(b, a, 1, 3);
    note(&a, sizeof a);
    note(&b, sizeof b);
}

typedef float float_4 __attribute__((vector_size(16)));
static NI void fill_float_4(float_4 *a, float_4 *b, int k)
{
    for (int i = 0; i < 4; i++) {
        (*a)[i] = (float)(i * 3 + k + 1);
        (*b)[i] = (float)(i * 7 + k + 100);
    }
}
static NI void test_float_4(void)
{
    float_4 a, b, r;
    fill_float_4(&a, &b, 1);
    r = __builtin_shufflevector(a, b, 0, 4, 2, 6);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 0, 4, 2, 6);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 0, 4, 2, 6);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 0, 4, 2, 7);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 1, 5, 3, 7);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 1, 5, 3, 7);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 1, 5, 3, 7);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 1, 5, 3, 0);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 0, 4, 1, 5);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 0, 4, 1, 5);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 0, 4, 1, 5);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 0, 4, 1, 6);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 2, 6, 3, 7);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 2, 6, 3, 7);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 2, 6, 3, 7);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 2, 6, 3, 0);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 0, 2, 4, 6);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 0, 2, 4, 6);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 0, 2, 4, 6);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 0, 2, 4, 7);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 1, 3, 5, 7);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 1, 3, 5, 7);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 1, 3, 5, 7);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 1, 3, 5, 0);
    note(&r, sizeof r);
    /* In place, and fed back. */
    a = __builtin_shufflevector(a, b, 0, 4, 1, 5);
    b = __builtin_shufflevector(b, a, 1, 5, 3, 7);
    note(&a, sizeof a);
    note(&b, sizeof b);
}

typedef double double_2 __attribute__((vector_size(16)));
static NI void fill_double_2(double_2 *a, double_2 *b, int k)
{
    for (int i = 0; i < 2; i++) {
        (*a)[i] = (double)(i * 3 + k + 1);
        (*b)[i] = (double)(i * 7 + k + 100);
    }
}
static NI void test_double_2(void)
{
    double_2 a, b, r;
    fill_double_2(&a, &b, 1);
    r = __builtin_shufflevector(a, b, 0, 2);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 0, 2);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 0, 2);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 0, 3);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 1, 3);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 1, 3);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 1, 3);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 1, 0);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 0, 2);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 0, 2);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 0, 2);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 0, 3);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 1, 3);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 1, 3);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 1, 3);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 1, 0);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 0, 2);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 0, 2);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 0, 2);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 0, 3);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 1, 3);
    note(&r, sizeof r);
    r = __builtin_shufflevector(b, a, 1, 3);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, a, 1, 3);
    note(&r, sizeof r);
    r = __builtin_shufflevector(a, b, 1, 0);
    note(&r, sizeof r);
    /* In place, and fed back. */
    a = __builtin_shufflevector(a, b, 0, 2);
    b = __builtin_shufflevector(b, a, 1, 3);
    note(&a, sizeof a);
    note(&b, sizeof b);
}

/* Other lengths of result, which are not these. */
typedef uint32_t w4 __attribute__((vector_size(16)));
typedef uint32_t w2 __attribute__((vector_size(8)));
typedef uint32_t w8 __attribute__((vector_size(32)));
static NI void lengths(void)
{
    w4 a = { 1, 2, 3, 4 }, b = { 5, 6, 7, 8 };
    w2 h = __builtin_shufflevector(a, b, 0, 4);
    w2 g = __builtin_shufflevector(a, b, 3, 7);
    w8 l = __builtin_shufflevector(a, b, 0, 4, 1, 5, 2, 6, 3, 7);
    note(&h, sizeof h);
    note(&g, sizeof g);
    note(&l, sizeof l);
}

/* A short vector in each place a value can go. */
typedef uint8_t s8 __attribute__((vector_size(8)));
static NI s8 returned(s8 a, s8 b) { return __builtin_shufflevector(a, b, 0, 8, 2, 10, 4, 12, 6, 14); }
static NI s8 passed(s8 a) { return a + a; }
static s8 stored;
struct holder { int n; s8 v; };
static NI void places(void)
{
    s8 a = { 1, 2, 3, 4, 5, 6, 7, 8 }, b = { 11, 12, 13, 14, 15, 16, 17, 18 };
    s8 init = __builtin_shufflevector(a, b, 1, 9, 3, 11, 5, 13, 7, 15);
    s8 r = returned(a, b);
    s8 p = passed(__builtin_shufflevector(a, b, 0, 2, 4, 6, 8, 10, 12, 14));
    s8 q = __builtin_shufflevector(a, b, 0, 8, 1, 9, 2, 10, 3, 11) + __builtin_shufflevector(a, b, 7, 6, 5, 4, 3, 2, 1, 0);
    s8 list[2];
    struct holder h = { 3, __builtin_shufflevector(a, b, 4, 12, 5, 13, 6, 14, 7, 15) };
    list[1] = __builtin_shufflevector(a, b, 1, 3, 5, 7, 9, 11, 13, 15);
    list[0] = __builtin_shufflevector(a, a, 7, 6, 5, 4, 3, 2, 1, 0);
    stored = __builtin_shufflevector(a, b, 7, 15, 6, 14, 5, 13, 4, 12);
    note(&init, 8);
    note(&r, 8);
    note(&p, 8);
    note(&q, 8);
    note(list, 16);
    note(&h.v, 8);
    note(&stored, 8);
}

int main(void)
{
    test_uint8_16();
    test_uint8_8();
    test_uint16_8();
    test_uint16_4();
    test_uint32_4();
    test_uint32_2();
    test_uint64_2();
    test_int8_16();
    test_int16_8();
    test_int32_4();
    test_int64_2();
    test_float_4();
    test_double_2();
    lengths();
    places();
    return 0;
}
