/* The ACLE scalar intrinsics of <arm_acle.h>: CRC32 and CRC32C, clz/cls,
 * rev/rev16/revsh, rbit, ror, barriers, hints and a system register read.
 * ccc defined no __ARM_ACLE, so clang's arm_acle.h (on the include path)
 * stopped with #error, and zlib-ng, crc32c and the like lost their ARM
 * paths. ccc now has its own arm_acle.h and defines __ARM_ACLE and, on
 * Apple arm64, __ARM_FEATURE_CRC32. Edge values, then a pseudo-random sweep
 * hashed so the output stays short. */
#include <stdio.h>
#include <stdint.h>
#if defined(__aarch64__) && defined(__ARM_ACLE)
#include <arm_acle.h>
#endif

static uint64_t state = 0x9e3779b97f4a7c15ULL;
static uint64_t next(void) {
    state ^= state << 13; state ^= state >> 7; state ^= state << 17;
    return state;
}

static uint64_t mix(uint64_t h, uint64_t v) { return (h ^ v) * 0x100000001b3ULL; }

/* The reference CRC: bit at a time, reflected. */
static uint32_t crc_ref(uint32_t crc, uint64_t data, int bytes, uint32_t poly) {
    for (int i = 0; i < bytes; i++) {
        crc ^= (uint8_t)(data >> (8 * i));
        for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (poly & -(crc & 1));
    }
    return crc;
}

/* Never called: barriers and hints only have to compile and assemble. */
void barriers_and_hints(void);
void barriers_and_hints(void) {
#if defined(__aarch64__) && defined(__ARM_ACLE)
    __dmb(15); __dmb(11); __dmb(10); __dmb(9);
    __dsb(15); __dsb(11); __isb(15);
    __wfi(); __wfe(); __sev(); __sevl();
#endif
}

int main(void) {
#if defined(__aarch64__) && defined(__ARM_ACLE)
    printf("acle %d crc32 %d\n", __ARM_ACLE,
#ifdef __ARM_FEATURE_CRC32
           1
#else
           0
#endif
    );
    static const uint64_t edge[] = { 0, 1, 2, 0x7f, 0x80, 0xff, 0x8000, 0xffff, 0x7fffffff, 0x80000000,
                                     0xffffffff, 0x100000000ULL, 0x7fffffffffffffffULL, 0x8000000000000000ULL,
                                     0xffffffffffffffffULL, 0x0123456789abcdefULL, 0xfedcba9876543210ULL };
    for (unsigned i = 0; i < sizeof edge / sizeof edge[0]; i++) {
        uint64_t v = edge[i];
        uint32_t w = (uint32_t)v;
        printf("%016llx: clz %u %u %lu cls %u %u %lu rev %08x %016llx %016lx rev16 %08x %016llx revsh %d"
               " rbit %08x %016llx %016lx\n",
               (unsigned long long)v, __clz(w), __clzll(v), (unsigned long)__clzl(v),
               __cls(w), __clsll(v), (unsigned long)__clsl(v),
               __rev(w), (unsigned long long)__revll(v), __revl(v),
               __rev16(w), (unsigned long long)__rev16ll(v), __revsh((int16_t)w),
               __rbit(w), (unsigned long long)__rbitll(v), __rbitl(v));
        printf("  ror %08x %08x %08x %016llx %016llx %016lx crc %08x %08x %08x %08x %08x %08x %08x %08x\n",
               __ror(w, 0), __ror(w, 5), __ror(w, 37), (unsigned long long)__rorll(v, 0),
               (unsigned long long)__rorll(v, 63), __rorl(v, 70),
               __crc32b(w, (uint8_t)v), __crc32h(~w, (uint16_t)v), __crc32w(w, w), __crc32d(~w, v),
               __crc32cb(w, (uint8_t)v), __crc32ch(~w, (uint16_t)v), __crc32cw(w, w), __crc32cd(~w, v));
    }
    uint64_t h = 0;
    int bad = 0;
    for (int i = 0; i < 20000; i++) {
        uint64_t v = next(), s = next();
        uint32_t w = (uint32_t)v, c = (uint32_t)s, y = (uint32_t)(s >> 40);
        h = mix(h, __clz(w)); h = mix(h, __clzll(v >> (s & 63))); h = mix(h, __cls(w >> (y & 31)));
        h = mix(h, __clsll((int64_t)v >> (s & 63))); h = mix(h, __rev(w)); h = mix(h, __rev16ll(v));
        h = mix(h, (uint16_t)__revsh((int16_t)v)); h = mix(h, __rbit(w)); h = mix(h, __rbitll(v));
        h = mix(h, __ror(w, y)); h = mix(h, __rorll(v, y));
        uint32_t r[8] = { __crc32b(c, (uint8_t)v), __crc32h(c, (uint16_t)v), __crc32w(c, w), __crc32d(c, v),
                          __crc32cb(c, (uint8_t)v), __crc32ch(c, (uint16_t)v), __crc32cw(c, w), __crc32cd(c, v) };
        static const int bytes[4] = { 1, 2, 4, 8 };
        for (int k = 0; k < 8; k++) {
            uint32_t want = crc_ref(c, v, bytes[k % 4], k < 4 ? 0xEDB88320u : 0x82F63B78u);
            if (r[k] != want && bad++ < 5) printf("crc %d of %08x %016llx: %08x want %08x\n", k, c, (unsigned long long)v, r[k], want);
            h = mix(h, r[k]);
        }
    }
    /* A whole buffer, as zlib-ng's crc32 does: 8 bytes at a time, then the tail. */
    unsigned char buf[1003];
    for (unsigned i = 0; i < sizeof buf; i++) buf[i] = (unsigned char)next();
    uint32_t crc = 0xffffffff, crcc = 0xffffffff;
    unsigned i = 0;
    for (; i + 8 <= sizeof buf; i += 8) {
        uint64_t q; __builtin_memcpy(&q, buf + i, 8);
        crc = __crc32d(crc, q); crcc = __crc32cd(crcc, q);
    }
    for (; i < sizeof buf; i++) { crc = __crc32b(crc, buf[i]); crcc = __crc32cb(crcc, buf[i]); }
    uint64_t freq = __arm_rsr64("cntfrq_el0");
    uint64_t t0 = __arm_rsr64("cntvct_el0"), t1 = __arm_rsr64("cntvct_el0");
    __nop(); __yield();
    printf("sweep %016llx bad %d buffer %08x %08x freq %d clock %d\n", (unsigned long long)h, bad,
           ~crc, ~crcc, freq != 0, t1 >= t0);
#else
    printf("no acle\n");
#endif
    return 0;
}
