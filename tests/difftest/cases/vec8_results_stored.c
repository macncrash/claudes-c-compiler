/* 8-byte vector results copied out of their temporaries: stored to arrays,
 * struct fields and locals, kept and reused, read lane by lane, passed to
 * and returned from a function, and stored through a pointer the loop
 * steps. Each copy stays a vector copy; the values must come out the same. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

typedef uint8_t u8x8 __attribute__((vector_size(8)));
typedef int16_t i16x4 __attribute__((vector_size(8)));
typedef uint32_t u32x2 __attribute__((vector_size(8)));
typedef float f32x2 __attribute__((vector_size(8)));

#define N 64

static u8x8 pa[N], pb[N], pout[N];
static i16x4 ha[N], hout[N];
static u32x2 wa[N], wout[N];
static f32x2 fa[N], fout[N];
struct pair { u8x8 lo; i16x4 hi; int tag; } pairs[N];

static void blend(void) {
    for (int i = 0; i < N; i++)
        pout[i] = (pa[i] >> 1) + (pa[i] >> 2) + (pb[i] >> 2);
}

static void halves(void) {
    for (int i = 0; i < N; i++) {
        i16x4 t = ha[i] * 3 - (ha[i] >> 1);
        hout[i] = t;
        pairs[i].hi = t + 1;
        pairs[i].lo = pa[i] ^ pb[i];
        pairs[i].tag = t[2];
    }
}

static void words(void) {
    u32x2 acc = { 1, 2 };
    for (int i = 0; i < N; i++) {
        acc = acc * 5u + wa[i];
        wout[i] = acc;
    }
}

static void floats(f32x2 *out) {
    for (int i = 0; i < N; i++)
        *out++ = fa[i] * (f32x2){ 0.5f, 2.0f } + 1.0f;
}

__attribute__((noinline)) static u8x8 mix(u8x8 a, u8x8 b) {
    return (a & b) + (a >> 3);
}

static uint64_t hash(const void *p, size_t n) {
    const unsigned char *c = p;
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; i++)
        h = (h ^ c[i]) * 1099511628211ull;
    return h;
}

int main(void) {
    for (int i = 0; i < N; i++) {
        for (int k = 0; k < 8; k++) {
            pa[i][k] = (uint8_t)(i * 37 + k * 11 + 200);
            pb[i][k] = (uint8_t)(i * 13 - k * 7);
        }
        for (int k = 0; k < 4; k++)
            ha[i][k] = (int16_t)(i * 1000 - k * 3333);
        wa[i] = (u32x2){ (uint32_t)i * 2654435761u, ~(uint32_t)i };
        fa[i] = (f32x2){ (float)i / 4, (float)(N - i) };
    }
    blend();
    halves();
    words();
    floats(fout);
    u8x8 m = { 0 };
    for (int i = 0; i < N; i++)
        m = mix(m + pout[i], pairs[i].lo);
    u8x8 keep = pout[N - 1];
    pout[0] = keep;
    printf("pout %016llx\n", (unsigned long long)hash(pout, sizeof pout));
    printf("hout %016llx\n", (unsigned long long)hash(hout, sizeof hout));
    printf("pairs %016llx\n", (unsigned long long)hash(pairs, sizeof pairs));
    printf("wout %016llx\n", (unsigned long long)hash(wout, sizeof wout));
    printf("fout %016llx\n", (unsigned long long)hash(fout, sizeof fout));
    printf("m %u %u %u %u %u %u %u %u\n", m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7]);
    printf("lanes %d %d %u %.3f\n", hout[5][1], pairs[7].tag, wout[N - 1][1], fout[3][0]);
    return 0;
}
