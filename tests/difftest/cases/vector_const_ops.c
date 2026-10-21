/* Vector shifts by constants (immediate forms, counts 0 to lane bits - 1,
 * signed and unsigned lanes), constant vector and array literals inside
 * loops (moved out of the loop when nothing else writes them, kept when
 * something does or their address escapes), and operands loaded from the
 * same vector by consecutive operations, with and without a store to it in
 * between. */
#include <stdio.h>
#include <string.h>

typedef signed char s8x8 __attribute__((vector_size(8)));
typedef unsigned char u8x8 __attribute__((vector_size(8)));
typedef short s16x8 __attribute__((vector_size(16)));
typedef unsigned short u16x4 __attribute__((vector_size(8)));
typedef int s32x4 __attribute__((vector_size(16)));
typedef unsigned u32x4 __attribute__((vector_size(16)));
typedef long long s64x2 __attribute__((vector_size(16)));
typedef unsigned long long u64x2 __attribute__((vector_size(16)));
typedef double f64x2 __attribute__((vector_size(16)));

static unsigned long long h = 1469598103934665603ULL;

static void mix(const void *p, size_t n) {
    const unsigned char *b = p;
    for (size_t i = 0; i < n; i++)
        h = (h ^ b[i]) * 1099511628211ULL;
}

__attribute__((noinline)) static void shifts(void) {
    s8x8 a8 = { -128, -77, -1, 0, 1, 63, 100, 127 };
    u8x8 b8 = { 0, 1, 2, 77, 128, 200, 254, 255 };
    s16x8 a16 = { -32768, -12345, -1, 0, 1, 999, 20000, 32767 };
    u16x4 b16 = { 0, 1, 40000, 65535 };
    s32x4 a32 = { -2147483647 - 1, -5, 7, 2147483647 };
    u32x4 b32 = { 0, 1, 0x80000000u, 0xffffffffu };
    s64x2 a64 = { -9000000000000000000LL, 12345678901234LL };
    u64x2 b64 = { 0x8000000000000001ULL, 0xffffffffffffffffULL };

    s8x8 r8[6] = { a8 >> 0, a8 >> 1, a8 >> 7, a8 << 0, a8 << 3, a8 << 7 };
    u8x8 q8[4] = { b8 >> 1, b8 >> 7, b8 << 1, b8 << 7 };
    s16x8 r16[3] = { a16 >> 15, a16 >> 4, a16 << 15 };
    u16x4 q16[3] = { b16 >> 15, b16 >> 3, b16 << 9 };
    s32x4 r32[3] = { a32 >> 31, a32 >> 13, a32 << 31 };
    u32x4 q32[3] = { b32 >> 31, b32 >> 1, b32 << 17 };
    s64x2 r64[3] = { a64 >> 63, a64 >> 40, a64 << 63 };
    u64x2 q64[3] = { b64 >> 63, b64 >> 1, b64 << 32 };
    mix(r8, sizeof r8); mix(q8, sizeof q8); mix(r16, sizeof r16); mix(q16, sizeof q16);
    mix(r32, sizeof r32); mix(q32, sizeof q32); mix(r64, sizeof r64); mix(q64, sizeof q64);
    printf("shift r8 %d %d %d q32 %u %u r64 %lld\n", r8[1][0], r8[2][1], r8[5][1],
           q32[1][2], q32[2][3], (long long)r64[1][0]);
}

static f64x2 dv[64];
static u32x4 uv[64];
static int tab_out[64];

__attribute__((noinline)) static void sink(const void *p) { mix(p, 16); }

__attribute__((noinline)) static void literals(int n) {
    for (int i = 0; i < n; i++) {
        /* Only constant stores: built once before the loop. */
        dv[i] = dv[i] * (f64x2){ 0.5, -1.5 } + (f64x2){ 1.0, 2.0 };
        uv[i] = (uv[i] << 3) + (u32x4){ 1, 2, 3, 4 };
        int t[4] = { 7, 11, 13, 17 };
        tab_out[i] = t[i & 3] * i;
    }
    for (int i = 0; i < n; i++) {
        /* Written again in the loop: must be rebuilt every iteration. */
        f64x2 c = { 0.25, 4.0 };
        if (i & 1)
            c[0] = i;
        dv[i] = dv[i] + c;
        int t[4] = { 1, 2, 3, 4 };
        t[i & 3] = -i;
        tab_out[i] += t[0] + t[1] + t[2] + t[3];
        /* Address escapes to a call. */
        u32x4 e = { 9, 8, 7, 6 };
        sink(&e);
        uv[i] ^= e;
    }
}

/* Two operations reading the same vector, with a store to it in between. */
__attribute__((noinline)) static void reloads(u32x4 *p, int n) {
    for (int i = 0; i < n; i++) {
        u32x4 a = p[i] >> 2;
        p[i] = a;
        u32x4 b = p[i] + p[i];
        u32x4 c = p[i] >> 1;
        p[i] = b ^ c ^ (p[i] << 4);
    }
}

int main(int argc, char **argv) {
    (void)argv;
    shifts();
    for (int i = 0; i < 64; i++) {
        dv[i] = (f64x2){ i * 0.75, 3.0 - i };
        uv[i] = (u32x4){ i * 2654435761u, (unsigned)i, ~(unsigned)i, 0x12345678u + i };
    }
    literals(40 + argc);
    reloads(uv, 64);
    mix(dv, sizeof dv);
    mix(uv, sizeof uv);
    mix(tab_out, sizeof tab_out);
    printf("dv %g %g uv %u %u tab %d %d\n", dv[3][0], dv[40][1], uv[5][1], uv[63][3], tab_out[2], tab_out[39]);
    printf("hash %016llx\n", h);
    return 0;
}
