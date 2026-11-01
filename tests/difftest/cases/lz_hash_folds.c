/* An LZ-style hash and match search: a little-endian byte pair loaded as a
 * halfword, a 32-bit multiply truncated before a shift, an unsigned bound
 * of 65535 tested by a shift, and a match loop whose counter starts at
 * zero (its first test is a test of the bound). Distances at and past the
 * bound, empty and full-length matches. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define N 70000
static uint8_t in[N];
static int32_t head[1 << 14];
static int32_t prev[N];

static uint32_t hash3(const uint8_t *p) {
    uint32_t v = (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16;
    return (v * 2654435761u) >> 18;
}

__attribute__((noinline)) static uint64_t search(size_t n, size_t limit) {
    memset(head, -1, sizeof head);
    uint64_t total = 0, far = 0;
    for (size_t i = 0; i + 3 <= n; i++) {
        uint32_t h = hash3(in + i);
        int32_t cand = head[h];
        prev[i] = cand;
        head[h] = (int32_t)i;
        size_t best = 0;
        for (int depth = 0; cand >= 0 && depth < 4; depth++) {
            if (i - (size_t)cand > 65535) {
                far++;
                break;
            }
            size_t max = n - i < limit ? n - i : limit, l = 0;
            while (l < max && in[cand + l] == in[i + l])
                l++;
            if (l > best)
                best = l;
            cand = prev[cand];
        }
        total += best;
    }
    return total * 1000003 + far;
}

__attribute__((noinline)) static uint32_t pairs(const uint8_t *p, size_t n) {
    uint32_t acc = 0;
    for (size_t i = 0; i + 1 < n; i += 3) {
        uint32_t v = (uint32_t)p[i] | (uint32_t)p[i + 1] << 8;
        acc = acc * 31 + v;
    }
    return acc;
}

int main(void) {
    uint32_t s = 12345;
    for (size_t i = 0; i < N; i++) {
        s = s * 1103515245u + 12345u;
        in[i] = (uint8_t)((i % 65536 < 300) ? "abcabcabd"[i % 9] : (s >> 16) % 4);
    }
    printf("%llu\n", (unsigned long long)search(N, 130));
    printf("%llu\n", (unsigned long long)search(N, 0));
    printf("%llu\n", (unsigned long long)search(1000, 7));
    printf("%u %u\n", pairs(in, N), pairs(in + 1, 2));
    return 0;
}
