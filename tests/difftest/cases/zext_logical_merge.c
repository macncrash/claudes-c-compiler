/* Unsigned values carried around loops through 64-bit logical operations
 * and right shifts of their widening: a table CRC-32 (the table entry xor
 * c >> 8), a hash mixing in an or of two bytes and a mask, and merges
 * where an int's sign or a negative long gets in, whose high bits must
 * not be taken as zero. */
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

static uint32_t table[256];
static uint8_t buf[300];

static uint32_t __attribute__((noinline)) crc(const uint8_t *p, size_t n) {
    uint32_t c = 0xffffffffu;
    for (size_t i = 0; i < n; i++)
        c = table[(c ^ p[i]) & 0xff] ^ (c >> 8);
    return ~c;
}

static uint32_t __attribute__((noinline)) mix(const uint8_t *p, size_t n) {
    uint32_t h = 7;
    for (size_t i = 0; i + 1 < n; i++) {
        uint64_t w = (uint64_t)((p[i] | (uint32_t)p[i + 1] << 8) & 0x7fff);
        h = (uint32_t)((h >> 3) ^ w ^ ((uint64_t)h & 0xf0f0));
        h = h * 33 + p[i];
    }
    return h;
}

static uint64_t __attribute__((noinline)) signed_in(const int *v, int n) {
    uint32_t c = 1;
    uint64_t s = 0;
    for (int i = 0; i < n; i++) {
        c = (uint32_t)((uint64_t)c >> 1 ^ (uint64_t)(long)v[i]);
        s += (uint64_t)c;
    }
    return s + c;
}

static uint64_t __attribute__((noinline)) negative_in(int n, long k) {
    uint32_t c = 5;
    uint64_t s = 0;
    for (int i = 0; i < n; i++) {
        c = (uint32_t)(((uint64_t)c >> 2) | (uint64_t)k);
        s = s * 3 + c;
        k += 0x10001;
    }
    return s;
}

int main(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++)
            c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
        table[i] = c;
    }
    uint32_t r = 12345;
    for (int i = 0; i < 300; i++) {
        r = r * 1103515245u + 12345u;
        buf[i] = (uint8_t)(r >> 16);
    }
    int v[6] = { 3, -1, 70000, -70000, 0x7fffffff, -2 };
    printf("crc %08x %08x\n", crc((const uint8_t *)"123456789", 9), crc(buf, 300));
    printf("mix %u %u\n", mix(buf, 300), mix(buf + 7, 20));
    printf("signed %llu\n", (unsigned long long)signed_in(v, 6));
    printf("negative %llu %llu\n", (unsigned long long)negative_in(5, -3), (unsigned long long)negative_in(4, 0x123456789));
    return 0;
}
