/* More values live across calls than there are callee-saved registers, so
 * the allocator uses every one of them (on AArch64 x19-x28) and spills the
 * rest; and the same with inline assembly that clobbers some of those
 * registers, which must then hold nothing across it. */
#include <stdint.h>
#include <stdio.h>

__attribute__((noinline)) static uint64_t mix(uint64_t x) {
    return x * 0x9e3779b97f4a7c15ull ^ (x >> 29);
}

__attribute__((noinline)) static uint64_t twelve(uint64_t seed) {
    uint64_t a = mix(seed), b = mix(a), c = mix(b), d = mix(c), e = mix(d), f = mix(e);
    uint64_t g = mix(f), h = mix(g), i = mix(h), j = mix(i), k = mix(j), l = mix(k);
    for (int r = 0; r < 3; r++) {
        a += mix(l); b ^= mix(a); c += mix(b); d ^= mix(c); e += mix(d); f ^= mix(e);
        g += mix(f); h ^= mix(g); i += mix(h); j ^= mix(i); k += mix(j); l ^= mix(k);
    }
    return a ^ b ^ c ^ d ^ e ^ f ^ g ^ h ^ i ^ j ^ k ^ l;
}

__attribute__((noinline)) static uint64_t clobbered(uint64_t seed) {
    uint64_t a = mix(seed), b = mix(a), c = mix(b), d = mix(c), e = mix(d), f = mix(e);
    uint64_t g = mix(f), h = mix(g), i = mix(h), j = mix(i), k = mix(j);
#if defined(__aarch64__)
    __asm__ volatile("mov x19, #1\n\tmov x20, #2\n\tmov x27, #3\n\tmov x28, #4" ::: "x19", "x20", "x27", "x28");
#elif defined(__x86_64__)
    __asm__ volatile("mov $1, %%rbx\n\tmov $2, %%r12\n\tmov $3, %%r15" ::: "rbx", "r12", "r15");
#endif
    a += mix(k);
    return a ^ b ^ c ^ d ^ e ^ f ^ g ^ h ^ i ^ j ^ k;
}

int main(void) {
    for (uint64_t s = 1; s < 4; s++)
        printf("%llu %llu\n", (unsigned long long)twelve(s), (unsigned long long)clobbered(s));
    return 0;
}
