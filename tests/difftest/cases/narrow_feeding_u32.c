/* 64-bit arithmetic whose only reader is a 32-bit operation, done at 32
 * bits instead: FNV and CRC steps over unsigned and signed bytes (a signed
 * byte sign-extends into the upper 24 bits), halfwords, sums that carry
 * and differences that borrow past bit 31, and results also read at 64
 * bits (kept wide). */
#include <stdint.h>
#include <stdio.h>

__attribute__((noinline)) static uint32_t fnv_u8(const uint8_t *s, int n) {
    uint32_t h = 2166136261u;
    for (int i = 0; i < n; i++)
        h = (h ^ s[i]) * 16777619u;
    return h;
}

__attribute__((noinline)) static uint32_t fnv_s8(const signed char *s, int n) {
    uint32_t h = 2166136261u;
    for (int i = 0; i < n; i++)
        h = (h ^ s[i]) * 16777619u;
    return h;
}

__attribute__((noinline)) static uint32_t mix16(const uint16_t *s, const int16_t *t, int n) {
    uint32_t h = 7;
    for (int i = 0; i < n; i++) {
        h = (h + s[i]) * 0x9e3779b1u;
        h = (h - t[i]) * 31u;
        h = (h | t[i]) * 3u ^ (h & s[i]);
    }
    return h;
}

__attribute__((noinline)) static uint32_t carry(uint32_t a, uint32_t b, uint8_t c) {
    /* a + b carries past bit 31; only the low 32 bits reach the product. */
    return (uint32_t)(((uint64_t)a + b + c) * 5u);
}

__attribute__((noinline)) static uint64_t shared(uint32_t h, const uint8_t *s, int n, uint64_t *wide) {
    uint64_t w = 0;
    for (int i = 0; i < n; i++) {
        uint64_t x = (uint64_t)h ^ s[i];  /* read at 64 bits below too */
        w += x << 7;
        h = (uint32_t)x * 16777619u;
    }
    *wide = w;
    return h;
}

__attribute__((noinline)) static uint32_t crc(const uint8_t *p, int n, const uint32_t *table) {
    uint32_t c = 0xffffffffu;
    for (int i = 0; i < n; i++)
        c = table[(c ^ p[i]) & 0xff] ^ (c >> 8);
    return ~c;
}

int main(void) {
    uint8_t bytes[64];
    signed char sbytes[64];
    uint16_t halves[64];
    int16_t shalves[64];
    for (int i = 0; i < 64; i++) {
        bytes[i] = (uint8_t)(i * 151 + 7);
        sbytes[i] = (signed char)(i * 97 - 50);
        halves[i] = (uint16_t)(i * 40503u + 11);
        shalves[i] = (int16_t)(i * 3001 - 30000);
    }
    uint32_t table[256];
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++)
            c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
        table[i] = c;
    }
    printf("fnv_u8 %08x %08x\n", fnv_u8(bytes, 64), fnv_u8(bytes, 3));
    printf("fnv_s8 %08x %08x\n", fnv_s8(sbytes, 64), fnv_s8(sbytes, 5));
    printf("mix16 %08x\n", mix16(halves, shalves, 64));
    printf("carry %08x %08x %08x\n", carry(0xffffffffu, 0xffffffffu, 255), carry(0x80000000u, 0x80000000u, 1), carry(1, 2, 3));
    uint64_t wide;
    uint64_t h = shared(0xdeadbeefu, bytes, 64, &wide);
    printf("shared %08llx %016llx\n", (unsigned long long)h, (unsigned long long)wide);
    printf("crc %08x\n", crc((const uint8_t *)"123456789", 9, table));
    return 0;
}
