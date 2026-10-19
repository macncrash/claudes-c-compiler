/* Integer semantics: promotions, overflow wraparound on unsigned, shifts,
 * division of negatives, 128-bit integers, bitfields. */
#include <stdint.h>
#include <stdio.h>

struct bits { unsigned a : 3; signed b : 5; unsigned c : 24; };

int main(void) {
    unsigned char uc = 250; signed char sc = -6;
    printf("%d %d %u\n", uc + 10, sc * 3, (unsigned)(uc + sc));
    unsigned u = 0; u--; printf("%u %x\n", u, u >> 4);
    int neg = -17; printf("%d %d %d %d\n", neg / 5, neg % 5, neg >> 2, (int)((unsigned)neg >> 28));
    long long big = 0x7fffffffffffffffLL; printf("%lld %llu\n", big, (unsigned long long)big + 1);
    __int128 w = (__int128)1 << 100; w += 12345;
    printf("%llx %llx\n", (unsigned long long)(w >> 64), (unsigned long long)w);
    unsigned __int128 m = (unsigned __int128)0xffffffffffffffffULL * 0xffffffffffffffffULL;
    printf("%llx %llx\n", (unsigned long long)(m >> 64), (unsigned long long)m);
    struct bits b = {5, -3, 0xabcdef};
    b.a += 4; b.b -= 20;
    printf("%u %d %x %zu\n", b.a, b.b, b.c, sizeof b);
    uint16_t x = 0xffff; x += 2; printf("%u\n", x);
    int64_t p = INT64_MIN; printf("%lld\n", (long long)(p / -1 == p ? 0 : 1));
    printf("%d\n", __builtin_popcount(0xf0f0) + __builtin_clz(1) + __builtin_ctzll(1ULL << 40));
    return 0;
}
