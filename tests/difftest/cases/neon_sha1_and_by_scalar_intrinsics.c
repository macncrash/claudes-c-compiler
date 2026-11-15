/* NEON intrinsics ccc lacked: the SHA-1 ones (vsha1cq/pq/mq_u32, vsha1h_u32,
 * vsha1su0q/su1q_u32) and the by-scalar multiply family (vmul[q]_n, vmla[q]_n,
 * vmls[q]_n for 16- and 32-bit integers and float); calls to them were left as
 * undefined functions. With them in, ccc also predefines __ARM_FEATURE_CRYPTO,
 * __ARM_FEATURE_AES, __ARM_FEATURE_SHA2, __ARM_NEON__ and __ARM_NEON_FP as clang
 * does on Apple arm64 (aws-lc refuses to build without them). Found building
 * aws-lc. Random inputs, outputs hashed. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <arm_neon.h>

static uint32_t s = 2463534242u;
static uint32_t nx(void) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
static uint64_t H = 1469598103934665603ULL;
static void hb(const void *p, size_t n) { const unsigned char *c = p; while (n--) H = (H ^ *c++) * 1099511628211ULL; }
static uint32x4_t rv(void) { uint32_t a[4] = { nx(), nx(), nx(), nx() }; return vld1q_u32(a); }

#define RV(V, v) do { unsigned char b_[sizeof(V)]; for (size_t i_ = 0; i_ < sizeof b_; i_++) b_[i_] = (unsigned char)nx(); memcpy(&v, b_, sizeof v); } while (0)
#define BY_SCALAR(V, S, SUF) { V a, b; RV(V, a); RV(V, b); S k = (S)nx(); \
    V r1 = vmul##SUF(a, k), r2 = vmla##SUF(a, b, k), r3 = vmls##SUF(a, b, k); hb(&r1, sizeof r1); hb(&r2, sizeof r2); hb(&r3, sizeof r3); }
#define BY_SCALAR_F(V, SUF) { V a, b; float x[4] = { (float)(nx() % 1000) / 7, (float)(nx() % 1000) / 9, 1.5f, -2.25f }; \
    memcpy(&a, x, sizeof a); memcpy(&b, x + (sizeof a == 8 ? 2 : 0), sizeof b); float32_t k = (float32_t)(nx() % 100) / 3; \
    V r1 = vmul##SUF(a, k), r2 = vmla##SUF(a, b, k), r3 = vmls##SUF(a, b, k); hb(&r1, sizeof r1); hb(&r2, sizeof r2); hb(&r3, sizeof r3); }

int main(void) {
#if defined(__ARM_FEATURE_CRYPTO) && defined(__ARM_FEATURE_AES) && defined(__ARM_FEATURE_SHA2) && defined(__ARM_NEON__)
    printf("features %d %d\n", __ARM_FEATURE_CRYPTO, __ARM_NEON_FP);
#else
    printf("features missing\n");
#endif
    for (int i = 0; i < 1000; i++) {
        uint32x4_t a = rv(), b = rv(), c = rv();
        uint32_t e = nx();
        uint32x4_t r[5] = { vsha1cq_u32(a, e, b), vsha1pq_u32(a, e, b), vsha1mq_u32(a, e, b),
                            vsha1su0q_u32(a, b, c), vsha1su1q_u32(a, b) };
        hb(r, sizeof r);
        uint32_t h = vsha1h_u32(e);
        hb(&h, sizeof h);
    }
    printf("sha1 %016llx\n", (unsigned long long)H);
    for (int i = 0; i < 300; i++) {
        BY_SCALAR(uint16x4_t, uint16_t, _n_u16) BY_SCALAR(int16x4_t, int16_t, _n_s16)
        BY_SCALAR(uint32x2_t, uint32_t, _n_u32) BY_SCALAR(int32x2_t, int32_t, _n_s32)
        BY_SCALAR(uint16x8_t, uint16_t, q_n_u16) BY_SCALAR(int16x8_t, int16_t, q_n_s16)
        BY_SCALAR(uint32x4_t, uint32_t, q_n_u32) BY_SCALAR(int32x4_t, int32_t, q_n_s32)
        BY_SCALAR_F(float32x2_t, _n_f32) BY_SCALAR_F(float32x4_t, q_n_f32)
    }
    printf("by-scalar %016llx\n", (unsigned long long)H);
    return 0;
}
