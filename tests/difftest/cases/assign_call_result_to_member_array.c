/* An assignment whose right side calls a function storing into an array
 * reached through a structure, then reading the element back, in the shape
 * of SQLite's computeCellSize. The call may replace the array: the
 * element's address is taken after the call, as clang does. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct cells {
    unsigned short *size;
    int n;
    unsigned short (*measure)(struct cells *, int);
    unsigned short *spare;
};

static unsigned short measure_plain(struct cells *p, int i)
{
    return (unsigned short)(i * 37 + p->n);
}

/* Moves the size array to the spare buffer before answering. */
static unsigned short measure_moving(struct cells *p, int i)
{
    if (p->spare) {
        memcpy(p->spare, p->size, sizeof(unsigned short) * p->n);
        p->size = p->spare;
        p->spare = 0;
    }
    return (unsigned short)(0xfff0 + i);
}

static unsigned short __attribute__((noinline)) compute(struct cells *p, int i)
{
    p->size[i] = p->measure(p, i);
    return p->size[i];
}

static int calls;
static int __attribute__((noinline)) bump(int v)
{
    calls++;
    return v * 3;
}

static int __attribute__((noinline)) fill(int *a, const int *idx, int n)
{
    int s = 0;
    for (int i = 0; i < n; i++) {
        a[idx[i]] = bump(i) + a[idx[i]];
        s += a[idx[i]];
    }
    return s;
}

int main(void)
{
    unsigned short a[8] = {0}, b[8] = {0};
    struct cells c = {a, 8, measure_plain, 0};
    unsigned sum = 0;
    for (int i = 0; i < 8; i++)
        sum = sum * 31 + compute(&c, i);
    printf("%u %u %u\n", sum, a[3], a[7]);

    c.measure = measure_moving;
    c.spare = b;
    unsigned short r = compute(&c, 5);
    printf("%u %u %u %d\n", r, a[5], b[5], c.size == b);

    int arr[6] = {1, 2, 3, 4, 5, 6};
    int idx[6] = {5, 0, 3, 3, 1, 5};
    printf("%d %d %d %d\n", fill(arr, idx, 6), arr[5], arr[3], calls);
    return 0;
}
