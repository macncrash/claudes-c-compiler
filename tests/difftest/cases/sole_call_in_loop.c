/* Static functions called once, from a loop, inlined whatever their size
 * (up to a limit): an open-addressing delete with early returns and a
 * shifting loop, a function with a static local counter, and functions
 * the rule leaves to the others: one whose address is taken, one called
 * twice, and a recursive one. */
#include <stdio.h>
#include <stdint.h>

#define SIZE 1024
#define MASK (SIZE - 1)
static uint64_t keys[SIZE];
static uint32_t vals[SIZE];

static uint32_t slot(uint64_t k) { return (uint32_t)((k * 0x9E3779B97F4A7C15ull) >> 54); }

static void put(uint64_t k, uint32_t v) {
    uint32_t i = slot(k);
    while (keys[i] && keys[i] != k)
        i = (i + 1) & MASK;
    keys[i] = k;
    vals[i] = v;
}

static int del(uint64_t k) {
    uint32_t i = slot(k);
    while (keys[i] != k) {
        if (!keys[i])
            return 0;
        i = (i + 1) & MASK;
    }
    for (uint32_t j = i;;) {
        j = (j + 1) & MASK;
        if (!keys[j])
            break;
        uint32_t home = slot(keys[j]);
        if (((j - home) & MASK) >= ((j - i) & MASK)) {
            keys[i] = keys[j];
            vals[i] = vals[j];
            i = j;
        }
    }
    keys[i] = 0;
    return 1;
}

static int del_taken(uint64_t k) { return (int)(slot(k) & 1) + (k == 3); }
static int (*const del_ptr)(uint64_t) = del_taken;

static uint32_t tick(void) {
    static uint32_t n;
    n = n * 5 + 1;
    if (n & 0x100)
        n ^= 0x55;
    return n;
}

static uint32_t twice(uint32_t x) { uint32_t s = 0; for (uint32_t i = 0; i < x % 7; i++) s += i * x; return s; }

static uint32_t rec(uint32_t x) { return x < 2 ? x : rec(x / 2) + rec(x / 3); }

static uint64_t sum(void) {
    uint64_t s = 0;
    for (int i = 0; i < SIZE; i++)
        s = s * 31 + keys[i] + vals[i];
    return s;
}

int main(void) {
    for (uint32_t i = 1; i <= 700; i++)
        put(i * 7919ull, i);
    int deleted = 0;
    for (uint32_t i = 1; i <= 900; i += 2)
        deleted += del(i * 7919ull);
    deleted += del_ptr(3) + del_ptr(7919 * 2 + 1);
    uint32_t t = 0;
    for (int i = 0; i < 50; i++)
        t += tick();
    uint32_t w = twice(t) + twice(deleted);
    uint32_t r = 0;
    for (uint32_t i = 0; i < 40; i++)
        r += rec(i * 13);
    printf("deleted %d sum %llu tick %u twice %u rec %u\n", deleted, (unsigned long long)sum(), t, w, r);
    return 0;
}
