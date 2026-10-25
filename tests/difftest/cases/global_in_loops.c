/* File-scope variables updated in loops, and lookups that return through
 * an out parameter.
 *
 * `draw`: a generator whose state is a static updated on every call; the
 * loops calling it (inlined) keep the state in a register and store it on
 * every way out: falling out, `break`, `goto`, an exit from an inner loop
 * of a nest, and a `switch` inside the loop. Loops that also make a real
 * call, return from inside, or use a variable whose address escapes (or
 * that is read through another type) keep it in memory.
 *
 * `find`: an inlined lookup writing its result through `&v` only on a hit,
 * with `v` declared in the loop body; the found flag and `v` merge before
 * `if (find(k, &v) && v == ...)`. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint64_t state;
static uint32_t counter;
static int16_t small;
static double acc;
static int escaped;
static int *escaped_ptr = &escaped;
static uint32_t punned;

static uint64_t draw(void) {
    uint64_t z = (state += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

__attribute__((noinline)) static void touch(void) { state ^= 0x5555; counter += 1000; }

__attribute__((noinline)) static uint64_t plain_loop(int n) {
    uint64_t s = 0;
    for (int i = 0; i < n; i++)
        s += draw() & 0xff;
    return s;
}

__attribute__((noinline)) static uint64_t break_out(int n, uint64_t stop) {
    uint64_t s = 0;
    for (int i = 0; i < n; i++) {
        uint64_t x = draw();
        counter++;
        if ((x & 0xf) == stop)
            break;
        s += x >> 60;
    }
    return s + counter;
}

__attribute__((noinline)) static int goto_out(int n) {
    int i;
    for (i = 0; i < n; i++) {
        small = (int16_t)(small * 3 + 1);
        if (small < -1000)
            goto out;
    }
    return -1;
out:
    return i;
}

__attribute__((noinline)) static uint64_t nest(int n, int m) {
    uint64_t s = 0;
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < m; j++) {
            uint64_t x = draw();
            if ((x & 0x3f) == 0)
                return s + 1000000;
            s += x & 3;
        }
        counter += 2;
    }
    return s;
}

__attribute__((noinline)) static double with_switch(int n) {
    for (int i = 0; i < n; i++) {
        switch (i % 4) {
        case 0: acc += 1.5; break;
        case 1: acc *= 0.75; break;
        case 2: continue;
        default: acc -= i; break;
        }
        counter ^= (uint32_t)i;
    }
    return acc;
}

__attribute__((noinline)) static uint64_t with_call(int n) {
    uint64_t s = 0;
    for (int i = 0; i < n; i++) {
        s += draw() & 7;
        if (i % 5 == 0)
            touch();
    }
    return s + state;
}

__attribute__((noinline)) static int return_inside(int n) {
    for (int i = 0; i < n; i++) {
        counter += 3;
        if (counter % 7 == 0)
            return i;
    }
    return -1;
}

__attribute__((noinline)) static int through_pointer(int n) {
    for (int i = 0; i < n; i++) {
        escaped += 2;
        *escaped_ptr += 1;
    }
    return escaped;
}

__attribute__((noinline)) static uint32_t pun(int n) {
    for (int i = 0; i < n; i++) {
        punned += 0x10001;
        ((uint16_t *)&punned)[0] ^= 0xff;
    }
    return punned;
}

/* Reads only: the loop's loads become one before it. */
__attribute__((noinline)) static uint64_t read_only(int n) {
    uint64_t s = 0;
    for (int i = 0; i < n; i++)
        s += state >> (i & 31);
    return s;
}

#define SLOTS 64
static uint64_t keys[SLOTS];
static uint32_t vals[SLOTS];

static int find(uint64_t key, uint32_t *val) {
    uint32_t i = (uint32_t)(key * 0x9e3779b97f4a7c15ull >> 58);
    while (keys[i]) {
        if (keys[i] == key) {
            *val = vals[i];
            return 1;
        }
        i = (i + 1) & (SLOTS - 1);
    }
    return 0;
}

__attribute__((noinline)) static uint64_t lookups(int n) {
    uint64_t hits = 0, sum = 0, odd = 0;
    for (int i = 0; i < n; i++) {
        uint32_t v;
        if (find((uint64_t)i * 7 + 1, &v) && v == (uint32_t)i) {
            hits++;
            sum += v;
        }
        uint32_t w;
        if (!find((uint64_t)i * 7 + 2, &w))
            odd++;
        else
            sum += w * 3;
    }
    return hits * 1000000 + sum * 10 + odd;
}

/* Print a call's result, then the globals it may have changed (evaluated
 * after the call, unlike printf's arguments). */
static void show(const char *what, long long r) {
    printf("%s %lld state=%llu counter=%u small=%d acc=%g\n", what, r, (unsigned long long)state, counter, small, acc);
}

int main(void) {
    state = 1;
    show("plain", (long long)plain_loop(100));
    show("break", (long long)break_out(1000, 3));
    show("break", (long long)break_out(0, 3));
    small = 1;
    show("goto", goto_out(100));
    show("goto", goto_out(2));
    show("nest", (long long)nest(50, 10));
    show("nest", (long long)nest(3, 4));
    acc = 2;
    show("switch", (long long)(with_switch(10) * 1024));
    show("call", (long long)with_call(23));
    show("return", return_inside(10));
    show("return", return_inside(0));
    printf("pointer %d\n", through_pointer(5));
    printf("pun %u\n", pun(5));
    printf("read %llu\n", (unsigned long long)read_only(40));

    memset(keys, 0, sizeof keys);
    for (uint32_t i = 0; i < 40; i++) {
        uint64_t key = (uint64_t)i * 7 + 1 + (i % 3 == 0);
        uint32_t s = (uint32_t)(key * 0x9e3779b97f4a7c15ull >> 58);
        while (keys[s])
            s = (s + 1) & (SLOTS - 1);
        keys[s] = key;
        vals[s] = i % 5 == 0 ? i + 1 : i;
    }
    printf("lookups %llu\n", (unsigned long long)lookups(50));
    return 0;
}
