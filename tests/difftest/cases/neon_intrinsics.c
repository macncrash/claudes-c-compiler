/* Every intrinsic of ccc's arm_neon.h, on deterministic data. clang runs the
 * same calls through its own header, so each result is compared with the
 * real instruction's. Generated. */
#include <arm_neon.h>
#include <stdio.h>
#include <string.h>

static unsigned char pool[256] __attribute__((aligned(16)));
static unsigned char fpool[256] __attribute__((aligned(16)));
static unsigned char out[256] __attribute__((aligned(16)));

static void show(const char *name, const void *p, unsigned n)
{
    const unsigned char *b = p;
    printf("%s", name);
    for (unsigned i = 0; i < n; i++)
        printf(" %02x", b[i]);
    printf("\n");
}

static void set_pools(int kind)
{
    unsigned x = 2463534242u + (unsigned)kind * 977u;
    for (unsigned i = 0; i < sizeof pool; i++) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        pool[i] = kind == 1 ? (unsigned char)((x & 1) ? 0xff : (x & 2) ? 0x80 : (x & 4) ? 0x7f : 0) : (unsigned char)x;
    }
    float *f = (float *)fpool;
    for (unsigned i = 0; i < sizeof fpool / 4; i++) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        f[i] = (float)((int)(x % 2001) - 1000) * 0.25f;
    }
}

static void t_vdup_n_u8(void)
{
    unsigned char a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vdup_n_u8(a0); show("vdup_n_u8", &r, sizeof r);
}

static void t_vmov_n_u8(void)
{
    unsigned char a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vmov_n_u8(a0); show("vmov_n_u8", &r, sizeof r);
}

static void t_vld1_u8(void)
{
    __auto_type r = vld1_u8((const unsigned char *)(pool + 0)); show("vld1_u8", &r, sizeof r);
}

static void t_vst1_u8(void)
{
    memset(out, 0, sizeof out);
    uint8x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    vst1_u8((unsigned char *)out, a1);
    show("vst1_u8", out, sizeof out);
}

static void t_vget_lane_u8(void)
{
    uint8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vget_lane_u8(a0, 7); show("vget_lane_u8", &r, sizeof r);
}

static void t_vset_lane_u8(void)
{
    unsigned char a0; memcpy(&a0, pool + 0, sizeof a0);
    uint8x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vset_lane_u8(a0, a1, 7); show("vset_lane_u8", &r, sizeof r);
}

static void t_vdup_n_s8(void)
{
    signed char a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vdup_n_s8(a0); show("vdup_n_s8", &r, sizeof r);
}

static void t_vmov_n_s8(void)
{
    signed char a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vmov_n_s8(a0); show("vmov_n_s8", &r, sizeof r);
}

static void t_vld1_s8(void)
{
    __auto_type r = vld1_s8((const signed char *)(pool + 0)); show("vld1_s8", &r, sizeof r);
}

static void t_vst1_s8(void)
{
    memset(out, 0, sizeof out);
    int8x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    vst1_s8((signed char *)out, a1);
    show("vst1_s8", out, sizeof out);
}

static void t_vget_lane_s8(void)
{
    int8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vget_lane_s8(a0, 7); show("vget_lane_s8", &r, sizeof r);
}

static void t_vset_lane_s8(void)
{
    signed char a0; memcpy(&a0, pool + 0, sizeof a0);
    int8x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vset_lane_s8(a0, a1, 7); show("vset_lane_s8", &r, sizeof r);
}

static void t_vdup_n_u16(void)
{
    unsigned short a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vdup_n_u16(a0); show("vdup_n_u16", &r, sizeof r);
}

static void t_vmov_n_u16(void)
{
    unsigned short a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vmov_n_u16(a0); show("vmov_n_u16", &r, sizeof r);
}

static void t_vld1_u16(void)
{
    __auto_type r = vld1_u16((const unsigned short *)(pool + 0)); show("vld1_u16", &r, sizeof r);
}

static void t_vst1_u16(void)
{
    memset(out, 0, sizeof out);
    uint16x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    vst1_u16((unsigned short *)out, a1);
    show("vst1_u16", out, sizeof out);
}

static void t_vget_lane_u16(void)
{
    uint16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vget_lane_u16(a0, 3); show("vget_lane_u16", &r, sizeof r);
}

static void t_vset_lane_u16(void)
{
    unsigned short a0; memcpy(&a0, pool + 0, sizeof a0);
    uint16x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vset_lane_u16(a0, a1, 3); show("vset_lane_u16", &r, sizeof r);
}

static void t_vdup_n_s16(void)
{
    short a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vdup_n_s16(a0); show("vdup_n_s16", &r, sizeof r);
}

static void t_vmov_n_s16(void)
{
    short a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vmov_n_s16(a0); show("vmov_n_s16", &r, sizeof r);
}

static void t_vld1_s16(void)
{
    __auto_type r = vld1_s16((const short *)(pool + 0)); show("vld1_s16", &r, sizeof r);
}

static void t_vst1_s16(void)
{
    memset(out, 0, sizeof out);
    int16x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    vst1_s16((short *)out, a1);
    show("vst1_s16", out, sizeof out);
}

static void t_vget_lane_s16(void)
{
    int16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vget_lane_s16(a0, 3); show("vget_lane_s16", &r, sizeof r);
}

static void t_vset_lane_s16(void)
{
    short a0; memcpy(&a0, pool + 0, sizeof a0);
    int16x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vset_lane_s16(a0, a1, 3); show("vset_lane_s16", &r, sizeof r);
}

static void t_vdup_n_u32(void)
{
    unsigned int a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vdup_n_u32(a0); show("vdup_n_u32", &r, sizeof r);
}

static void t_vmov_n_u32(void)
{
    unsigned int a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vmov_n_u32(a0); show("vmov_n_u32", &r, sizeof r);
}

static void t_vld1_u32(void)
{
    __auto_type r = vld1_u32((const unsigned int *)(pool + 0)); show("vld1_u32", &r, sizeof r);
}

static void t_vst1_u32(void)
{
    memset(out, 0, sizeof out);
    uint32x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    vst1_u32((unsigned int *)out, a1);
    show("vst1_u32", out, sizeof out);
}

static void t_vget_lane_u32(void)
{
    uint32x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vget_lane_u32(a0, 1); show("vget_lane_u32", &r, sizeof r);
}

static void t_vset_lane_u32(void)
{
    unsigned int a0; memcpy(&a0, pool + 0, sizeof a0);
    uint32x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vset_lane_u32(a0, a1, 1); show("vset_lane_u32", &r, sizeof r);
}

static void t_vdup_n_s32(void)
{
    __auto_type r = vdup_n_s32(3); show("vdup_n_s32", &r, sizeof r);
}

static void t_vmov_n_s32(void)
{
    __auto_type r = vmov_n_s32(3); show("vmov_n_s32", &r, sizeof r);
}

static void t_vld1_s32(void)
{
    __auto_type r = vld1_s32((const int *)(pool + 0)); show("vld1_s32", &r, sizeof r);
}

static void t_vst1_s32(void)
{
    memset(out, 0, sizeof out);
    int32x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    vst1_s32((int *)out, a1);
    show("vst1_s32", out, sizeof out);
}

static void t_vget_lane_s32(void)
{
    int32x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vget_lane_s32(a0, 1); show("vget_lane_s32", &r, sizeof r);
}

static void t_vset_lane_s32(void)
{
    int32x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vset_lane_s32(1, a1, 1); show("vset_lane_s32", &r, sizeof r);
}

static void t_vdup_n_u64(void)
{
    unsigned long long a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vdup_n_u64(a0); show("vdup_n_u64", &r, sizeof r);
}

static void t_vmov_n_u64(void)
{
    unsigned long long a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vmov_n_u64(a0); show("vmov_n_u64", &r, sizeof r);
}

static void t_vld1_u64(void)
{
    __auto_type r = vld1_u64((const unsigned long long *)(pool + 0)); show("vld1_u64", &r, sizeof r);
}

static void t_vst1_u64(void)
{
    memset(out, 0, sizeof out);
    uint64x1_t a1; memcpy(&a1, pool + 32, sizeof a1);
    vst1_u64((unsigned long long *)out, a1);
    show("vst1_u64", out, sizeof out);
}

static void t_vget_lane_u64(void)
{
    uint64x1_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vget_lane_u64(a0, 0); show("vget_lane_u64", &r, sizeof r);
}

static void t_vset_lane_u64(void)
{
    unsigned long long a0; memcpy(&a0, pool + 0, sizeof a0);
    uint64x1_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vset_lane_u64(a0, a1, 0); show("vset_lane_u64", &r, sizeof r);
}

static void t_vdup_n_s64(void)
{
    long long a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vdup_n_s64(a0); show("vdup_n_s64", &r, sizeof r);
}

static void t_vmov_n_s64(void)
{
    long long a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vmov_n_s64(a0); show("vmov_n_s64", &r, sizeof r);
}

static void t_vld1_s64(void)
{
    __auto_type r = vld1_s64((const long long *)(pool + 0)); show("vld1_s64", &r, sizeof r);
}

static void t_vst1_s64(void)
{
    memset(out, 0, sizeof out);
    int64x1_t a1; memcpy(&a1, pool + 32, sizeof a1);
    vst1_s64((long long *)out, a1);
    show("vst1_s64", out, sizeof out);
}

static void t_vget_lane_s64(void)
{
    int64x1_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vget_lane_s64(a0, 0); show("vget_lane_s64", &r, sizeof r);
}

static void t_vset_lane_s64(void)
{
    long long a0; memcpy(&a0, pool + 0, sizeof a0);
    int64x1_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vset_lane_s64(a0, a1, 0); show("vset_lane_s64", &r, sizeof r);
}

static void t_vdup_n_f32(void)
{
    float a0; memcpy(&a0, fpool + 0, sizeof a0);
    __auto_type r = vdup_n_f32(a0); show("vdup_n_f32", &r, sizeof r);
}

static void t_vmov_n_f32(void)
{
    float a0; memcpy(&a0, fpool + 0, sizeof a0);
    __auto_type r = vmov_n_f32(a0); show("vmov_n_f32", &r, sizeof r);
}

static void t_vld1_f32(void)
{
    __auto_type r = vld1_f32((const float *)(fpool + 0)); show("vld1_f32", &r, sizeof r);
}

static void t_vst1_f32(void)
{
    memset(out, 0, sizeof out);
    float32x2_t a1; memcpy(&a1, fpool + 32, sizeof a1);
    vst1_f32((float *)out, a1);
    show("vst1_f32", out, sizeof out);
}

static void t_vget_lane_f32(void)
{
    float32x2_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    __auto_type r = vget_lane_f32(a0, 1); show("vget_lane_f32", &r, sizeof r);
}

static void t_vset_lane_f32(void)
{
    float a0; memcpy(&a0, fpool + 0, sizeof a0);
    float32x2_t a1; memcpy(&a1, fpool + 32, sizeof a1);
    __auto_type r = vset_lane_f32(a0, a1, 1); show("vset_lane_f32", &r, sizeof r);
}

static void t_vdup_n_f64(void)
{
    double a0; memcpy(&a0, fpool + 0, sizeof a0);
    __auto_type r = vdup_n_f64(a0); show("vdup_n_f64", &r, sizeof r);
}

static void t_vmov_n_f64(void)
{
    double a0; memcpy(&a0, fpool + 0, sizeof a0);
    __auto_type r = vmov_n_f64(a0); show("vmov_n_f64", &r, sizeof r);
}

static void t_vld1_f64(void)
{
    __auto_type r = vld1_f64((const double *)(pool + 0)); show("vld1_f64", &r, sizeof r);
}

static void t_vst1_f64(void)
{
    memset(out, 0, sizeof out);
    float64x1_t a1; memcpy(&a1, fpool + 32, sizeof a1);
    vst1_f64((double *)out, a1);
    show("vst1_f64", out, sizeof out);
}

static void t_vget_lane_f64(void)
{
    float64x1_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    __auto_type r = vget_lane_f64(a0, 0); show("vget_lane_f64", &r, sizeof r);
}

static void t_vset_lane_f64(void)
{
    double a0; memcpy(&a0, fpool + 0, sizeof a0);
    float64x1_t a1; memcpy(&a1, fpool + 32, sizeof a1);
    __auto_type r = vset_lane_f64(a0, a1, 0); show("vset_lane_f64", &r, sizeof r);
}

static void t_vld1_p8(void)
{
    __auto_type r = vld1_p8((const unsigned char *)(pool + 0)); show("vld1_p8", &r, sizeof r);
}

static void t_vst1_p8(void)
{
    memset(out, 0, sizeof out);
    poly8x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    vst1_p8((unsigned char *)out, a1);
    show("vst1_p8", out, sizeof out);
}

static void t_vget_lane_p8(void)
{
    poly8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vget_lane_p8(a0, 7); show("vget_lane_p8", &r, sizeof r);
}

static void t_vset_lane_p8(void)
{
    unsigned char a0; memcpy(&a0, pool + 0, sizeof a0);
    poly8x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vset_lane_p8(a0, a1, 7); show("vset_lane_p8", &r, sizeof r);
}

static void t_vld1_p16(void)
{
    __auto_type r = vld1_p16((const unsigned short *)(pool + 0)); show("vld1_p16", &r, sizeof r);
}

static void t_vst1_p16(void)
{
    memset(out, 0, sizeof out);
    poly16x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    vst1_p16((unsigned short *)out, a1);
    show("vst1_p16", out, sizeof out);
}

static void t_vget_lane_p16(void)
{
    poly16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vget_lane_p16(a0, 3); show("vget_lane_p16", &r, sizeof r);
}

static void t_vset_lane_p16(void)
{
    unsigned short a0; memcpy(&a0, pool + 0, sizeof a0);
    poly16x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vset_lane_p16(a0, a1, 3); show("vset_lane_p16", &r, sizeof r);
}

static void t_vld1_p64(void)
{
    __auto_type r = vld1_p64((const unsigned long long *)(pool + 0)); show("vld1_p64", &r, sizeof r);
}

static void t_vst1_p64(void)
{
    memset(out, 0, sizeof out);
    poly64x1_t a1; memcpy(&a1, pool + 32, sizeof a1);
    vst1_p64((unsigned long long *)out, a1);
    show("vst1_p64", out, sizeof out);
}

static void t_vget_lane_p64(void)
{
    poly64x1_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vget_lane_p64(a0, 0); show("vget_lane_p64", &r, sizeof r);
}

static void t_vset_lane_p64(void)
{
    unsigned long long a0; memcpy(&a0, pool + 0, sizeof a0);
    poly64x1_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vset_lane_p64(a0, a1, 0); show("vset_lane_p64", &r, sizeof r);
}

static void t_vadd_u8(void)
{
    uint8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint8x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vadd_u8(a0, a1); show("vadd_u8", &r, sizeof r);
}

static void t_vsub_u8(void)
{
    uint8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint8x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vsub_u8(a0, a1); show("vsub_u8", &r, sizeof r);
}

static void t_vmul_u8(void)
{
    uint8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint8x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vmul_u8(a0, a1); show("vmul_u8", &r, sizeof r);
}

static void t_vceq_u8(void)
{
    uint8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint8x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vceq_u8(a0, a1); show("vceq_u8", &r, sizeof r);
}

static void t_vcge_u8(void)
{
    uint8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint8x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vcge_u8(a0, a1); show("vcge_u8", &r, sizeof r);
}

static void t_vcgt_u8(void)
{
    uint8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint8x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vcgt_u8(a0, a1); show("vcgt_u8", &r, sizeof r);
}

static void t_vcle_u8(void)
{
    uint8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint8x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vcle_u8(a0, a1); show("vcle_u8", &r, sizeof r);
}

static void t_vclt_u8(void)
{
    uint8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint8x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vclt_u8(a0, a1); show("vclt_u8", &r, sizeof r);
}

static void t_vbsl_u8(void)
{
    uint8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint8x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    uint8x8_t a2; memcpy(&a2, pool + 64, sizeof a2);
    __auto_type r = vbsl_u8(a0, a1, a2); show("vbsl_u8", &r, sizeof r);
}

static void t_vadd_s8(void)
{
    int8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int8x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vadd_s8(a0, a1); show("vadd_s8", &r, sizeof r);
}

static void t_vsub_s8(void)
{
    int8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int8x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vsub_s8(a0, a1); show("vsub_s8", &r, sizeof r);
}

static void t_vmul_s8(void)
{
    int8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int8x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vmul_s8(a0, a1); show("vmul_s8", &r, sizeof r);
}

static void t_vceq_s8(void)
{
    int8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int8x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vceq_s8(a0, a1); show("vceq_s8", &r, sizeof r);
}

static void t_vcge_s8(void)
{
    int8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int8x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vcge_s8(a0, a1); show("vcge_s8", &r, sizeof r);
}

static void t_vcgt_s8(void)
{
    int8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int8x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vcgt_s8(a0, a1); show("vcgt_s8", &r, sizeof r);
}

static void t_vcle_s8(void)
{
    int8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int8x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vcle_s8(a0, a1); show("vcle_s8", &r, sizeof r);
}

static void t_vclt_s8(void)
{
    int8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int8x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vclt_s8(a0, a1); show("vclt_s8", &r, sizeof r);
}

static void t_vneg_s8(void)
{
    int8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vneg_s8(a0); show("vneg_s8", &r, sizeof r);
}

static void t_vbsl_s8(void)
{
    uint8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int8x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    int8x8_t a2; memcpy(&a2, pool + 64, sizeof a2);
    __auto_type r = vbsl_s8(a0, a1, a2); show("vbsl_s8", &r, sizeof r);
}

static void t_vadd_u16(void)
{
    uint16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint16x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vadd_u16(a0, a1); show("vadd_u16", &r, sizeof r);
}

static void t_vsub_u16(void)
{
    uint16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint16x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vsub_u16(a0, a1); show("vsub_u16", &r, sizeof r);
}

static void t_vmul_u16(void)
{
    uint16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint16x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vmul_u16(a0, a1); show("vmul_u16", &r, sizeof r);
}

static void t_vceq_u16(void)
{
    uint16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint16x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vceq_u16(a0, a1); show("vceq_u16", &r, sizeof r);
}

static void t_vcge_u16(void)
{
    uint16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint16x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vcge_u16(a0, a1); show("vcge_u16", &r, sizeof r);
}

static void t_vcgt_u16(void)
{
    uint16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint16x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vcgt_u16(a0, a1); show("vcgt_u16", &r, sizeof r);
}

static void t_vcle_u16(void)
{
    uint16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint16x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vcle_u16(a0, a1); show("vcle_u16", &r, sizeof r);
}

static void t_vclt_u16(void)
{
    uint16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint16x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vclt_u16(a0, a1); show("vclt_u16", &r, sizeof r);
}

static void t_vbsl_u16(void)
{
    uint16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint16x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    uint16x4_t a2; memcpy(&a2, pool + 64, sizeof a2);
    __auto_type r = vbsl_u16(a0, a1, a2); show("vbsl_u16", &r, sizeof r);
}

static void t_vadd_s16(void)
{
    int16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int16x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vadd_s16(a0, a1); show("vadd_s16", &r, sizeof r);
}

static void t_vsub_s16(void)
{
    int16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int16x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vsub_s16(a0, a1); show("vsub_s16", &r, sizeof r);
}

static void t_vmul_s16(void)
{
    int16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int16x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vmul_s16(a0, a1); show("vmul_s16", &r, sizeof r);
}

static void t_vceq_s16(void)
{
    int16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int16x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vceq_s16(a0, a1); show("vceq_s16", &r, sizeof r);
}

static void t_vcge_s16(void)
{
    int16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int16x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vcge_s16(a0, a1); show("vcge_s16", &r, sizeof r);
}

static void t_vcgt_s16(void)
{
    int16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int16x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vcgt_s16(a0, a1); show("vcgt_s16", &r, sizeof r);
}

static void t_vcle_s16(void)
{
    int16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int16x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vcle_s16(a0, a1); show("vcle_s16", &r, sizeof r);
}

static void t_vclt_s16(void)
{
    int16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int16x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vclt_s16(a0, a1); show("vclt_s16", &r, sizeof r);
}

static void t_vneg_s16(void)
{
    int16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vneg_s16(a0); show("vneg_s16", &r, sizeof r);
}

static void t_vbsl_s16(void)
{
    uint16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int16x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    int16x4_t a2; memcpy(&a2, pool + 64, sizeof a2);
    __auto_type r = vbsl_s16(a0, a1, a2); show("vbsl_s16", &r, sizeof r);
}

static void t_vadd_u32(void)
{
    uint32x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint32x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vadd_u32(a0, a1); show("vadd_u32", &r, sizeof r);
}

static void t_vsub_u32(void)
{
    uint32x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint32x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vsub_u32(a0, a1); show("vsub_u32", &r, sizeof r);
}

static void t_vmul_u32(void)
{
    uint32x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint32x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vmul_u32(a0, a1); show("vmul_u32", &r, sizeof r);
}

static void t_vceq_u32(void)
{
    uint32x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint32x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vceq_u32(a0, a1); show("vceq_u32", &r, sizeof r);
}

static void t_vcge_u32(void)
{
    uint32x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint32x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vcge_u32(a0, a1); show("vcge_u32", &r, sizeof r);
}

static void t_vcgt_u32(void)
{
    uint32x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint32x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vcgt_u32(a0, a1); show("vcgt_u32", &r, sizeof r);
}

static void t_vcle_u32(void)
{
    uint32x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint32x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vcle_u32(a0, a1); show("vcle_u32", &r, sizeof r);
}

static void t_vclt_u32(void)
{
    uint32x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint32x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vclt_u32(a0, a1); show("vclt_u32", &r, sizeof r);
}

static void t_vbsl_u32(void)
{
    uint32x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint32x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    uint32x2_t a2; memcpy(&a2, pool + 64, sizeof a2);
    __auto_type r = vbsl_u32(a0, a1, a2); show("vbsl_u32", &r, sizeof r);
}

static void t_vadd_s32(void)
{
    int32x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int32x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vadd_s32(a0, a1); show("vadd_s32", &r, sizeof r);
}

static void t_vsub_s32(void)
{
    int32x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int32x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vsub_s32(a0, a1); show("vsub_s32", &r, sizeof r);
}

static void t_vmul_s32(void)
{
    int32x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int32x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vmul_s32(a0, a1); show("vmul_s32", &r, sizeof r);
}

static void t_vceq_s32(void)
{
    int32x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int32x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vceq_s32(a0, a1); show("vceq_s32", &r, sizeof r);
}

static void t_vcge_s32(void)
{
    int32x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int32x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vcge_s32(a0, a1); show("vcge_s32", &r, sizeof r);
}

static void t_vcgt_s32(void)
{
    int32x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int32x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vcgt_s32(a0, a1); show("vcgt_s32", &r, sizeof r);
}

static void t_vcle_s32(void)
{
    int32x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int32x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vcle_s32(a0, a1); show("vcle_s32", &r, sizeof r);
}

static void t_vclt_s32(void)
{
    int32x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int32x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vclt_s32(a0, a1); show("vclt_s32", &r, sizeof r);
}

static void t_vneg_s32(void)
{
    int32x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vneg_s32(a0); show("vneg_s32", &r, sizeof r);
}

static void t_vbsl_s32(void)
{
    uint32x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int32x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    int32x2_t a2; memcpy(&a2, pool + 64, sizeof a2);
    __auto_type r = vbsl_s32(a0, a1, a2); show("vbsl_s32", &r, sizeof r);
}

static void t_vadd_u64(void)
{
    uint64x1_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint64x1_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vadd_u64(a0, a1); show("vadd_u64", &r, sizeof r);
}

static void t_vsub_u64(void)
{
    uint64x1_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint64x1_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vsub_u64(a0, a1); show("vsub_u64", &r, sizeof r);
}

static void t_vceq_u64(void)
{
    uint64x1_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint64x1_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vceq_u64(a0, a1); show("vceq_u64", &r, sizeof r);
}

static void t_vcge_u64(void)
{
    uint64x1_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint64x1_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vcge_u64(a0, a1); show("vcge_u64", &r, sizeof r);
}

static void t_vcgt_u64(void)
{
    uint64x1_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint64x1_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vcgt_u64(a0, a1); show("vcgt_u64", &r, sizeof r);
}

static void t_vcle_u64(void)
{
    uint64x1_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint64x1_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vcle_u64(a0, a1); show("vcle_u64", &r, sizeof r);
}

static void t_vclt_u64(void)
{
    uint64x1_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint64x1_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vclt_u64(a0, a1); show("vclt_u64", &r, sizeof r);
}

static void t_vbsl_u64(void)
{
    uint64x1_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint64x1_t a1; memcpy(&a1, pool + 32, sizeof a1);
    uint64x1_t a2; memcpy(&a2, pool + 64, sizeof a2);
    __auto_type r = vbsl_u64(a0, a1, a2); show("vbsl_u64", &r, sizeof r);
}

static void t_vadd_s64(void)
{
    int64x1_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int64x1_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vadd_s64(a0, a1); show("vadd_s64", &r, sizeof r);
}

static void t_vsub_s64(void)
{
    int64x1_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int64x1_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vsub_s64(a0, a1); show("vsub_s64", &r, sizeof r);
}

static void t_vceq_s64(void)
{
    int64x1_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int64x1_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vceq_s64(a0, a1); show("vceq_s64", &r, sizeof r);
}

static void t_vcge_s64(void)
{
    int64x1_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int64x1_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vcge_s64(a0, a1); show("vcge_s64", &r, sizeof r);
}

static void t_vcgt_s64(void)
{
    int64x1_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int64x1_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vcgt_s64(a0, a1); show("vcgt_s64", &r, sizeof r);
}

static void t_vcle_s64(void)
{
    int64x1_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int64x1_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vcle_s64(a0, a1); show("vcle_s64", &r, sizeof r);
}

static void t_vclt_s64(void)
{
    int64x1_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int64x1_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vclt_s64(a0, a1); show("vclt_s64", &r, sizeof r);
}

static void t_vneg_s64(void)
{
    int64x1_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vneg_s64(a0); show("vneg_s64", &r, sizeof r);
}

static void t_vbsl_s64(void)
{
    uint64x1_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int64x1_t a1; memcpy(&a1, pool + 32, sizeof a1);
    int64x1_t a2; memcpy(&a2, pool + 64, sizeof a2);
    __auto_type r = vbsl_s64(a0, a1, a2); show("vbsl_s64", &r, sizeof r);
}

static void t_vadd_f32(void)
{
    float32x2_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    float32x2_t a1; memcpy(&a1, fpool + 32, sizeof a1);
    __auto_type r = vadd_f32(a0, a1); show("vadd_f32", &r, sizeof r);
}

static void t_vsub_f32(void)
{
    float32x2_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    float32x2_t a1; memcpy(&a1, fpool + 32, sizeof a1);
    __auto_type r = vsub_f32(a0, a1); show("vsub_f32", &r, sizeof r);
}

static void t_vmul_f32(void)
{
    float32x2_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    float32x2_t a1; memcpy(&a1, fpool + 32, sizeof a1);
    __auto_type r = vmul_f32(a0, a1); show("vmul_f32", &r, sizeof r);
}

static void t_vdiv_f32(void)
{
    float32x2_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    float32x2_t a1; memcpy(&a1, fpool + 32, sizeof a1);
    __auto_type r = vdiv_f32(a0, a1); show("vdiv_f32", &r, sizeof r);
}

static void t_vceq_f32(void)
{
    float32x2_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    float32x2_t a1; memcpy(&a1, fpool + 32, sizeof a1);
    __auto_type r = vceq_f32(a0, a1); show("vceq_f32", &r, sizeof r);
}

static void t_vcge_f32(void)
{
    float32x2_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    float32x2_t a1; memcpy(&a1, fpool + 32, sizeof a1);
    __auto_type r = vcge_f32(a0, a1); show("vcge_f32", &r, sizeof r);
}

static void t_vcgt_f32(void)
{
    float32x2_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    float32x2_t a1; memcpy(&a1, fpool + 32, sizeof a1);
    __auto_type r = vcgt_f32(a0, a1); show("vcgt_f32", &r, sizeof r);
}

static void t_vcle_f32(void)
{
    float32x2_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    float32x2_t a1; memcpy(&a1, fpool + 32, sizeof a1);
    __auto_type r = vcle_f32(a0, a1); show("vcle_f32", &r, sizeof r);
}

static void t_vclt_f32(void)
{
    float32x2_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    float32x2_t a1; memcpy(&a1, fpool + 32, sizeof a1);
    __auto_type r = vclt_f32(a0, a1); show("vclt_f32", &r, sizeof r);
}

static void t_vneg_f32(void)
{
    float32x2_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    __auto_type r = vneg_f32(a0); show("vneg_f32", &r, sizeof r);
}

static void t_vbsl_f32(void)
{
    uint32x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    float32x2_t a1; memcpy(&a1, fpool + 32, sizeof a1);
    float32x2_t a2; memcpy(&a2, fpool + 64, sizeof a2);
    __auto_type r = vbsl_f32(a0, a1, a2); show("vbsl_f32", &r, sizeof r);
}

static void t_vadd_f64(void)
{
    float64x1_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    float64x1_t a1; memcpy(&a1, fpool + 32, sizeof a1);
    __auto_type r = vadd_f64(a0, a1); show("vadd_f64", &r, sizeof r);
}

static void t_vsub_f64(void)
{
    float64x1_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    float64x1_t a1; memcpy(&a1, fpool + 32, sizeof a1);
    __auto_type r = vsub_f64(a0, a1); show("vsub_f64", &r, sizeof r);
}

static void t_vmul_f64(void)
{
    float64x1_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    float64x1_t a1; memcpy(&a1, fpool + 32, sizeof a1);
    __auto_type r = vmul_f64(a0, a1); show("vmul_f64", &r, sizeof r);
}

static void t_vdiv_f64(void)
{
    float64x1_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    float64x1_t a1; memcpy(&a1, fpool + 32, sizeof a1);
    __auto_type r = vdiv_f64(a0, a1); show("vdiv_f64", &r, sizeof r);
}

static void t_vceq_f64(void)
{
    float64x1_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    float64x1_t a1; memcpy(&a1, fpool + 32, sizeof a1);
    __auto_type r = vceq_f64(a0, a1); show("vceq_f64", &r, sizeof r);
}

static void t_vcge_f64(void)
{
    float64x1_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    float64x1_t a1; memcpy(&a1, fpool + 32, sizeof a1);
    __auto_type r = vcge_f64(a0, a1); show("vcge_f64", &r, sizeof r);
}

static void t_vcgt_f64(void)
{
    float64x1_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    float64x1_t a1; memcpy(&a1, fpool + 32, sizeof a1);
    __auto_type r = vcgt_f64(a0, a1); show("vcgt_f64", &r, sizeof r);
}

static void t_vcle_f64(void)
{
    float64x1_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    float64x1_t a1; memcpy(&a1, fpool + 32, sizeof a1);
    __auto_type r = vcle_f64(a0, a1); show("vcle_f64", &r, sizeof r);
}

static void t_vclt_f64(void)
{
    float64x1_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    float64x1_t a1; memcpy(&a1, fpool + 32, sizeof a1);
    __auto_type r = vclt_f64(a0, a1); show("vclt_f64", &r, sizeof r);
}

static void t_vneg_f64(void)
{
    float64x1_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    __auto_type r = vneg_f64(a0); show("vneg_f64", &r, sizeof r);
}

static void t_vbsl_f64(void)
{
    uint64x1_t a0; memcpy(&a0, pool + 0, sizeof a0);
    float64x1_t a1; memcpy(&a1, fpool + 32, sizeof a1);
    float64x1_t a2; memcpy(&a2, fpool + 64, sizeof a2);
    __auto_type r = vbsl_f64(a0, a1, a2); show("vbsl_f64", &r, sizeof r);
}

static void t_vand_u8(void)
{
    uint8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint8x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vand_u8(a0, a1); show("vand_u8", &r, sizeof r);
}

static void t_vorr_u8(void)
{
    uint8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint8x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vorr_u8(a0, a1); show("vorr_u8", &r, sizeof r);
}

static void t_veor_u8(void)
{
    uint8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint8x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = veor_u8(a0, a1); show("veor_u8", &r, sizeof r);
}

static void t_vbic_u8(void)
{
    uint8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint8x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vbic_u8(a0, a1); show("vbic_u8", &r, sizeof r);
}

static void t_vorn_u8(void)
{
    uint8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint8x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vorn_u8(a0, a1); show("vorn_u8", &r, sizeof r);
}

static void t_vtst_u8(void)
{
    uint8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint8x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vtst_u8(a0, a1); show("vtst_u8", &r, sizeof r);
}

static void t_vmvn_u8(void)
{
    uint8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vmvn_u8(a0); show("vmvn_u8", &r, sizeof r);
}

static void t_vmla_u8(void)
{
    uint8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint8x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    uint8x8_t a2; memcpy(&a2, pool + 64, sizeof a2);
    __auto_type r = vmla_u8(a0, a1, a2); show("vmla_u8", &r, sizeof r);
}

static void t_vmls_u8(void)
{
    uint8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint8x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    uint8x8_t a2; memcpy(&a2, pool + 64, sizeof a2);
    __auto_type r = vmls_u8(a0, a1, a2); show("vmls_u8", &r, sizeof r);
}

static void t_vshl_n_u8(void)
{
    uint8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vshl_n_u8(a0, 3); show("vshl_n_u8", &r, sizeof r);
}

static void t_vshr_n_u8(void)
{
    uint8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vshr_n_u8(a0, 3); show("vshr_n_u8", &r, sizeof r);
}

static void t_vand_s8(void)
{
    int8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int8x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vand_s8(a0, a1); show("vand_s8", &r, sizeof r);
}

static void t_vorr_s8(void)
{
    int8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int8x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vorr_s8(a0, a1); show("vorr_s8", &r, sizeof r);
}

static void t_veor_s8(void)
{
    int8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int8x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = veor_s8(a0, a1); show("veor_s8", &r, sizeof r);
}

static void t_vbic_s8(void)
{
    int8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int8x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vbic_s8(a0, a1); show("vbic_s8", &r, sizeof r);
}

static void t_vorn_s8(void)
{
    int8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int8x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vorn_s8(a0, a1); show("vorn_s8", &r, sizeof r);
}

static void t_vtst_s8(void)
{
    int8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int8x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vtst_s8(a0, a1); show("vtst_s8", &r, sizeof r);
}

static void t_vmvn_s8(void)
{
    int8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vmvn_s8(a0); show("vmvn_s8", &r, sizeof r);
}

static void t_vmla_s8(void)
{
    int8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int8x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    int8x8_t a2; memcpy(&a2, pool + 64, sizeof a2);
    __auto_type r = vmla_s8(a0, a1, a2); show("vmla_s8", &r, sizeof r);
}

static void t_vmls_s8(void)
{
    int8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int8x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    int8x8_t a2; memcpy(&a2, pool + 64, sizeof a2);
    __auto_type r = vmls_s8(a0, a1, a2); show("vmls_s8", &r, sizeof r);
}

static void t_vshl_n_s8(void)
{
    int8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vshl_n_s8(a0, 3); show("vshl_n_s8", &r, sizeof r);
}

static void t_vshr_n_s8(void)
{
    int8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vshr_n_s8(a0, 3); show("vshr_n_s8", &r, sizeof r);
}

static void t_vand_u16(void)
{
    uint16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint16x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vand_u16(a0, a1); show("vand_u16", &r, sizeof r);
}

static void t_vorr_u16(void)
{
    uint16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint16x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vorr_u16(a0, a1); show("vorr_u16", &r, sizeof r);
}

static void t_veor_u16(void)
{
    uint16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint16x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = veor_u16(a0, a1); show("veor_u16", &r, sizeof r);
}

static void t_vbic_u16(void)
{
    uint16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint16x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vbic_u16(a0, a1); show("vbic_u16", &r, sizeof r);
}

static void t_vorn_u16(void)
{
    uint16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint16x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vorn_u16(a0, a1); show("vorn_u16", &r, sizeof r);
}

static void t_vtst_u16(void)
{
    uint16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint16x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vtst_u16(a0, a1); show("vtst_u16", &r, sizeof r);
}

static void t_vmvn_u16(void)
{
    uint16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vmvn_u16(a0); show("vmvn_u16", &r, sizeof r);
}

static void t_vmla_u16(void)
{
    uint16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint16x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    uint16x4_t a2; memcpy(&a2, pool + 64, sizeof a2);
    __auto_type r = vmla_u16(a0, a1, a2); show("vmla_u16", &r, sizeof r);
}

static void t_vmls_u16(void)
{
    uint16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint16x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    uint16x4_t a2; memcpy(&a2, pool + 64, sizeof a2);
    __auto_type r = vmls_u16(a0, a1, a2); show("vmls_u16", &r, sizeof r);
}

static void t_vshl_n_u16(void)
{
    uint16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vshl_n_u16(a0, 3); show("vshl_n_u16", &r, sizeof r);
}

static void t_vshr_n_u16(void)
{
    uint16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vshr_n_u16(a0, 3); show("vshr_n_u16", &r, sizeof r);
}

static void t_vand_s16(void)
{
    int16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int16x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vand_s16(a0, a1); show("vand_s16", &r, sizeof r);
}

static void t_vorr_s16(void)
{
    int16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int16x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vorr_s16(a0, a1); show("vorr_s16", &r, sizeof r);
}

static void t_veor_s16(void)
{
    int16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int16x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = veor_s16(a0, a1); show("veor_s16", &r, sizeof r);
}

static void t_vbic_s16(void)
{
    int16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int16x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vbic_s16(a0, a1); show("vbic_s16", &r, sizeof r);
}

static void t_vorn_s16(void)
{
    int16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int16x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vorn_s16(a0, a1); show("vorn_s16", &r, sizeof r);
}

static void t_vtst_s16(void)
{
    int16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int16x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vtst_s16(a0, a1); show("vtst_s16", &r, sizeof r);
}

static void t_vmvn_s16(void)
{
    int16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vmvn_s16(a0); show("vmvn_s16", &r, sizeof r);
}

static void t_vmla_s16(void)
{
    int16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int16x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    int16x4_t a2; memcpy(&a2, pool + 64, sizeof a2);
    __auto_type r = vmla_s16(a0, a1, a2); show("vmla_s16", &r, sizeof r);
}

static void t_vmls_s16(void)
{
    int16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int16x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    int16x4_t a2; memcpy(&a2, pool + 64, sizeof a2);
    __auto_type r = vmls_s16(a0, a1, a2); show("vmls_s16", &r, sizeof r);
}

static void t_vshl_n_s16(void)
{
    int16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vshl_n_s16(a0, 3); show("vshl_n_s16", &r, sizeof r);
}

static void t_vshr_n_s16(void)
{
    int16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vshr_n_s16(a0, 3); show("vshr_n_s16", &r, sizeof r);
}

static void t_vand_u32(void)
{
    uint32x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint32x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vand_u32(a0, a1); show("vand_u32", &r, sizeof r);
}

static void t_vorr_u32(void)
{
    uint32x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint32x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vorr_u32(a0, a1); show("vorr_u32", &r, sizeof r);
}

static void t_veor_u32(void)
{
    uint32x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint32x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = veor_u32(a0, a1); show("veor_u32", &r, sizeof r);
}

static void t_vbic_u32(void)
{
    uint32x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint32x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vbic_u32(a0, a1); show("vbic_u32", &r, sizeof r);
}

static void t_vorn_u32(void)
{
    uint32x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint32x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vorn_u32(a0, a1); show("vorn_u32", &r, sizeof r);
}

static void t_vtst_u32(void)
{
    uint32x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint32x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vtst_u32(a0, a1); show("vtst_u32", &r, sizeof r);
}

static void t_vmvn_u32(void)
{
    uint32x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vmvn_u32(a0); show("vmvn_u32", &r, sizeof r);
}

static void t_vmla_u32(void)
{
    uint32x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint32x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    uint32x2_t a2; memcpy(&a2, pool + 64, sizeof a2);
    __auto_type r = vmla_u32(a0, a1, a2); show("vmla_u32", &r, sizeof r);
}

static void t_vmls_u32(void)
{
    uint32x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint32x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    uint32x2_t a2; memcpy(&a2, pool + 64, sizeof a2);
    __auto_type r = vmls_u32(a0, a1, a2); show("vmls_u32", &r, sizeof r);
}

static void t_vshl_n_u32(void)
{
    uint32x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vshl_n_u32(a0, 3); show("vshl_n_u32", &r, sizeof r);
}

static void t_vshr_n_u32(void)
{
    uint32x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vshr_n_u32(a0, 3); show("vshr_n_u32", &r, sizeof r);
}

static void t_vand_s32(void)
{
    int32x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int32x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vand_s32(a0, a1); show("vand_s32", &r, sizeof r);
}

static void t_vorr_s32(void)
{
    int32x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int32x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vorr_s32(a0, a1); show("vorr_s32", &r, sizeof r);
}

static void t_veor_s32(void)
{
    int32x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int32x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = veor_s32(a0, a1); show("veor_s32", &r, sizeof r);
}

static void t_vbic_s32(void)
{
    int32x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int32x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vbic_s32(a0, a1); show("vbic_s32", &r, sizeof r);
}

static void t_vorn_s32(void)
{
    int32x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int32x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vorn_s32(a0, a1); show("vorn_s32", &r, sizeof r);
}

static void t_vtst_s32(void)
{
    int32x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int32x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vtst_s32(a0, a1); show("vtst_s32", &r, sizeof r);
}

static void t_vmvn_s32(void)
{
    int32x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vmvn_s32(a0); show("vmvn_s32", &r, sizeof r);
}

static void t_vmla_s32(void)
{
    int32x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int32x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    int32x2_t a2; memcpy(&a2, pool + 64, sizeof a2);
    __auto_type r = vmla_s32(a0, a1, a2); show("vmla_s32", &r, sizeof r);
}

static void t_vmls_s32(void)
{
    int32x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int32x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    int32x2_t a2; memcpy(&a2, pool + 64, sizeof a2);
    __auto_type r = vmls_s32(a0, a1, a2); show("vmls_s32", &r, sizeof r);
}

static void t_vshl_n_s32(void)
{
    int32x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vshl_n_s32(a0, 3); show("vshl_n_s32", &r, sizeof r);
}

static void t_vshr_n_s32(void)
{
    int32x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vshr_n_s32(a0, 3); show("vshr_n_s32", &r, sizeof r);
}

static void t_vand_u64(void)
{
    uint64x1_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint64x1_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vand_u64(a0, a1); show("vand_u64", &r, sizeof r);
}

static void t_vorr_u64(void)
{
    uint64x1_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint64x1_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vorr_u64(a0, a1); show("vorr_u64", &r, sizeof r);
}

static void t_veor_u64(void)
{
    uint64x1_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint64x1_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = veor_u64(a0, a1); show("veor_u64", &r, sizeof r);
}

static void t_vbic_u64(void)
{
    uint64x1_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint64x1_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vbic_u64(a0, a1); show("vbic_u64", &r, sizeof r);
}

static void t_vorn_u64(void)
{
    uint64x1_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint64x1_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vorn_u64(a0, a1); show("vorn_u64", &r, sizeof r);
}

static void t_vtst_u64(void)
{
    uint64x1_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint64x1_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vtst_u64(a0, a1); show("vtst_u64", &r, sizeof r);
}

static void t_vshl_n_u64(void)
{
    uint64x1_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vshl_n_u64(a0, 3); show("vshl_n_u64", &r, sizeof r);
}

static void t_vshr_n_u64(void)
{
    uint64x1_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vshr_n_u64(a0, 3); show("vshr_n_u64", &r, sizeof r);
}

static void t_vand_s64(void)
{
    int64x1_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int64x1_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vand_s64(a0, a1); show("vand_s64", &r, sizeof r);
}

static void t_vorr_s64(void)
{
    int64x1_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int64x1_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vorr_s64(a0, a1); show("vorr_s64", &r, sizeof r);
}

static void t_veor_s64(void)
{
    int64x1_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int64x1_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = veor_s64(a0, a1); show("veor_s64", &r, sizeof r);
}

static void t_vbic_s64(void)
{
    int64x1_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int64x1_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vbic_s64(a0, a1); show("vbic_s64", &r, sizeof r);
}

static void t_vorn_s64(void)
{
    int64x1_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int64x1_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vorn_s64(a0, a1); show("vorn_s64", &r, sizeof r);
}

static void t_vtst_s64(void)
{
    int64x1_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int64x1_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vtst_s64(a0, a1); show("vtst_s64", &r, sizeof r);
}

static void t_vshl_n_s64(void)
{
    int64x1_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vshl_n_s64(a0, 3); show("vshl_n_s64", &r, sizeof r);
}

static void t_vshr_n_s64(void)
{
    int64x1_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vshr_n_s64(a0, 3); show("vshr_n_s64", &r, sizeof r);
}

static void t_vreinterpret_u8_s8(void)
{
    int8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_u8_s8(a0); show("vreinterpret_u8_s8", &r, sizeof r);
}

static void t_vreinterpret_u8_u16(void)
{
    uint16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_u8_u16(a0); show("vreinterpret_u8_u16", &r, sizeof r);
}

static void t_vreinterpret_u8_s16(void)
{
    int16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_u8_s16(a0); show("vreinterpret_u8_s16", &r, sizeof r);
}

static void t_vreinterpret_u8_u32(void)
{
    uint32x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_u8_u32(a0); show("vreinterpret_u8_u32", &r, sizeof r);
}

static void t_vreinterpret_u8_s32(void)
{
    int32x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_u8_s32(a0); show("vreinterpret_u8_s32", &r, sizeof r);
}

static void t_vreinterpret_u8_u64(void)
{
    uint64x1_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_u8_u64(a0); show("vreinterpret_u8_u64", &r, sizeof r);
}

static void t_vreinterpret_u8_s64(void)
{
    int64x1_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_u8_s64(a0); show("vreinterpret_u8_s64", &r, sizeof r);
}

static void t_vreinterpret_u8_f32(void)
{
    float32x2_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    __auto_type r = vreinterpret_u8_f32(a0); show("vreinterpret_u8_f32", &r, sizeof r);
}

static void t_vreinterpret_u8_f64(void)
{
    float64x1_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    __auto_type r = vreinterpret_u8_f64(a0); show("vreinterpret_u8_f64", &r, sizeof r);
}

static void t_vreinterpret_u8_p8(void)
{
    poly8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_u8_p8(a0); show("vreinterpret_u8_p8", &r, sizeof r);
}

static void t_vreinterpret_u8_p16(void)
{
    poly16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_u8_p16(a0); show("vreinterpret_u8_p16", &r, sizeof r);
}

static void t_vreinterpret_u8_p64(void)
{
    poly64x1_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_u8_p64(a0); show("vreinterpret_u8_p64", &r, sizeof r);
}

static void t_vreinterpret_s8_u8(void)
{
    uint8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_s8_u8(a0); show("vreinterpret_s8_u8", &r, sizeof r);
}

static void t_vreinterpret_s8_u16(void)
{
    uint16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_s8_u16(a0); show("vreinterpret_s8_u16", &r, sizeof r);
}

static void t_vreinterpret_s8_s16(void)
{
    int16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_s8_s16(a0); show("vreinterpret_s8_s16", &r, sizeof r);
}

static void t_vreinterpret_s8_u32(void)
{
    uint32x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_s8_u32(a0); show("vreinterpret_s8_u32", &r, sizeof r);
}

static void t_vreinterpret_s8_s32(void)
{
    int32x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_s8_s32(a0); show("vreinterpret_s8_s32", &r, sizeof r);
}

static void t_vreinterpret_s8_u64(void)
{
    uint64x1_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_s8_u64(a0); show("vreinterpret_s8_u64", &r, sizeof r);
}

static void t_vreinterpret_s8_s64(void)
{
    int64x1_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_s8_s64(a0); show("vreinterpret_s8_s64", &r, sizeof r);
}

static void t_vreinterpret_s8_f32(void)
{
    float32x2_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    __auto_type r = vreinterpret_s8_f32(a0); show("vreinterpret_s8_f32", &r, sizeof r);
}

static void t_vreinterpret_s8_f64(void)
{
    float64x1_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    __auto_type r = vreinterpret_s8_f64(a0); show("vreinterpret_s8_f64", &r, sizeof r);
}

static void t_vreinterpret_s8_p8(void)
{
    poly8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_s8_p8(a0); show("vreinterpret_s8_p8", &r, sizeof r);
}

static void t_vreinterpret_s8_p16(void)
{
    poly16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_s8_p16(a0); show("vreinterpret_s8_p16", &r, sizeof r);
}

static void t_vreinterpret_s8_p64(void)
{
    poly64x1_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_s8_p64(a0); show("vreinterpret_s8_p64", &r, sizeof r);
}

static void t_vreinterpret_u16_u8(void)
{
    uint8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_u16_u8(a0); show("vreinterpret_u16_u8", &r, sizeof r);
}

static void t_vreinterpret_u16_s8(void)
{
    int8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_u16_s8(a0); show("vreinterpret_u16_s8", &r, sizeof r);
}

static void t_vreinterpret_u16_s16(void)
{
    int16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_u16_s16(a0); show("vreinterpret_u16_s16", &r, sizeof r);
}

static void t_vreinterpret_u16_u32(void)
{
    uint32x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_u16_u32(a0); show("vreinterpret_u16_u32", &r, sizeof r);
}

static void t_vreinterpret_u16_s32(void)
{
    int32x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_u16_s32(a0); show("vreinterpret_u16_s32", &r, sizeof r);
}

static void t_vreinterpret_u16_u64(void)
{
    uint64x1_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_u16_u64(a0); show("vreinterpret_u16_u64", &r, sizeof r);
}

static void t_vreinterpret_u16_s64(void)
{
    int64x1_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_u16_s64(a0); show("vreinterpret_u16_s64", &r, sizeof r);
}

static void t_vreinterpret_u16_f32(void)
{
    float32x2_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    __auto_type r = vreinterpret_u16_f32(a0); show("vreinterpret_u16_f32", &r, sizeof r);
}

static void t_vreinterpret_u16_f64(void)
{
    float64x1_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    __auto_type r = vreinterpret_u16_f64(a0); show("vreinterpret_u16_f64", &r, sizeof r);
}

static void t_vreinterpret_u16_p8(void)
{
    poly8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_u16_p8(a0); show("vreinterpret_u16_p8", &r, sizeof r);
}

static void t_vreinterpret_u16_p16(void)
{
    poly16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_u16_p16(a0); show("vreinterpret_u16_p16", &r, sizeof r);
}

static void t_vreinterpret_u16_p64(void)
{
    poly64x1_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_u16_p64(a0); show("vreinterpret_u16_p64", &r, sizeof r);
}

static void t_vreinterpret_s16_u8(void)
{
    uint8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_s16_u8(a0); show("vreinterpret_s16_u8", &r, sizeof r);
}

static void t_vreinterpret_s16_s8(void)
{
    int8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_s16_s8(a0); show("vreinterpret_s16_s8", &r, sizeof r);
}

static void t_vreinterpret_s16_u16(void)
{
    uint16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_s16_u16(a0); show("vreinterpret_s16_u16", &r, sizeof r);
}

static void t_vreinterpret_s16_u32(void)
{
    uint32x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_s16_u32(a0); show("vreinterpret_s16_u32", &r, sizeof r);
}

static void t_vreinterpret_s16_s32(void)
{
    int32x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_s16_s32(a0); show("vreinterpret_s16_s32", &r, sizeof r);
}

static void t_vreinterpret_s16_u64(void)
{
    uint64x1_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_s16_u64(a0); show("vreinterpret_s16_u64", &r, sizeof r);
}

static void t_vreinterpret_s16_s64(void)
{
    int64x1_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_s16_s64(a0); show("vreinterpret_s16_s64", &r, sizeof r);
}

static void t_vreinterpret_s16_f32(void)
{
    float32x2_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    __auto_type r = vreinterpret_s16_f32(a0); show("vreinterpret_s16_f32", &r, sizeof r);
}

static void t_vreinterpret_s16_f64(void)
{
    float64x1_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    __auto_type r = vreinterpret_s16_f64(a0); show("vreinterpret_s16_f64", &r, sizeof r);
}

static void t_vreinterpret_s16_p8(void)
{
    poly8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_s16_p8(a0); show("vreinterpret_s16_p8", &r, sizeof r);
}

static void t_vreinterpret_s16_p16(void)
{
    poly16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_s16_p16(a0); show("vreinterpret_s16_p16", &r, sizeof r);
}

static void t_vreinterpret_s16_p64(void)
{
    poly64x1_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_s16_p64(a0); show("vreinterpret_s16_p64", &r, sizeof r);
}

static void t_vreinterpret_u32_u8(void)
{
    uint8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_u32_u8(a0); show("vreinterpret_u32_u8", &r, sizeof r);
}

static void t_vreinterpret_u32_s8(void)
{
    int8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_u32_s8(a0); show("vreinterpret_u32_s8", &r, sizeof r);
}

static void t_vreinterpret_u32_u16(void)
{
    uint16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_u32_u16(a0); show("vreinterpret_u32_u16", &r, sizeof r);
}

static void t_vreinterpret_u32_s16(void)
{
    int16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_u32_s16(a0); show("vreinterpret_u32_s16", &r, sizeof r);
}

static void t_vreinterpret_u32_s32(void)
{
    int32x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_u32_s32(a0); show("vreinterpret_u32_s32", &r, sizeof r);
}

static void t_vreinterpret_u32_u64(void)
{
    uint64x1_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_u32_u64(a0); show("vreinterpret_u32_u64", &r, sizeof r);
}

static void t_vreinterpret_u32_s64(void)
{
    int64x1_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_u32_s64(a0); show("vreinterpret_u32_s64", &r, sizeof r);
}

static void t_vreinterpret_u32_f32(void)
{
    float32x2_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    __auto_type r = vreinterpret_u32_f32(a0); show("vreinterpret_u32_f32", &r, sizeof r);
}

static void t_vreinterpret_u32_f64(void)
{
    float64x1_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    __auto_type r = vreinterpret_u32_f64(a0); show("vreinterpret_u32_f64", &r, sizeof r);
}

static void t_vreinterpret_u32_p8(void)
{
    poly8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_u32_p8(a0); show("vreinterpret_u32_p8", &r, sizeof r);
}

static void t_vreinterpret_u32_p16(void)
{
    poly16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_u32_p16(a0); show("vreinterpret_u32_p16", &r, sizeof r);
}

static void t_vreinterpret_u32_p64(void)
{
    poly64x1_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_u32_p64(a0); show("vreinterpret_u32_p64", &r, sizeof r);
}

static void t_vreinterpret_s32_u8(void)
{
    uint8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_s32_u8(a0); show("vreinterpret_s32_u8", &r, sizeof r);
}

static void t_vreinterpret_s32_s8(void)
{
    int8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_s32_s8(a0); show("vreinterpret_s32_s8", &r, sizeof r);
}

static void t_vreinterpret_s32_u16(void)
{
    uint16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_s32_u16(a0); show("vreinterpret_s32_u16", &r, sizeof r);
}

static void t_vreinterpret_s32_s16(void)
{
    int16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_s32_s16(a0); show("vreinterpret_s32_s16", &r, sizeof r);
}

static void t_vreinterpret_s32_u32(void)
{
    uint32x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_s32_u32(a0); show("vreinterpret_s32_u32", &r, sizeof r);
}

static void t_vreinterpret_s32_u64(void)
{
    uint64x1_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_s32_u64(a0); show("vreinterpret_s32_u64", &r, sizeof r);
}

static void t_vreinterpret_s32_s64(void)
{
    int64x1_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_s32_s64(a0); show("vreinterpret_s32_s64", &r, sizeof r);
}

static void t_vreinterpret_s32_f32(void)
{
    float32x2_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    __auto_type r = vreinterpret_s32_f32(a0); show("vreinterpret_s32_f32", &r, sizeof r);
}

static void t_vreinterpret_s32_f64(void)
{
    float64x1_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    __auto_type r = vreinterpret_s32_f64(a0); show("vreinterpret_s32_f64", &r, sizeof r);
}

static void t_vreinterpret_s32_p8(void)
{
    poly8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_s32_p8(a0); show("vreinterpret_s32_p8", &r, sizeof r);
}

static void t_vreinterpret_s32_p16(void)
{
    poly16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_s32_p16(a0); show("vreinterpret_s32_p16", &r, sizeof r);
}

static void t_vreinterpret_s32_p64(void)
{
    poly64x1_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_s32_p64(a0); show("vreinterpret_s32_p64", &r, sizeof r);
}

static void t_vreinterpret_u64_u8(void)
{
    uint8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_u64_u8(a0); show("vreinterpret_u64_u8", &r, sizeof r);
}

static void t_vreinterpret_u64_s8(void)
{
    int8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_u64_s8(a0); show("vreinterpret_u64_s8", &r, sizeof r);
}

static void t_vreinterpret_u64_u16(void)
{
    uint16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_u64_u16(a0); show("vreinterpret_u64_u16", &r, sizeof r);
}

static void t_vreinterpret_u64_s16(void)
{
    int16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_u64_s16(a0); show("vreinterpret_u64_s16", &r, sizeof r);
}

static void t_vreinterpret_u64_u32(void)
{
    uint32x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_u64_u32(a0); show("vreinterpret_u64_u32", &r, sizeof r);
}

static void t_vreinterpret_u64_s32(void)
{
    int32x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_u64_s32(a0); show("vreinterpret_u64_s32", &r, sizeof r);
}

static void t_vreinterpret_u64_s64(void)
{
    int64x1_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_u64_s64(a0); show("vreinterpret_u64_s64", &r, sizeof r);
}

static void t_vreinterpret_u64_f32(void)
{
    float32x2_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    __auto_type r = vreinterpret_u64_f32(a0); show("vreinterpret_u64_f32", &r, sizeof r);
}

static void t_vreinterpret_u64_f64(void)
{
    float64x1_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    __auto_type r = vreinterpret_u64_f64(a0); show("vreinterpret_u64_f64", &r, sizeof r);
}

static void t_vreinterpret_u64_p8(void)
{
    poly8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_u64_p8(a0); show("vreinterpret_u64_p8", &r, sizeof r);
}

static void t_vreinterpret_u64_p16(void)
{
    poly16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_u64_p16(a0); show("vreinterpret_u64_p16", &r, sizeof r);
}

static void t_vreinterpret_u64_p64(void)
{
    poly64x1_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_u64_p64(a0); show("vreinterpret_u64_p64", &r, sizeof r);
}

static void t_vreinterpret_s64_u8(void)
{
    uint8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_s64_u8(a0); show("vreinterpret_s64_u8", &r, sizeof r);
}

static void t_vreinterpret_s64_s8(void)
{
    int8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_s64_s8(a0); show("vreinterpret_s64_s8", &r, sizeof r);
}

static void t_vreinterpret_s64_u16(void)
{
    uint16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_s64_u16(a0); show("vreinterpret_s64_u16", &r, sizeof r);
}

static void t_vreinterpret_s64_s16(void)
{
    int16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_s64_s16(a0); show("vreinterpret_s64_s16", &r, sizeof r);
}

static void t_vreinterpret_s64_u32(void)
{
    uint32x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_s64_u32(a0); show("vreinterpret_s64_u32", &r, sizeof r);
}

static void t_vreinterpret_s64_s32(void)
{
    int32x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_s64_s32(a0); show("vreinterpret_s64_s32", &r, sizeof r);
}

static void t_vreinterpret_s64_u64(void)
{
    uint64x1_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_s64_u64(a0); show("vreinterpret_s64_u64", &r, sizeof r);
}

static void t_vreinterpret_s64_f32(void)
{
    float32x2_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    __auto_type r = vreinterpret_s64_f32(a0); show("vreinterpret_s64_f32", &r, sizeof r);
}

static void t_vreinterpret_s64_f64(void)
{
    float64x1_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    __auto_type r = vreinterpret_s64_f64(a0); show("vreinterpret_s64_f64", &r, sizeof r);
}

static void t_vreinterpret_s64_p8(void)
{
    poly8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_s64_p8(a0); show("vreinterpret_s64_p8", &r, sizeof r);
}

static void t_vreinterpret_s64_p16(void)
{
    poly16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_s64_p16(a0); show("vreinterpret_s64_p16", &r, sizeof r);
}

static void t_vreinterpret_s64_p64(void)
{
    poly64x1_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_s64_p64(a0); show("vreinterpret_s64_p64", &r, sizeof r);
}

static void t_vreinterpret_f32_u8(void)
{
    uint8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_f32_u8(a0); show("vreinterpret_f32_u8", &r, sizeof r);
}

static void t_vreinterpret_f32_s8(void)
{
    int8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_f32_s8(a0); show("vreinterpret_f32_s8", &r, sizeof r);
}

static void t_vreinterpret_f32_u16(void)
{
    uint16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_f32_u16(a0); show("vreinterpret_f32_u16", &r, sizeof r);
}

static void t_vreinterpret_f32_s16(void)
{
    int16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_f32_s16(a0); show("vreinterpret_f32_s16", &r, sizeof r);
}

static void t_vreinterpret_f32_u32(void)
{
    uint32x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_f32_u32(a0); show("vreinterpret_f32_u32", &r, sizeof r);
}

static void t_vreinterpret_f32_s32(void)
{
    int32x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_f32_s32(a0); show("vreinterpret_f32_s32", &r, sizeof r);
}

static void t_vreinterpret_f32_u64(void)
{
    uint64x1_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_f32_u64(a0); show("vreinterpret_f32_u64", &r, sizeof r);
}

static void t_vreinterpret_f32_s64(void)
{
    int64x1_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_f32_s64(a0); show("vreinterpret_f32_s64", &r, sizeof r);
}

static void t_vreinterpret_f32_f64(void)
{
    float64x1_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    __auto_type r = vreinterpret_f32_f64(a0); show("vreinterpret_f32_f64", &r, sizeof r);
}

static void t_vreinterpret_f32_p8(void)
{
    poly8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_f32_p8(a0); show("vreinterpret_f32_p8", &r, sizeof r);
}

static void t_vreinterpret_f32_p16(void)
{
    poly16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_f32_p16(a0); show("vreinterpret_f32_p16", &r, sizeof r);
}

static void t_vreinterpret_f32_p64(void)
{
    poly64x1_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_f32_p64(a0); show("vreinterpret_f32_p64", &r, sizeof r);
}

static void t_vreinterpret_f64_u8(void)
{
    uint8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_f64_u8(a0); show("vreinterpret_f64_u8", &r, sizeof r);
}

static void t_vreinterpret_f64_s8(void)
{
    int8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_f64_s8(a0); show("vreinterpret_f64_s8", &r, sizeof r);
}

static void t_vreinterpret_f64_u16(void)
{
    uint16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_f64_u16(a0); show("vreinterpret_f64_u16", &r, sizeof r);
}

static void t_vreinterpret_f64_s16(void)
{
    int16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_f64_s16(a0); show("vreinterpret_f64_s16", &r, sizeof r);
}

static void t_vreinterpret_f64_u32(void)
{
    uint32x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_f64_u32(a0); show("vreinterpret_f64_u32", &r, sizeof r);
}

static void t_vreinterpret_f64_s32(void)
{
    int32x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_f64_s32(a0); show("vreinterpret_f64_s32", &r, sizeof r);
}

static void t_vreinterpret_f64_u64(void)
{
    uint64x1_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_f64_u64(a0); show("vreinterpret_f64_u64", &r, sizeof r);
}

static void t_vreinterpret_f64_s64(void)
{
    int64x1_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_f64_s64(a0); show("vreinterpret_f64_s64", &r, sizeof r);
}

static void t_vreinterpret_f64_f32(void)
{
    float32x2_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    __auto_type r = vreinterpret_f64_f32(a0); show("vreinterpret_f64_f32", &r, sizeof r);
}

static void t_vreinterpret_f64_p8(void)
{
    poly8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_f64_p8(a0); show("vreinterpret_f64_p8", &r, sizeof r);
}

static void t_vreinterpret_f64_p16(void)
{
    poly16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_f64_p16(a0); show("vreinterpret_f64_p16", &r, sizeof r);
}

static void t_vreinterpret_f64_p64(void)
{
    poly64x1_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_f64_p64(a0); show("vreinterpret_f64_p64", &r, sizeof r);
}

static void t_vreinterpret_p8_u8(void)
{
    uint8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_p8_u8(a0); show("vreinterpret_p8_u8", &r, sizeof r);
}

static void t_vreinterpret_p8_s8(void)
{
    int8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_p8_s8(a0); show("vreinterpret_p8_s8", &r, sizeof r);
}

static void t_vreinterpret_p8_u16(void)
{
    uint16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_p8_u16(a0); show("vreinterpret_p8_u16", &r, sizeof r);
}

static void t_vreinterpret_p8_s16(void)
{
    int16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_p8_s16(a0); show("vreinterpret_p8_s16", &r, sizeof r);
}

static void t_vreinterpret_p8_u32(void)
{
    uint32x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_p8_u32(a0); show("vreinterpret_p8_u32", &r, sizeof r);
}

static void t_vreinterpret_p8_s32(void)
{
    int32x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_p8_s32(a0); show("vreinterpret_p8_s32", &r, sizeof r);
}

static void t_vreinterpret_p8_u64(void)
{
    uint64x1_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_p8_u64(a0); show("vreinterpret_p8_u64", &r, sizeof r);
}

static void t_vreinterpret_p8_s64(void)
{
    int64x1_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_p8_s64(a0); show("vreinterpret_p8_s64", &r, sizeof r);
}

static void t_vreinterpret_p8_f32(void)
{
    float32x2_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    __auto_type r = vreinterpret_p8_f32(a0); show("vreinterpret_p8_f32", &r, sizeof r);
}

static void t_vreinterpret_p8_f64(void)
{
    float64x1_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    __auto_type r = vreinterpret_p8_f64(a0); show("vreinterpret_p8_f64", &r, sizeof r);
}

static void t_vreinterpret_p8_p16(void)
{
    poly16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_p8_p16(a0); show("vreinterpret_p8_p16", &r, sizeof r);
}

static void t_vreinterpret_p8_p64(void)
{
    poly64x1_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_p8_p64(a0); show("vreinterpret_p8_p64", &r, sizeof r);
}

static void t_vreinterpret_p16_u8(void)
{
    uint8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_p16_u8(a0); show("vreinterpret_p16_u8", &r, sizeof r);
}

static void t_vreinterpret_p16_s8(void)
{
    int8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_p16_s8(a0); show("vreinterpret_p16_s8", &r, sizeof r);
}

static void t_vreinterpret_p16_u16(void)
{
    uint16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_p16_u16(a0); show("vreinterpret_p16_u16", &r, sizeof r);
}

static void t_vreinterpret_p16_s16(void)
{
    int16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_p16_s16(a0); show("vreinterpret_p16_s16", &r, sizeof r);
}

static void t_vreinterpret_p16_u32(void)
{
    uint32x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_p16_u32(a0); show("vreinterpret_p16_u32", &r, sizeof r);
}

static void t_vreinterpret_p16_s32(void)
{
    int32x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_p16_s32(a0); show("vreinterpret_p16_s32", &r, sizeof r);
}

static void t_vreinterpret_p16_u64(void)
{
    uint64x1_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_p16_u64(a0); show("vreinterpret_p16_u64", &r, sizeof r);
}

static void t_vreinterpret_p16_s64(void)
{
    int64x1_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_p16_s64(a0); show("vreinterpret_p16_s64", &r, sizeof r);
}

static void t_vreinterpret_p16_f32(void)
{
    float32x2_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    __auto_type r = vreinterpret_p16_f32(a0); show("vreinterpret_p16_f32", &r, sizeof r);
}

static void t_vreinterpret_p16_f64(void)
{
    float64x1_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    __auto_type r = vreinterpret_p16_f64(a0); show("vreinterpret_p16_f64", &r, sizeof r);
}

static void t_vreinterpret_p16_p8(void)
{
    poly8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_p16_p8(a0); show("vreinterpret_p16_p8", &r, sizeof r);
}

static void t_vreinterpret_p16_p64(void)
{
    poly64x1_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_p16_p64(a0); show("vreinterpret_p16_p64", &r, sizeof r);
}

static void t_vreinterpret_p64_u8(void)
{
    uint8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_p64_u8(a0); show("vreinterpret_p64_u8", &r, sizeof r);
}

static void t_vreinterpret_p64_s8(void)
{
    int8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_p64_s8(a0); show("vreinterpret_p64_s8", &r, sizeof r);
}

static void t_vreinterpret_p64_u16(void)
{
    uint16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_p64_u16(a0); show("vreinterpret_p64_u16", &r, sizeof r);
}

static void t_vreinterpret_p64_s16(void)
{
    int16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_p64_s16(a0); show("vreinterpret_p64_s16", &r, sizeof r);
}

static void t_vreinterpret_p64_u32(void)
{
    uint32x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_p64_u32(a0); show("vreinterpret_p64_u32", &r, sizeof r);
}

static void t_vreinterpret_p64_s32(void)
{
    int32x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_p64_s32(a0); show("vreinterpret_p64_s32", &r, sizeof r);
}

static void t_vreinterpret_p64_u64(void)
{
    uint64x1_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_p64_u64(a0); show("vreinterpret_p64_u64", &r, sizeof r);
}

static void t_vreinterpret_p64_s64(void)
{
    int64x1_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_p64_s64(a0); show("vreinterpret_p64_s64", &r, sizeof r);
}

static void t_vreinterpret_p64_f32(void)
{
    float32x2_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    __auto_type r = vreinterpret_p64_f32(a0); show("vreinterpret_p64_f32", &r, sizeof r);
}

static void t_vreinterpret_p64_f64(void)
{
    float64x1_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    __auto_type r = vreinterpret_p64_f64(a0); show("vreinterpret_p64_f64", &r, sizeof r);
}

static void t_vreinterpret_p64_p8(void)
{
    poly8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_p64_p8(a0); show("vreinterpret_p64_p8", &r, sizeof r);
}

static void t_vreinterpret_p64_p16(void)
{
    poly16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpret_p64_p16(a0); show("vreinterpret_p64_p16", &r, sizeof r);
}

static void t_vdupq_n_u8(void)
{
    unsigned char a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vdupq_n_u8(a0); show("vdupq_n_u8", &r, sizeof r);
}

static void t_vmovq_n_u8(void)
{
    unsigned char a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vmovq_n_u8(a0); show("vmovq_n_u8", &r, sizeof r);
}

static void t_vld1q_u8(void)
{
    __auto_type r = vld1q_u8((const unsigned char *)(pool + 0)); show("vld1q_u8", &r, sizeof r);
}

static void t_vst1q_u8(void)
{
    memset(out, 0, sizeof out);
    uint8x16_t a1; memcpy(&a1, pool + 32, sizeof a1);
    vst1q_u8((unsigned char *)out, a1);
    show("vst1q_u8", out, sizeof out);
}

static void t_vgetq_lane_u8(void)
{
    uint8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vgetq_lane_u8(a0, 15); show("vgetq_lane_u8", &r, sizeof r);
}

static void t_vsetq_lane_u8(void)
{
    unsigned char a0; memcpy(&a0, pool + 0, sizeof a0);
    uint8x16_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vsetq_lane_u8(a0, a1, 15); show("vsetq_lane_u8", &r, sizeof r);
}

static void t_vdupq_n_s8(void)
{
    signed char a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vdupq_n_s8(a0); show("vdupq_n_s8", &r, sizeof r);
}

static void t_vmovq_n_s8(void)
{
    signed char a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vmovq_n_s8(a0); show("vmovq_n_s8", &r, sizeof r);
}

static void t_vld1q_s8(void)
{
    __auto_type r = vld1q_s8((const signed char *)(pool + 0)); show("vld1q_s8", &r, sizeof r);
}

static void t_vst1q_s8(void)
{
    memset(out, 0, sizeof out);
    int8x16_t a1; memcpy(&a1, pool + 32, sizeof a1);
    vst1q_s8((signed char *)out, a1);
    show("vst1q_s8", out, sizeof out);
}

static void t_vgetq_lane_s8(void)
{
    int8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vgetq_lane_s8(a0, 15); show("vgetq_lane_s8", &r, sizeof r);
}

static void t_vsetq_lane_s8(void)
{
    signed char a0; memcpy(&a0, pool + 0, sizeof a0);
    int8x16_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vsetq_lane_s8(a0, a1, 15); show("vsetq_lane_s8", &r, sizeof r);
}

static void t_vdupq_n_u16(void)
{
    unsigned short a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vdupq_n_u16(a0); show("vdupq_n_u16", &r, sizeof r);
}

static void t_vmovq_n_u16(void)
{
    unsigned short a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vmovq_n_u16(a0); show("vmovq_n_u16", &r, sizeof r);
}

static void t_vld1q_u16(void)
{
    __auto_type r = vld1q_u16((const unsigned short *)(pool + 0)); show("vld1q_u16", &r, sizeof r);
}

static void t_vst1q_u16(void)
{
    memset(out, 0, sizeof out);
    uint16x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    vst1q_u16((unsigned short *)out, a1);
    show("vst1q_u16", out, sizeof out);
}

static void t_vgetq_lane_u16(void)
{
    uint16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vgetq_lane_u16(a0, 7); show("vgetq_lane_u16", &r, sizeof r);
}

static void t_vsetq_lane_u16(void)
{
    unsigned short a0; memcpy(&a0, pool + 0, sizeof a0);
    uint16x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vsetq_lane_u16(a0, a1, 7); show("vsetq_lane_u16", &r, sizeof r);
}

static void t_vdupq_n_s16(void)
{
    short a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vdupq_n_s16(a0); show("vdupq_n_s16", &r, sizeof r);
}

static void t_vmovq_n_s16(void)
{
    short a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vmovq_n_s16(a0); show("vmovq_n_s16", &r, sizeof r);
}

static void t_vld1q_s16(void)
{
    __auto_type r = vld1q_s16((const short *)(pool + 0)); show("vld1q_s16", &r, sizeof r);
}

static void t_vst1q_s16(void)
{
    memset(out, 0, sizeof out);
    int16x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    vst1q_s16((short *)out, a1);
    show("vst1q_s16", out, sizeof out);
}

static void t_vgetq_lane_s16(void)
{
    int16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vgetq_lane_s16(a0, 7); show("vgetq_lane_s16", &r, sizeof r);
}

static void t_vsetq_lane_s16(void)
{
    short a0; memcpy(&a0, pool + 0, sizeof a0);
    int16x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vsetq_lane_s16(a0, a1, 7); show("vsetq_lane_s16", &r, sizeof r);
}

static void t_vdupq_n_u32(void)
{
    unsigned int a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vdupq_n_u32(a0); show("vdupq_n_u32", &r, sizeof r);
}

static void t_vmovq_n_u32(void)
{
    unsigned int a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vmovq_n_u32(a0); show("vmovq_n_u32", &r, sizeof r);
}

static void t_vld1q_u32(void)
{
    __auto_type r = vld1q_u32((const unsigned int *)(pool + 0)); show("vld1q_u32", &r, sizeof r);
}

static void t_vst1q_u32(void)
{
    memset(out, 0, sizeof out);
    uint32x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    vst1q_u32((unsigned int *)out, a1);
    show("vst1q_u32", out, sizeof out);
}

static void t_vgetq_lane_u32(void)
{
    uint32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vgetq_lane_u32(a0, 3); show("vgetq_lane_u32", &r, sizeof r);
}

static void t_vsetq_lane_u32(void)
{
    unsigned int a0; memcpy(&a0, pool + 0, sizeof a0);
    uint32x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vsetq_lane_u32(a0, a1, 3); show("vsetq_lane_u32", &r, sizeof r);
}

static void t_vdupq_n_s32(void)
{
    __auto_type r = vdupq_n_s32(3); show("vdupq_n_s32", &r, sizeof r);
}

static void t_vmovq_n_s32(void)
{
    __auto_type r = vmovq_n_s32(3); show("vmovq_n_s32", &r, sizeof r);
}

static void t_vld1q_s32(void)
{
    __auto_type r = vld1q_s32((const int *)(pool + 0)); show("vld1q_s32", &r, sizeof r);
}

static void t_vst1q_s32(void)
{
    memset(out, 0, sizeof out);
    int32x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    vst1q_s32((int *)out, a1);
    show("vst1q_s32", out, sizeof out);
}

static void t_vgetq_lane_s32(void)
{
    int32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vgetq_lane_s32(a0, 3); show("vgetq_lane_s32", &r, sizeof r);
}

static void t_vsetq_lane_s32(void)
{
    int32x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vsetq_lane_s32(3, a1, 3); show("vsetq_lane_s32", &r, sizeof r);
}

static void t_vdupq_n_u64(void)
{
    unsigned long long a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vdupq_n_u64(a0); show("vdupq_n_u64", &r, sizeof r);
}

static void t_vmovq_n_u64(void)
{
    unsigned long long a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vmovq_n_u64(a0); show("vmovq_n_u64", &r, sizeof r);
}

static void t_vld1q_u64(void)
{
    __auto_type r = vld1q_u64((const unsigned long long *)(pool + 0)); show("vld1q_u64", &r, sizeof r);
}

static void t_vst1q_u64(void)
{
    memset(out, 0, sizeof out);
    uint64x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    vst1q_u64((unsigned long long *)out, a1);
    show("vst1q_u64", out, sizeof out);
}

static void t_vgetq_lane_u64(void)
{
    uint64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vgetq_lane_u64(a0, 1); show("vgetq_lane_u64", &r, sizeof r);
}

static void t_vsetq_lane_u64(void)
{
    unsigned long long a0; memcpy(&a0, pool + 0, sizeof a0);
    uint64x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vsetq_lane_u64(a0, a1, 1); show("vsetq_lane_u64", &r, sizeof r);
}

static void t_vdupq_n_s64(void)
{
    long long a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vdupq_n_s64(a0); show("vdupq_n_s64", &r, sizeof r);
}

static void t_vmovq_n_s64(void)
{
    long long a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vmovq_n_s64(a0); show("vmovq_n_s64", &r, sizeof r);
}

static void t_vld1q_s64(void)
{
    __auto_type r = vld1q_s64((const long long *)(pool + 0)); show("vld1q_s64", &r, sizeof r);
}

static void t_vst1q_s64(void)
{
    memset(out, 0, sizeof out);
    int64x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    vst1q_s64((long long *)out, a1);
    show("vst1q_s64", out, sizeof out);
}

static void t_vgetq_lane_s64(void)
{
    int64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vgetq_lane_s64(a0, 1); show("vgetq_lane_s64", &r, sizeof r);
}

static void t_vsetq_lane_s64(void)
{
    long long a0; memcpy(&a0, pool + 0, sizeof a0);
    int64x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vsetq_lane_s64(a0, a1, 1); show("vsetq_lane_s64", &r, sizeof r);
}

static void t_vdupq_n_f32(void)
{
    float a0; memcpy(&a0, fpool + 0, sizeof a0);
    __auto_type r = vdupq_n_f32(a0); show("vdupq_n_f32", &r, sizeof r);
}

static void t_vmovq_n_f32(void)
{
    float a0; memcpy(&a0, fpool + 0, sizeof a0);
    __auto_type r = vmovq_n_f32(a0); show("vmovq_n_f32", &r, sizeof r);
}

static void t_vld1q_f32(void)
{
    __auto_type r = vld1q_f32((const float *)(fpool + 0)); show("vld1q_f32", &r, sizeof r);
}

static void t_vst1q_f32(void)
{
    memset(out, 0, sizeof out);
    float32x4_t a1; memcpy(&a1, fpool + 32, sizeof a1);
    vst1q_f32((float *)out, a1);
    show("vst1q_f32", out, sizeof out);
}

static void t_vgetq_lane_f32(void)
{
    float32x4_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    __auto_type r = vgetq_lane_f32(a0, 3); show("vgetq_lane_f32", &r, sizeof r);
}

static void t_vsetq_lane_f32(void)
{
    float a0; memcpy(&a0, fpool + 0, sizeof a0);
    float32x4_t a1; memcpy(&a1, fpool + 32, sizeof a1);
    __auto_type r = vsetq_lane_f32(a0, a1, 3); show("vsetq_lane_f32", &r, sizeof r);
}

static void t_vdupq_n_f64(void)
{
    double a0; memcpy(&a0, fpool + 0, sizeof a0);
    __auto_type r = vdupq_n_f64(a0); show("vdupq_n_f64", &r, sizeof r);
}

static void t_vmovq_n_f64(void)
{
    double a0; memcpy(&a0, fpool + 0, sizeof a0);
    __auto_type r = vmovq_n_f64(a0); show("vmovq_n_f64", &r, sizeof r);
}

static void t_vld1q_f64(void)
{
    __auto_type r = vld1q_f64((const double *)(pool + 0)); show("vld1q_f64", &r, sizeof r);
}

static void t_vst1q_f64(void)
{
    memset(out, 0, sizeof out);
    float64x2_t a1; memcpy(&a1, fpool + 32, sizeof a1);
    vst1q_f64((double *)out, a1);
    show("vst1q_f64", out, sizeof out);
}

static void t_vgetq_lane_f64(void)
{
    float64x2_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    __auto_type r = vgetq_lane_f64(a0, 1); show("vgetq_lane_f64", &r, sizeof r);
}

static void t_vsetq_lane_f64(void)
{
    double a0; memcpy(&a0, fpool + 0, sizeof a0);
    float64x2_t a1; memcpy(&a1, fpool + 32, sizeof a1);
    __auto_type r = vsetq_lane_f64(a0, a1, 1); show("vsetq_lane_f64", &r, sizeof r);
}

static void t_vld1q_p8(void)
{
    __auto_type r = vld1q_p8((const unsigned char *)(pool + 0)); show("vld1q_p8", &r, sizeof r);
}

static void t_vst1q_p8(void)
{
    memset(out, 0, sizeof out);
    poly8x16_t a1; memcpy(&a1, pool + 32, sizeof a1);
    vst1q_p8((unsigned char *)out, a1);
    show("vst1q_p8", out, sizeof out);
}

static void t_vgetq_lane_p8(void)
{
    poly8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vgetq_lane_p8(a0, 15); show("vgetq_lane_p8", &r, sizeof r);
}

static void t_vsetq_lane_p8(void)
{
    unsigned char a0; memcpy(&a0, pool + 0, sizeof a0);
    poly8x16_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vsetq_lane_p8(a0, a1, 15); show("vsetq_lane_p8", &r, sizeof r);
}

static void t_vld1q_p16(void)
{
    __auto_type r = vld1q_p16((const unsigned short *)(pool + 0)); show("vld1q_p16", &r, sizeof r);
}

static void t_vst1q_p16(void)
{
    memset(out, 0, sizeof out);
    poly16x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    vst1q_p16((unsigned short *)out, a1);
    show("vst1q_p16", out, sizeof out);
}

static void t_vgetq_lane_p16(void)
{
    poly16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vgetq_lane_p16(a0, 7); show("vgetq_lane_p16", &r, sizeof r);
}

static void t_vsetq_lane_p16(void)
{
    unsigned short a0; memcpy(&a0, pool + 0, sizeof a0);
    poly16x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vsetq_lane_p16(a0, a1, 7); show("vsetq_lane_p16", &r, sizeof r);
}

static void t_vld1q_p64(void)
{
    __auto_type r = vld1q_p64((const unsigned long long *)(pool + 0)); show("vld1q_p64", &r, sizeof r);
}

static void t_vst1q_p64(void)
{
    memset(out, 0, sizeof out);
    poly64x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    vst1q_p64((unsigned long long *)out, a1);
    show("vst1q_p64", out, sizeof out);
}

static void t_vgetq_lane_p64(void)
{
    poly64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vgetq_lane_p64(a0, 1); show("vgetq_lane_p64", &r, sizeof r);
}

static void t_vsetq_lane_p64(void)
{
    unsigned long long a0; memcpy(&a0, pool + 0, sizeof a0);
    poly64x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vsetq_lane_p64(a0, a1, 1); show("vsetq_lane_p64", &r, sizeof r);
}

static void t_vaddq_u8(void)
{
    uint8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint8x16_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vaddq_u8(a0, a1); show("vaddq_u8", &r, sizeof r);
}

static void t_vsubq_u8(void)
{
    uint8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint8x16_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vsubq_u8(a0, a1); show("vsubq_u8", &r, sizeof r);
}

static void t_vmulq_u8(void)
{
    uint8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint8x16_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vmulq_u8(a0, a1); show("vmulq_u8", &r, sizeof r);
}

static void t_vceqq_u8(void)
{
    uint8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint8x16_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vceqq_u8(a0, a1); show("vceqq_u8", &r, sizeof r);
}

static void t_vcgeq_u8(void)
{
    uint8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint8x16_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vcgeq_u8(a0, a1); show("vcgeq_u8", &r, sizeof r);
}

static void t_vcgtq_u8(void)
{
    uint8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint8x16_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vcgtq_u8(a0, a1); show("vcgtq_u8", &r, sizeof r);
}

static void t_vcleq_u8(void)
{
    uint8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint8x16_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vcleq_u8(a0, a1); show("vcleq_u8", &r, sizeof r);
}

static void t_vcltq_u8(void)
{
    uint8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint8x16_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vcltq_u8(a0, a1); show("vcltq_u8", &r, sizeof r);
}

static void t_vbslq_u8(void)
{
    uint8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint8x16_t a1; memcpy(&a1, pool + 32, sizeof a1);
    uint8x16_t a2; memcpy(&a2, pool + 64, sizeof a2);
    __auto_type r = vbslq_u8(a0, a1, a2); show("vbslq_u8", &r, sizeof r);
}

static void t_vaddq_s8(void)
{
    int8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int8x16_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vaddq_s8(a0, a1); show("vaddq_s8", &r, sizeof r);
}

static void t_vsubq_s8(void)
{
    int8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int8x16_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vsubq_s8(a0, a1); show("vsubq_s8", &r, sizeof r);
}

static void t_vmulq_s8(void)
{
    int8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int8x16_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vmulq_s8(a0, a1); show("vmulq_s8", &r, sizeof r);
}

static void t_vceqq_s8(void)
{
    int8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int8x16_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vceqq_s8(a0, a1); show("vceqq_s8", &r, sizeof r);
}

static void t_vcgeq_s8(void)
{
    int8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int8x16_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vcgeq_s8(a0, a1); show("vcgeq_s8", &r, sizeof r);
}

static void t_vcgtq_s8(void)
{
    int8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int8x16_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vcgtq_s8(a0, a1); show("vcgtq_s8", &r, sizeof r);
}

static void t_vcleq_s8(void)
{
    int8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int8x16_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vcleq_s8(a0, a1); show("vcleq_s8", &r, sizeof r);
}

static void t_vcltq_s8(void)
{
    int8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int8x16_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vcltq_s8(a0, a1); show("vcltq_s8", &r, sizeof r);
}

static void t_vnegq_s8(void)
{
    int8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vnegq_s8(a0); show("vnegq_s8", &r, sizeof r);
}

static void t_vbslq_s8(void)
{
    uint8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int8x16_t a1; memcpy(&a1, pool + 32, sizeof a1);
    int8x16_t a2; memcpy(&a2, pool + 64, sizeof a2);
    __auto_type r = vbslq_s8(a0, a1, a2); show("vbslq_s8", &r, sizeof r);
}

static void t_vaddq_u16(void)
{
    uint16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint16x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vaddq_u16(a0, a1); show("vaddq_u16", &r, sizeof r);
}

static void t_vsubq_u16(void)
{
    uint16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint16x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vsubq_u16(a0, a1); show("vsubq_u16", &r, sizeof r);
}

static void t_vmulq_u16(void)
{
    uint16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint16x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vmulq_u16(a0, a1); show("vmulq_u16", &r, sizeof r);
}

static void t_vceqq_u16(void)
{
    uint16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint16x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vceqq_u16(a0, a1); show("vceqq_u16", &r, sizeof r);
}

static void t_vcgeq_u16(void)
{
    uint16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint16x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vcgeq_u16(a0, a1); show("vcgeq_u16", &r, sizeof r);
}

static void t_vcgtq_u16(void)
{
    uint16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint16x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vcgtq_u16(a0, a1); show("vcgtq_u16", &r, sizeof r);
}

static void t_vcleq_u16(void)
{
    uint16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint16x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vcleq_u16(a0, a1); show("vcleq_u16", &r, sizeof r);
}

static void t_vcltq_u16(void)
{
    uint16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint16x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vcltq_u16(a0, a1); show("vcltq_u16", &r, sizeof r);
}

static void t_vbslq_u16(void)
{
    uint16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint16x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    uint16x8_t a2; memcpy(&a2, pool + 64, sizeof a2);
    __auto_type r = vbslq_u16(a0, a1, a2); show("vbslq_u16", &r, sizeof r);
}

static void t_vaddq_s16(void)
{
    int16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int16x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vaddq_s16(a0, a1); show("vaddq_s16", &r, sizeof r);
}

static void t_vsubq_s16(void)
{
    int16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int16x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vsubq_s16(a0, a1); show("vsubq_s16", &r, sizeof r);
}

static void t_vmulq_s16(void)
{
    int16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int16x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vmulq_s16(a0, a1); show("vmulq_s16", &r, sizeof r);
}

static void t_vceqq_s16(void)
{
    int16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int16x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vceqq_s16(a0, a1); show("vceqq_s16", &r, sizeof r);
}

static void t_vcgeq_s16(void)
{
    int16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int16x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vcgeq_s16(a0, a1); show("vcgeq_s16", &r, sizeof r);
}

static void t_vcgtq_s16(void)
{
    int16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int16x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vcgtq_s16(a0, a1); show("vcgtq_s16", &r, sizeof r);
}

static void t_vcleq_s16(void)
{
    int16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int16x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vcleq_s16(a0, a1); show("vcleq_s16", &r, sizeof r);
}

static void t_vcltq_s16(void)
{
    int16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int16x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vcltq_s16(a0, a1); show("vcltq_s16", &r, sizeof r);
}

static void t_vnegq_s16(void)
{
    int16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vnegq_s16(a0); show("vnegq_s16", &r, sizeof r);
}

static void t_vbslq_s16(void)
{
    uint16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int16x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    int16x8_t a2; memcpy(&a2, pool + 64, sizeof a2);
    __auto_type r = vbslq_s16(a0, a1, a2); show("vbslq_s16", &r, sizeof r);
}

static void t_vaddq_u32(void)
{
    uint32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint32x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vaddq_u32(a0, a1); show("vaddq_u32", &r, sizeof r);
}

static void t_vsubq_u32(void)
{
    uint32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint32x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vsubq_u32(a0, a1); show("vsubq_u32", &r, sizeof r);
}

static void t_vmulq_u32(void)
{
    uint32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint32x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vmulq_u32(a0, a1); show("vmulq_u32", &r, sizeof r);
}

static void t_vceqq_u32(void)
{
    uint32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint32x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vceqq_u32(a0, a1); show("vceqq_u32", &r, sizeof r);
}

static void t_vcgeq_u32(void)
{
    uint32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint32x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vcgeq_u32(a0, a1); show("vcgeq_u32", &r, sizeof r);
}

static void t_vcgtq_u32(void)
{
    uint32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint32x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vcgtq_u32(a0, a1); show("vcgtq_u32", &r, sizeof r);
}

static void t_vcleq_u32(void)
{
    uint32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint32x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vcleq_u32(a0, a1); show("vcleq_u32", &r, sizeof r);
}

static void t_vcltq_u32(void)
{
    uint32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint32x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vcltq_u32(a0, a1); show("vcltq_u32", &r, sizeof r);
}

static void t_vbslq_u32(void)
{
    uint32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint32x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    uint32x4_t a2; memcpy(&a2, pool + 64, sizeof a2);
    __auto_type r = vbslq_u32(a0, a1, a2); show("vbslq_u32", &r, sizeof r);
}

static void t_vaddq_s32(void)
{
    int32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int32x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vaddq_s32(a0, a1); show("vaddq_s32", &r, sizeof r);
}

static void t_vsubq_s32(void)
{
    int32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int32x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vsubq_s32(a0, a1); show("vsubq_s32", &r, sizeof r);
}

static void t_vmulq_s32(void)
{
    int32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int32x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vmulq_s32(a0, a1); show("vmulq_s32", &r, sizeof r);
}

static void t_vceqq_s32(void)
{
    int32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int32x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vceqq_s32(a0, a1); show("vceqq_s32", &r, sizeof r);
}

static void t_vcgeq_s32(void)
{
    int32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int32x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vcgeq_s32(a0, a1); show("vcgeq_s32", &r, sizeof r);
}

static void t_vcgtq_s32(void)
{
    int32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int32x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vcgtq_s32(a0, a1); show("vcgtq_s32", &r, sizeof r);
}

static void t_vcleq_s32(void)
{
    int32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int32x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vcleq_s32(a0, a1); show("vcleq_s32", &r, sizeof r);
}

static void t_vcltq_s32(void)
{
    int32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int32x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vcltq_s32(a0, a1); show("vcltq_s32", &r, sizeof r);
}

static void t_vnegq_s32(void)
{
    int32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vnegq_s32(a0); show("vnegq_s32", &r, sizeof r);
}

static void t_vbslq_s32(void)
{
    uint32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int32x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    int32x4_t a2; memcpy(&a2, pool + 64, sizeof a2);
    __auto_type r = vbslq_s32(a0, a1, a2); show("vbslq_s32", &r, sizeof r);
}

static void t_vaddq_u64(void)
{
    uint64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint64x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vaddq_u64(a0, a1); show("vaddq_u64", &r, sizeof r);
}

static void t_vsubq_u64(void)
{
    uint64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint64x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vsubq_u64(a0, a1); show("vsubq_u64", &r, sizeof r);
}

static void t_vceqq_u64(void)
{
    uint64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint64x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vceqq_u64(a0, a1); show("vceqq_u64", &r, sizeof r);
}

static void t_vcgeq_u64(void)
{
    uint64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint64x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vcgeq_u64(a0, a1); show("vcgeq_u64", &r, sizeof r);
}

static void t_vcgtq_u64(void)
{
    uint64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint64x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vcgtq_u64(a0, a1); show("vcgtq_u64", &r, sizeof r);
}

static void t_vcleq_u64(void)
{
    uint64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint64x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vcleq_u64(a0, a1); show("vcleq_u64", &r, sizeof r);
}

static void t_vcltq_u64(void)
{
    uint64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint64x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vcltq_u64(a0, a1); show("vcltq_u64", &r, sizeof r);
}

static void t_vbslq_u64(void)
{
    uint64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint64x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    uint64x2_t a2; memcpy(&a2, pool + 64, sizeof a2);
    __auto_type r = vbslq_u64(a0, a1, a2); show("vbslq_u64", &r, sizeof r);
}

static void t_vaddq_s64(void)
{
    int64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int64x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vaddq_s64(a0, a1); show("vaddq_s64", &r, sizeof r);
}

static void t_vsubq_s64(void)
{
    int64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int64x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vsubq_s64(a0, a1); show("vsubq_s64", &r, sizeof r);
}

static void t_vceqq_s64(void)
{
    int64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int64x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vceqq_s64(a0, a1); show("vceqq_s64", &r, sizeof r);
}

static void t_vcgeq_s64(void)
{
    int64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int64x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vcgeq_s64(a0, a1); show("vcgeq_s64", &r, sizeof r);
}

static void t_vcgtq_s64(void)
{
    int64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int64x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vcgtq_s64(a0, a1); show("vcgtq_s64", &r, sizeof r);
}

static void t_vcleq_s64(void)
{
    int64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int64x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vcleq_s64(a0, a1); show("vcleq_s64", &r, sizeof r);
}

static void t_vcltq_s64(void)
{
    int64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int64x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vcltq_s64(a0, a1); show("vcltq_s64", &r, sizeof r);
}

static void t_vnegq_s64(void)
{
    int64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vnegq_s64(a0); show("vnegq_s64", &r, sizeof r);
}

static void t_vbslq_s64(void)
{
    uint64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int64x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    int64x2_t a2; memcpy(&a2, pool + 64, sizeof a2);
    __auto_type r = vbslq_s64(a0, a1, a2); show("vbslq_s64", &r, sizeof r);
}

static void t_vaddq_f32(void)
{
    float32x4_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    float32x4_t a1; memcpy(&a1, fpool + 32, sizeof a1);
    __auto_type r = vaddq_f32(a0, a1); show("vaddq_f32", &r, sizeof r);
}

static void t_vsubq_f32(void)
{
    float32x4_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    float32x4_t a1; memcpy(&a1, fpool + 32, sizeof a1);
    __auto_type r = vsubq_f32(a0, a1); show("vsubq_f32", &r, sizeof r);
}

static void t_vmulq_f32(void)
{
    float32x4_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    float32x4_t a1; memcpy(&a1, fpool + 32, sizeof a1);
    __auto_type r = vmulq_f32(a0, a1); show("vmulq_f32", &r, sizeof r);
}

static void t_vdivq_f32(void)
{
    float32x4_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    float32x4_t a1; memcpy(&a1, fpool + 32, sizeof a1);
    __auto_type r = vdivq_f32(a0, a1); show("vdivq_f32", &r, sizeof r);
}

static void t_vceqq_f32(void)
{
    float32x4_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    float32x4_t a1; memcpy(&a1, fpool + 32, sizeof a1);
    __auto_type r = vceqq_f32(a0, a1); show("vceqq_f32", &r, sizeof r);
}

static void t_vcgeq_f32(void)
{
    float32x4_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    float32x4_t a1; memcpy(&a1, fpool + 32, sizeof a1);
    __auto_type r = vcgeq_f32(a0, a1); show("vcgeq_f32", &r, sizeof r);
}

static void t_vcgtq_f32(void)
{
    float32x4_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    float32x4_t a1; memcpy(&a1, fpool + 32, sizeof a1);
    __auto_type r = vcgtq_f32(a0, a1); show("vcgtq_f32", &r, sizeof r);
}

static void t_vcleq_f32(void)
{
    float32x4_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    float32x4_t a1; memcpy(&a1, fpool + 32, sizeof a1);
    __auto_type r = vcleq_f32(a0, a1); show("vcleq_f32", &r, sizeof r);
}

static void t_vcltq_f32(void)
{
    float32x4_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    float32x4_t a1; memcpy(&a1, fpool + 32, sizeof a1);
    __auto_type r = vcltq_f32(a0, a1); show("vcltq_f32", &r, sizeof r);
}

static void t_vnegq_f32(void)
{
    float32x4_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    __auto_type r = vnegq_f32(a0); show("vnegq_f32", &r, sizeof r);
}

static void t_vbslq_f32(void)
{
    uint32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    float32x4_t a1; memcpy(&a1, fpool + 32, sizeof a1);
    float32x4_t a2; memcpy(&a2, fpool + 64, sizeof a2);
    __auto_type r = vbslq_f32(a0, a1, a2); show("vbslq_f32", &r, sizeof r);
}

static void t_vaddq_f64(void)
{
    float64x2_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    float64x2_t a1; memcpy(&a1, fpool + 32, sizeof a1);
    __auto_type r = vaddq_f64(a0, a1); show("vaddq_f64", &r, sizeof r);
}

static void t_vsubq_f64(void)
{
    float64x2_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    float64x2_t a1; memcpy(&a1, fpool + 32, sizeof a1);
    __auto_type r = vsubq_f64(a0, a1); show("vsubq_f64", &r, sizeof r);
}

static void t_vmulq_f64(void)
{
    float64x2_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    float64x2_t a1; memcpy(&a1, fpool + 32, sizeof a1);
    __auto_type r = vmulq_f64(a0, a1); show("vmulq_f64", &r, sizeof r);
}

static void t_vdivq_f64(void)
{
    float64x2_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    float64x2_t a1; memcpy(&a1, fpool + 32, sizeof a1);
    __auto_type r = vdivq_f64(a0, a1); show("vdivq_f64", &r, sizeof r);
}

static void t_vceqq_f64(void)
{
    float64x2_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    float64x2_t a1; memcpy(&a1, fpool + 32, sizeof a1);
    __auto_type r = vceqq_f64(a0, a1); show("vceqq_f64", &r, sizeof r);
}

static void t_vcgeq_f64(void)
{
    float64x2_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    float64x2_t a1; memcpy(&a1, fpool + 32, sizeof a1);
    __auto_type r = vcgeq_f64(a0, a1); show("vcgeq_f64", &r, sizeof r);
}

static void t_vcgtq_f64(void)
{
    float64x2_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    float64x2_t a1; memcpy(&a1, fpool + 32, sizeof a1);
    __auto_type r = vcgtq_f64(a0, a1); show("vcgtq_f64", &r, sizeof r);
}

static void t_vcleq_f64(void)
{
    float64x2_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    float64x2_t a1; memcpy(&a1, fpool + 32, sizeof a1);
    __auto_type r = vcleq_f64(a0, a1); show("vcleq_f64", &r, sizeof r);
}

static void t_vcltq_f64(void)
{
    float64x2_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    float64x2_t a1; memcpy(&a1, fpool + 32, sizeof a1);
    __auto_type r = vcltq_f64(a0, a1); show("vcltq_f64", &r, sizeof r);
}

static void t_vnegq_f64(void)
{
    float64x2_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    __auto_type r = vnegq_f64(a0); show("vnegq_f64", &r, sizeof r);
}

static void t_vbslq_f64(void)
{
    uint64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    float64x2_t a1; memcpy(&a1, fpool + 32, sizeof a1);
    float64x2_t a2; memcpy(&a2, fpool + 64, sizeof a2);
    __auto_type r = vbslq_f64(a0, a1, a2); show("vbslq_f64", &r, sizeof r);
}

static void t_vandq_u8(void)
{
    uint8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint8x16_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vandq_u8(a0, a1); show("vandq_u8", &r, sizeof r);
}

static void t_vorrq_u8(void)
{
    uint8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint8x16_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vorrq_u8(a0, a1); show("vorrq_u8", &r, sizeof r);
}

static void t_veorq_u8(void)
{
    uint8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint8x16_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = veorq_u8(a0, a1); show("veorq_u8", &r, sizeof r);
}

static void t_vbicq_u8(void)
{
    uint8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint8x16_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vbicq_u8(a0, a1); show("vbicq_u8", &r, sizeof r);
}

static void t_vornq_u8(void)
{
    uint8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint8x16_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vornq_u8(a0, a1); show("vornq_u8", &r, sizeof r);
}

static void t_vtstq_u8(void)
{
    uint8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint8x16_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vtstq_u8(a0, a1); show("vtstq_u8", &r, sizeof r);
}

static void t_vmvnq_u8(void)
{
    uint8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vmvnq_u8(a0); show("vmvnq_u8", &r, sizeof r);
}

static void t_vmlaq_u8(void)
{
    uint8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint8x16_t a1; memcpy(&a1, pool + 32, sizeof a1);
    uint8x16_t a2; memcpy(&a2, pool + 64, sizeof a2);
    __auto_type r = vmlaq_u8(a0, a1, a2); show("vmlaq_u8", &r, sizeof r);
}

static void t_vmlsq_u8(void)
{
    uint8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint8x16_t a1; memcpy(&a1, pool + 32, sizeof a1);
    uint8x16_t a2; memcpy(&a2, pool + 64, sizeof a2);
    __auto_type r = vmlsq_u8(a0, a1, a2); show("vmlsq_u8", &r, sizeof r);
}

static void t_vshlq_n_u8(void)
{
    uint8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vshlq_n_u8(a0, 3); show("vshlq_n_u8", &r, sizeof r);
}

static void t_vshrq_n_u8(void)
{
    uint8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vshrq_n_u8(a0, 3); show("vshrq_n_u8", &r, sizeof r);
}

static void t_vandq_s8(void)
{
    int8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int8x16_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vandq_s8(a0, a1); show("vandq_s8", &r, sizeof r);
}

static void t_vorrq_s8(void)
{
    int8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int8x16_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vorrq_s8(a0, a1); show("vorrq_s8", &r, sizeof r);
}

static void t_veorq_s8(void)
{
    int8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int8x16_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = veorq_s8(a0, a1); show("veorq_s8", &r, sizeof r);
}

static void t_vbicq_s8(void)
{
    int8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int8x16_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vbicq_s8(a0, a1); show("vbicq_s8", &r, sizeof r);
}

static void t_vornq_s8(void)
{
    int8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int8x16_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vornq_s8(a0, a1); show("vornq_s8", &r, sizeof r);
}

static void t_vtstq_s8(void)
{
    int8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int8x16_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vtstq_s8(a0, a1); show("vtstq_s8", &r, sizeof r);
}

static void t_vmvnq_s8(void)
{
    int8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vmvnq_s8(a0); show("vmvnq_s8", &r, sizeof r);
}

static void t_vmlaq_s8(void)
{
    int8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int8x16_t a1; memcpy(&a1, pool + 32, sizeof a1);
    int8x16_t a2; memcpy(&a2, pool + 64, sizeof a2);
    __auto_type r = vmlaq_s8(a0, a1, a2); show("vmlaq_s8", &r, sizeof r);
}

static void t_vmlsq_s8(void)
{
    int8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int8x16_t a1; memcpy(&a1, pool + 32, sizeof a1);
    int8x16_t a2; memcpy(&a2, pool + 64, sizeof a2);
    __auto_type r = vmlsq_s8(a0, a1, a2); show("vmlsq_s8", &r, sizeof r);
}

static void t_vshlq_n_s8(void)
{
    int8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vshlq_n_s8(a0, 3); show("vshlq_n_s8", &r, sizeof r);
}

static void t_vshrq_n_s8(void)
{
    int8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vshrq_n_s8(a0, 3); show("vshrq_n_s8", &r, sizeof r);
}

static void t_vandq_u16(void)
{
    uint16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint16x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vandq_u16(a0, a1); show("vandq_u16", &r, sizeof r);
}

static void t_vorrq_u16(void)
{
    uint16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint16x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vorrq_u16(a0, a1); show("vorrq_u16", &r, sizeof r);
}

static void t_veorq_u16(void)
{
    uint16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint16x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = veorq_u16(a0, a1); show("veorq_u16", &r, sizeof r);
}

static void t_vbicq_u16(void)
{
    uint16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint16x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vbicq_u16(a0, a1); show("vbicq_u16", &r, sizeof r);
}

static void t_vornq_u16(void)
{
    uint16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint16x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vornq_u16(a0, a1); show("vornq_u16", &r, sizeof r);
}

static void t_vtstq_u16(void)
{
    uint16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint16x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vtstq_u16(a0, a1); show("vtstq_u16", &r, sizeof r);
}

static void t_vmvnq_u16(void)
{
    uint16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vmvnq_u16(a0); show("vmvnq_u16", &r, sizeof r);
}

static void t_vmlaq_u16(void)
{
    uint16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint16x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    uint16x8_t a2; memcpy(&a2, pool + 64, sizeof a2);
    __auto_type r = vmlaq_u16(a0, a1, a2); show("vmlaq_u16", &r, sizeof r);
}

static void t_vmlsq_u16(void)
{
    uint16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint16x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    uint16x8_t a2; memcpy(&a2, pool + 64, sizeof a2);
    __auto_type r = vmlsq_u16(a0, a1, a2); show("vmlsq_u16", &r, sizeof r);
}

static void t_vshlq_n_u16(void)
{
    uint16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vshlq_n_u16(a0, 3); show("vshlq_n_u16", &r, sizeof r);
}

static void t_vshrq_n_u16(void)
{
    uint16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vshrq_n_u16(a0, 3); show("vshrq_n_u16", &r, sizeof r);
}

static void t_vandq_s16(void)
{
    int16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int16x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vandq_s16(a0, a1); show("vandq_s16", &r, sizeof r);
}

static void t_vorrq_s16(void)
{
    int16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int16x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vorrq_s16(a0, a1); show("vorrq_s16", &r, sizeof r);
}

static void t_veorq_s16(void)
{
    int16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int16x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = veorq_s16(a0, a1); show("veorq_s16", &r, sizeof r);
}

static void t_vbicq_s16(void)
{
    int16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int16x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vbicq_s16(a0, a1); show("vbicq_s16", &r, sizeof r);
}

static void t_vornq_s16(void)
{
    int16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int16x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vornq_s16(a0, a1); show("vornq_s16", &r, sizeof r);
}

static void t_vtstq_s16(void)
{
    int16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int16x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vtstq_s16(a0, a1); show("vtstq_s16", &r, sizeof r);
}

static void t_vmvnq_s16(void)
{
    int16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vmvnq_s16(a0); show("vmvnq_s16", &r, sizeof r);
}

static void t_vmlaq_s16(void)
{
    int16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int16x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    int16x8_t a2; memcpy(&a2, pool + 64, sizeof a2);
    __auto_type r = vmlaq_s16(a0, a1, a2); show("vmlaq_s16", &r, sizeof r);
}

static void t_vmlsq_s16(void)
{
    int16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int16x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    int16x8_t a2; memcpy(&a2, pool + 64, sizeof a2);
    __auto_type r = vmlsq_s16(a0, a1, a2); show("vmlsq_s16", &r, sizeof r);
}

static void t_vshlq_n_s16(void)
{
    int16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vshlq_n_s16(a0, 3); show("vshlq_n_s16", &r, sizeof r);
}

static void t_vshrq_n_s16(void)
{
    int16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vshrq_n_s16(a0, 3); show("vshrq_n_s16", &r, sizeof r);
}

static void t_vandq_u32(void)
{
    uint32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint32x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vandq_u32(a0, a1); show("vandq_u32", &r, sizeof r);
}

static void t_vorrq_u32(void)
{
    uint32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint32x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vorrq_u32(a0, a1); show("vorrq_u32", &r, sizeof r);
}

static void t_veorq_u32(void)
{
    uint32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint32x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = veorq_u32(a0, a1); show("veorq_u32", &r, sizeof r);
}

static void t_vbicq_u32(void)
{
    uint32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint32x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vbicq_u32(a0, a1); show("vbicq_u32", &r, sizeof r);
}

static void t_vornq_u32(void)
{
    uint32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint32x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vornq_u32(a0, a1); show("vornq_u32", &r, sizeof r);
}

static void t_vtstq_u32(void)
{
    uint32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint32x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vtstq_u32(a0, a1); show("vtstq_u32", &r, sizeof r);
}

static void t_vmvnq_u32(void)
{
    uint32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vmvnq_u32(a0); show("vmvnq_u32", &r, sizeof r);
}

static void t_vmlaq_u32(void)
{
    uint32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint32x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    uint32x4_t a2; memcpy(&a2, pool + 64, sizeof a2);
    __auto_type r = vmlaq_u32(a0, a1, a2); show("vmlaq_u32", &r, sizeof r);
}

static void t_vmlsq_u32(void)
{
    uint32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint32x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    uint32x4_t a2; memcpy(&a2, pool + 64, sizeof a2);
    __auto_type r = vmlsq_u32(a0, a1, a2); show("vmlsq_u32", &r, sizeof r);
}

static void t_vshlq_n_u32(void)
{
    uint32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vshlq_n_u32(a0, 3); show("vshlq_n_u32", &r, sizeof r);
}

static void t_vshrq_n_u32(void)
{
    uint32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vshrq_n_u32(a0, 3); show("vshrq_n_u32", &r, sizeof r);
}

static void t_vandq_s32(void)
{
    int32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int32x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vandq_s32(a0, a1); show("vandq_s32", &r, sizeof r);
}

static void t_vorrq_s32(void)
{
    int32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int32x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vorrq_s32(a0, a1); show("vorrq_s32", &r, sizeof r);
}

static void t_veorq_s32(void)
{
    int32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int32x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = veorq_s32(a0, a1); show("veorq_s32", &r, sizeof r);
}

static void t_vbicq_s32(void)
{
    int32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int32x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vbicq_s32(a0, a1); show("vbicq_s32", &r, sizeof r);
}

static void t_vornq_s32(void)
{
    int32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int32x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vornq_s32(a0, a1); show("vornq_s32", &r, sizeof r);
}

static void t_vtstq_s32(void)
{
    int32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int32x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vtstq_s32(a0, a1); show("vtstq_s32", &r, sizeof r);
}

static void t_vmvnq_s32(void)
{
    int32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vmvnq_s32(a0); show("vmvnq_s32", &r, sizeof r);
}

static void t_vmlaq_s32(void)
{
    int32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int32x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    int32x4_t a2; memcpy(&a2, pool + 64, sizeof a2);
    __auto_type r = vmlaq_s32(a0, a1, a2); show("vmlaq_s32", &r, sizeof r);
}

static void t_vmlsq_s32(void)
{
    int32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int32x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    int32x4_t a2; memcpy(&a2, pool + 64, sizeof a2);
    __auto_type r = vmlsq_s32(a0, a1, a2); show("vmlsq_s32", &r, sizeof r);
}

static void t_vshlq_n_s32(void)
{
    int32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vshlq_n_s32(a0, 3); show("vshlq_n_s32", &r, sizeof r);
}

static void t_vshrq_n_s32(void)
{
    int32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vshrq_n_s32(a0, 3); show("vshrq_n_s32", &r, sizeof r);
}

static void t_vandq_u64(void)
{
    uint64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint64x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vandq_u64(a0, a1); show("vandq_u64", &r, sizeof r);
}

static void t_vorrq_u64(void)
{
    uint64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint64x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vorrq_u64(a0, a1); show("vorrq_u64", &r, sizeof r);
}

static void t_veorq_u64(void)
{
    uint64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint64x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = veorq_u64(a0, a1); show("veorq_u64", &r, sizeof r);
}

static void t_vbicq_u64(void)
{
    uint64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint64x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vbicq_u64(a0, a1); show("vbicq_u64", &r, sizeof r);
}

static void t_vornq_u64(void)
{
    uint64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint64x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vornq_u64(a0, a1); show("vornq_u64", &r, sizeof r);
}

static void t_vtstq_u64(void)
{
    uint64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint64x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vtstq_u64(a0, a1); show("vtstq_u64", &r, sizeof r);
}

static void t_vshlq_n_u64(void)
{
    uint64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vshlq_n_u64(a0, 3); show("vshlq_n_u64", &r, sizeof r);
}

static void t_vshrq_n_u64(void)
{
    uint64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vshrq_n_u64(a0, 3); show("vshrq_n_u64", &r, sizeof r);
}

static void t_vandq_s64(void)
{
    int64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int64x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vandq_s64(a0, a1); show("vandq_s64", &r, sizeof r);
}

static void t_vorrq_s64(void)
{
    int64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int64x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vorrq_s64(a0, a1); show("vorrq_s64", &r, sizeof r);
}

static void t_veorq_s64(void)
{
    int64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int64x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = veorq_s64(a0, a1); show("veorq_s64", &r, sizeof r);
}

static void t_vbicq_s64(void)
{
    int64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int64x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vbicq_s64(a0, a1); show("vbicq_s64", &r, sizeof r);
}

static void t_vornq_s64(void)
{
    int64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int64x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vornq_s64(a0, a1); show("vornq_s64", &r, sizeof r);
}

static void t_vtstq_s64(void)
{
    int64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int64x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vtstq_s64(a0, a1); show("vtstq_s64", &r, sizeof r);
}

static void t_vshlq_n_s64(void)
{
    int64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vshlq_n_s64(a0, 3); show("vshlq_n_s64", &r, sizeof r);
}

static void t_vshrq_n_s64(void)
{
    int64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vshrq_n_s64(a0, 3); show("vshrq_n_s64", &r, sizeof r);
}

static void t_vreinterpretq_u8_s8(void)
{
    int8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_u8_s8(a0); show("vreinterpretq_u8_s8", &r, sizeof r);
}

static void t_vreinterpretq_u8_u16(void)
{
    uint16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_u8_u16(a0); show("vreinterpretq_u8_u16", &r, sizeof r);
}

static void t_vreinterpretq_u8_s16(void)
{
    int16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_u8_s16(a0); show("vreinterpretq_u8_s16", &r, sizeof r);
}

static void t_vreinterpretq_u8_u32(void)
{
    uint32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_u8_u32(a0); show("vreinterpretq_u8_u32", &r, sizeof r);
}

static void t_vreinterpretq_u8_s32(void)
{
    int32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_u8_s32(a0); show("vreinterpretq_u8_s32", &r, sizeof r);
}

static void t_vreinterpretq_u8_u64(void)
{
    uint64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_u8_u64(a0); show("vreinterpretq_u8_u64", &r, sizeof r);
}

static void t_vreinterpretq_u8_s64(void)
{
    int64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_u8_s64(a0); show("vreinterpretq_u8_s64", &r, sizeof r);
}

static void t_vreinterpretq_u8_f32(void)
{
    float32x4_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    __auto_type r = vreinterpretq_u8_f32(a0); show("vreinterpretq_u8_f32", &r, sizeof r);
}

static void t_vreinterpretq_u8_f64(void)
{
    float64x2_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    __auto_type r = vreinterpretq_u8_f64(a0); show("vreinterpretq_u8_f64", &r, sizeof r);
}

static void t_vreinterpretq_u8_p8(void)
{
    poly8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_u8_p8(a0); show("vreinterpretq_u8_p8", &r, sizeof r);
}

static void t_vreinterpretq_u8_p16(void)
{
    poly16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_u8_p16(a0); show("vreinterpretq_u8_p16", &r, sizeof r);
}

static void t_vreinterpretq_u8_p64(void)
{
    poly64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_u8_p64(a0); show("vreinterpretq_u8_p64", &r, sizeof r);
}

static void t_vreinterpretq_s8_u8(void)
{
    uint8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_s8_u8(a0); show("vreinterpretq_s8_u8", &r, sizeof r);
}

static void t_vreinterpretq_s8_u16(void)
{
    uint16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_s8_u16(a0); show("vreinterpretq_s8_u16", &r, sizeof r);
}

static void t_vreinterpretq_s8_s16(void)
{
    int16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_s8_s16(a0); show("vreinterpretq_s8_s16", &r, sizeof r);
}

static void t_vreinterpretq_s8_u32(void)
{
    uint32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_s8_u32(a0); show("vreinterpretq_s8_u32", &r, sizeof r);
}

static void t_vreinterpretq_s8_s32(void)
{
    int32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_s8_s32(a0); show("vreinterpretq_s8_s32", &r, sizeof r);
}

static void t_vreinterpretq_s8_u64(void)
{
    uint64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_s8_u64(a0); show("vreinterpretq_s8_u64", &r, sizeof r);
}

static void t_vreinterpretq_s8_s64(void)
{
    int64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_s8_s64(a0); show("vreinterpretq_s8_s64", &r, sizeof r);
}

static void t_vreinterpretq_s8_f32(void)
{
    float32x4_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    __auto_type r = vreinterpretq_s8_f32(a0); show("vreinterpretq_s8_f32", &r, sizeof r);
}

static void t_vreinterpretq_s8_f64(void)
{
    float64x2_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    __auto_type r = vreinterpretq_s8_f64(a0); show("vreinterpretq_s8_f64", &r, sizeof r);
}

static void t_vreinterpretq_s8_p8(void)
{
    poly8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_s8_p8(a0); show("vreinterpretq_s8_p8", &r, sizeof r);
}

static void t_vreinterpretq_s8_p16(void)
{
    poly16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_s8_p16(a0); show("vreinterpretq_s8_p16", &r, sizeof r);
}

static void t_vreinterpretq_s8_p64(void)
{
    poly64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_s8_p64(a0); show("vreinterpretq_s8_p64", &r, sizeof r);
}

static void t_vreinterpretq_u16_u8(void)
{
    uint8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_u16_u8(a0); show("vreinterpretq_u16_u8", &r, sizeof r);
}

static void t_vreinterpretq_u16_s8(void)
{
    int8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_u16_s8(a0); show("vreinterpretq_u16_s8", &r, sizeof r);
}

static void t_vreinterpretq_u16_s16(void)
{
    int16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_u16_s16(a0); show("vreinterpretq_u16_s16", &r, sizeof r);
}

static void t_vreinterpretq_u16_u32(void)
{
    uint32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_u16_u32(a0); show("vreinterpretq_u16_u32", &r, sizeof r);
}

static void t_vreinterpretq_u16_s32(void)
{
    int32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_u16_s32(a0); show("vreinterpretq_u16_s32", &r, sizeof r);
}

static void t_vreinterpretq_u16_u64(void)
{
    uint64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_u16_u64(a0); show("vreinterpretq_u16_u64", &r, sizeof r);
}

static void t_vreinterpretq_u16_s64(void)
{
    int64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_u16_s64(a0); show("vreinterpretq_u16_s64", &r, sizeof r);
}

static void t_vreinterpretq_u16_f32(void)
{
    float32x4_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    __auto_type r = vreinterpretq_u16_f32(a0); show("vreinterpretq_u16_f32", &r, sizeof r);
}

static void t_vreinterpretq_u16_f64(void)
{
    float64x2_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    __auto_type r = vreinterpretq_u16_f64(a0); show("vreinterpretq_u16_f64", &r, sizeof r);
}

static void t_vreinterpretq_u16_p8(void)
{
    poly8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_u16_p8(a0); show("vreinterpretq_u16_p8", &r, sizeof r);
}

static void t_vreinterpretq_u16_p16(void)
{
    poly16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_u16_p16(a0); show("vreinterpretq_u16_p16", &r, sizeof r);
}

static void t_vreinterpretq_u16_p64(void)
{
    poly64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_u16_p64(a0); show("vreinterpretq_u16_p64", &r, sizeof r);
}

static void t_vreinterpretq_s16_u8(void)
{
    uint8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_s16_u8(a0); show("vreinterpretq_s16_u8", &r, sizeof r);
}

static void t_vreinterpretq_s16_s8(void)
{
    int8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_s16_s8(a0); show("vreinterpretq_s16_s8", &r, sizeof r);
}

static void t_vreinterpretq_s16_u16(void)
{
    uint16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_s16_u16(a0); show("vreinterpretq_s16_u16", &r, sizeof r);
}

static void t_vreinterpretq_s16_u32(void)
{
    uint32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_s16_u32(a0); show("vreinterpretq_s16_u32", &r, sizeof r);
}

static void t_vreinterpretq_s16_s32(void)
{
    int32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_s16_s32(a0); show("vreinterpretq_s16_s32", &r, sizeof r);
}

static void t_vreinterpretq_s16_u64(void)
{
    uint64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_s16_u64(a0); show("vreinterpretq_s16_u64", &r, sizeof r);
}

static void t_vreinterpretq_s16_s64(void)
{
    int64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_s16_s64(a0); show("vreinterpretq_s16_s64", &r, sizeof r);
}

static void t_vreinterpretq_s16_f32(void)
{
    float32x4_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    __auto_type r = vreinterpretq_s16_f32(a0); show("vreinterpretq_s16_f32", &r, sizeof r);
}

static void t_vreinterpretq_s16_f64(void)
{
    float64x2_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    __auto_type r = vreinterpretq_s16_f64(a0); show("vreinterpretq_s16_f64", &r, sizeof r);
}

static void t_vreinterpretq_s16_p8(void)
{
    poly8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_s16_p8(a0); show("vreinterpretq_s16_p8", &r, sizeof r);
}

static void t_vreinterpretq_s16_p16(void)
{
    poly16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_s16_p16(a0); show("vreinterpretq_s16_p16", &r, sizeof r);
}

static void t_vreinterpretq_s16_p64(void)
{
    poly64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_s16_p64(a0); show("vreinterpretq_s16_p64", &r, sizeof r);
}

static void t_vreinterpretq_u32_u8(void)
{
    uint8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_u32_u8(a0); show("vreinterpretq_u32_u8", &r, sizeof r);
}

static void t_vreinterpretq_u32_s8(void)
{
    int8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_u32_s8(a0); show("vreinterpretq_u32_s8", &r, sizeof r);
}

static void t_vreinterpretq_u32_u16(void)
{
    uint16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_u32_u16(a0); show("vreinterpretq_u32_u16", &r, sizeof r);
}

static void t_vreinterpretq_u32_s16(void)
{
    int16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_u32_s16(a0); show("vreinterpretq_u32_s16", &r, sizeof r);
}

static void t_vreinterpretq_u32_s32(void)
{
    int32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_u32_s32(a0); show("vreinterpretq_u32_s32", &r, sizeof r);
}

static void t_vreinterpretq_u32_u64(void)
{
    uint64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_u32_u64(a0); show("vreinterpretq_u32_u64", &r, sizeof r);
}

static void t_vreinterpretq_u32_s64(void)
{
    int64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_u32_s64(a0); show("vreinterpretq_u32_s64", &r, sizeof r);
}

static void t_vreinterpretq_u32_f32(void)
{
    float32x4_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    __auto_type r = vreinterpretq_u32_f32(a0); show("vreinterpretq_u32_f32", &r, sizeof r);
}

static void t_vreinterpretq_u32_f64(void)
{
    float64x2_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    __auto_type r = vreinterpretq_u32_f64(a0); show("vreinterpretq_u32_f64", &r, sizeof r);
}

static void t_vreinterpretq_u32_p8(void)
{
    poly8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_u32_p8(a0); show("vreinterpretq_u32_p8", &r, sizeof r);
}

static void t_vreinterpretq_u32_p16(void)
{
    poly16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_u32_p16(a0); show("vreinterpretq_u32_p16", &r, sizeof r);
}

static void t_vreinterpretq_u32_p64(void)
{
    poly64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_u32_p64(a0); show("vreinterpretq_u32_p64", &r, sizeof r);
}

static void t_vreinterpretq_s32_u8(void)
{
    uint8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_s32_u8(a0); show("vreinterpretq_s32_u8", &r, sizeof r);
}

static void t_vreinterpretq_s32_s8(void)
{
    int8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_s32_s8(a0); show("vreinterpretq_s32_s8", &r, sizeof r);
}

static void t_vreinterpretq_s32_u16(void)
{
    uint16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_s32_u16(a0); show("vreinterpretq_s32_u16", &r, sizeof r);
}

static void t_vreinterpretq_s32_s16(void)
{
    int16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_s32_s16(a0); show("vreinterpretq_s32_s16", &r, sizeof r);
}

static void t_vreinterpretq_s32_u32(void)
{
    uint32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_s32_u32(a0); show("vreinterpretq_s32_u32", &r, sizeof r);
}

static void t_vreinterpretq_s32_u64(void)
{
    uint64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_s32_u64(a0); show("vreinterpretq_s32_u64", &r, sizeof r);
}

static void t_vreinterpretq_s32_s64(void)
{
    int64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_s32_s64(a0); show("vreinterpretq_s32_s64", &r, sizeof r);
}

static void t_vreinterpretq_s32_f32(void)
{
    float32x4_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    __auto_type r = vreinterpretq_s32_f32(a0); show("vreinterpretq_s32_f32", &r, sizeof r);
}

static void t_vreinterpretq_s32_f64(void)
{
    float64x2_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    __auto_type r = vreinterpretq_s32_f64(a0); show("vreinterpretq_s32_f64", &r, sizeof r);
}

static void t_vreinterpretq_s32_p8(void)
{
    poly8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_s32_p8(a0); show("vreinterpretq_s32_p8", &r, sizeof r);
}

static void t_vreinterpretq_s32_p16(void)
{
    poly16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_s32_p16(a0); show("vreinterpretq_s32_p16", &r, sizeof r);
}

static void t_vreinterpretq_s32_p64(void)
{
    poly64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_s32_p64(a0); show("vreinterpretq_s32_p64", &r, sizeof r);
}

static void t_vreinterpretq_u64_u8(void)
{
    uint8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_u64_u8(a0); show("vreinterpretq_u64_u8", &r, sizeof r);
}

static void t_vreinterpretq_u64_s8(void)
{
    int8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_u64_s8(a0); show("vreinterpretq_u64_s8", &r, sizeof r);
}

static void t_vreinterpretq_u64_u16(void)
{
    uint16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_u64_u16(a0); show("vreinterpretq_u64_u16", &r, sizeof r);
}

static void t_vreinterpretq_u64_s16(void)
{
    int16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_u64_s16(a0); show("vreinterpretq_u64_s16", &r, sizeof r);
}

static void t_vreinterpretq_u64_u32(void)
{
    uint32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_u64_u32(a0); show("vreinterpretq_u64_u32", &r, sizeof r);
}

static void t_vreinterpretq_u64_s32(void)
{
    int32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_u64_s32(a0); show("vreinterpretq_u64_s32", &r, sizeof r);
}

static void t_vreinterpretq_u64_s64(void)
{
    int64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_u64_s64(a0); show("vreinterpretq_u64_s64", &r, sizeof r);
}

static void t_vreinterpretq_u64_f32(void)
{
    float32x4_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    __auto_type r = vreinterpretq_u64_f32(a0); show("vreinterpretq_u64_f32", &r, sizeof r);
}

static void t_vreinterpretq_u64_f64(void)
{
    float64x2_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    __auto_type r = vreinterpretq_u64_f64(a0); show("vreinterpretq_u64_f64", &r, sizeof r);
}

static void t_vreinterpretq_u64_p8(void)
{
    poly8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_u64_p8(a0); show("vreinterpretq_u64_p8", &r, sizeof r);
}

static void t_vreinterpretq_u64_p16(void)
{
    poly16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_u64_p16(a0); show("vreinterpretq_u64_p16", &r, sizeof r);
}

static void t_vreinterpretq_u64_p64(void)
{
    poly64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_u64_p64(a0); show("vreinterpretq_u64_p64", &r, sizeof r);
}

static void t_vreinterpretq_s64_u8(void)
{
    uint8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_s64_u8(a0); show("vreinterpretq_s64_u8", &r, sizeof r);
}

static void t_vreinterpretq_s64_s8(void)
{
    int8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_s64_s8(a0); show("vreinterpretq_s64_s8", &r, sizeof r);
}

static void t_vreinterpretq_s64_u16(void)
{
    uint16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_s64_u16(a0); show("vreinterpretq_s64_u16", &r, sizeof r);
}

static void t_vreinterpretq_s64_s16(void)
{
    int16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_s64_s16(a0); show("vreinterpretq_s64_s16", &r, sizeof r);
}

static void t_vreinterpretq_s64_u32(void)
{
    uint32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_s64_u32(a0); show("vreinterpretq_s64_u32", &r, sizeof r);
}

static void t_vreinterpretq_s64_s32(void)
{
    int32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_s64_s32(a0); show("vreinterpretq_s64_s32", &r, sizeof r);
}

static void t_vreinterpretq_s64_u64(void)
{
    uint64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_s64_u64(a0); show("vreinterpretq_s64_u64", &r, sizeof r);
}

static void t_vreinterpretq_s64_f32(void)
{
    float32x4_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    __auto_type r = vreinterpretq_s64_f32(a0); show("vreinterpretq_s64_f32", &r, sizeof r);
}

static void t_vreinterpretq_s64_f64(void)
{
    float64x2_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    __auto_type r = vreinterpretq_s64_f64(a0); show("vreinterpretq_s64_f64", &r, sizeof r);
}

static void t_vreinterpretq_s64_p8(void)
{
    poly8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_s64_p8(a0); show("vreinterpretq_s64_p8", &r, sizeof r);
}

static void t_vreinterpretq_s64_p16(void)
{
    poly16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_s64_p16(a0); show("vreinterpretq_s64_p16", &r, sizeof r);
}

static void t_vreinterpretq_s64_p64(void)
{
    poly64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_s64_p64(a0); show("vreinterpretq_s64_p64", &r, sizeof r);
}

static void t_vreinterpretq_f32_u8(void)
{
    uint8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_f32_u8(a0); show("vreinterpretq_f32_u8", &r, sizeof r);
}

static void t_vreinterpretq_f32_s8(void)
{
    int8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_f32_s8(a0); show("vreinterpretq_f32_s8", &r, sizeof r);
}

static void t_vreinterpretq_f32_u16(void)
{
    uint16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_f32_u16(a0); show("vreinterpretq_f32_u16", &r, sizeof r);
}

static void t_vreinterpretq_f32_s16(void)
{
    int16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_f32_s16(a0); show("vreinterpretq_f32_s16", &r, sizeof r);
}

static void t_vreinterpretq_f32_u32(void)
{
    uint32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_f32_u32(a0); show("vreinterpretq_f32_u32", &r, sizeof r);
}

static void t_vreinterpretq_f32_s32(void)
{
    int32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_f32_s32(a0); show("vreinterpretq_f32_s32", &r, sizeof r);
}

static void t_vreinterpretq_f32_u64(void)
{
    uint64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_f32_u64(a0); show("vreinterpretq_f32_u64", &r, sizeof r);
}

static void t_vreinterpretq_f32_s64(void)
{
    int64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_f32_s64(a0); show("vreinterpretq_f32_s64", &r, sizeof r);
}

static void t_vreinterpretq_f32_f64(void)
{
    float64x2_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    __auto_type r = vreinterpretq_f32_f64(a0); show("vreinterpretq_f32_f64", &r, sizeof r);
}

static void t_vreinterpretq_f32_p8(void)
{
    poly8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_f32_p8(a0); show("vreinterpretq_f32_p8", &r, sizeof r);
}

static void t_vreinterpretq_f32_p16(void)
{
    poly16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_f32_p16(a0); show("vreinterpretq_f32_p16", &r, sizeof r);
}

static void t_vreinterpretq_f32_p64(void)
{
    poly64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_f32_p64(a0); show("vreinterpretq_f32_p64", &r, sizeof r);
}

static void t_vreinterpretq_f64_u8(void)
{
    uint8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_f64_u8(a0); show("vreinterpretq_f64_u8", &r, sizeof r);
}

static void t_vreinterpretq_f64_s8(void)
{
    int8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_f64_s8(a0); show("vreinterpretq_f64_s8", &r, sizeof r);
}

static void t_vreinterpretq_f64_u16(void)
{
    uint16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_f64_u16(a0); show("vreinterpretq_f64_u16", &r, sizeof r);
}

static void t_vreinterpretq_f64_s16(void)
{
    int16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_f64_s16(a0); show("vreinterpretq_f64_s16", &r, sizeof r);
}

static void t_vreinterpretq_f64_u32(void)
{
    uint32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_f64_u32(a0); show("vreinterpretq_f64_u32", &r, sizeof r);
}

static void t_vreinterpretq_f64_s32(void)
{
    int32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_f64_s32(a0); show("vreinterpretq_f64_s32", &r, sizeof r);
}

static void t_vreinterpretq_f64_u64(void)
{
    uint64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_f64_u64(a0); show("vreinterpretq_f64_u64", &r, sizeof r);
}

static void t_vreinterpretq_f64_s64(void)
{
    int64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_f64_s64(a0); show("vreinterpretq_f64_s64", &r, sizeof r);
}

static void t_vreinterpretq_f64_f32(void)
{
    float32x4_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    __auto_type r = vreinterpretq_f64_f32(a0); show("vreinterpretq_f64_f32", &r, sizeof r);
}

static void t_vreinterpretq_f64_p8(void)
{
    poly8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_f64_p8(a0); show("vreinterpretq_f64_p8", &r, sizeof r);
}

static void t_vreinterpretq_f64_p16(void)
{
    poly16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_f64_p16(a0); show("vreinterpretq_f64_p16", &r, sizeof r);
}

static void t_vreinterpretq_f64_p64(void)
{
    poly64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_f64_p64(a0); show("vreinterpretq_f64_p64", &r, sizeof r);
}

static void t_vreinterpretq_p8_u8(void)
{
    uint8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_p8_u8(a0); show("vreinterpretq_p8_u8", &r, sizeof r);
}

static void t_vreinterpretq_p8_s8(void)
{
    int8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_p8_s8(a0); show("vreinterpretq_p8_s8", &r, sizeof r);
}

static void t_vreinterpretq_p8_u16(void)
{
    uint16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_p8_u16(a0); show("vreinterpretq_p8_u16", &r, sizeof r);
}

static void t_vreinterpretq_p8_s16(void)
{
    int16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_p8_s16(a0); show("vreinterpretq_p8_s16", &r, sizeof r);
}

static void t_vreinterpretq_p8_u32(void)
{
    uint32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_p8_u32(a0); show("vreinterpretq_p8_u32", &r, sizeof r);
}

static void t_vreinterpretq_p8_s32(void)
{
    int32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_p8_s32(a0); show("vreinterpretq_p8_s32", &r, sizeof r);
}

static void t_vreinterpretq_p8_u64(void)
{
    uint64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_p8_u64(a0); show("vreinterpretq_p8_u64", &r, sizeof r);
}

static void t_vreinterpretq_p8_s64(void)
{
    int64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_p8_s64(a0); show("vreinterpretq_p8_s64", &r, sizeof r);
}

static void t_vreinterpretq_p8_f32(void)
{
    float32x4_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    __auto_type r = vreinterpretq_p8_f32(a0); show("vreinterpretq_p8_f32", &r, sizeof r);
}

static void t_vreinterpretq_p8_f64(void)
{
    float64x2_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    __auto_type r = vreinterpretq_p8_f64(a0); show("vreinterpretq_p8_f64", &r, sizeof r);
}

static void t_vreinterpretq_p8_p16(void)
{
    poly16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_p8_p16(a0); show("vreinterpretq_p8_p16", &r, sizeof r);
}

static void t_vreinterpretq_p8_p64(void)
{
    poly64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_p8_p64(a0); show("vreinterpretq_p8_p64", &r, sizeof r);
}

static void t_vreinterpretq_p16_u8(void)
{
    uint8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_p16_u8(a0); show("vreinterpretq_p16_u8", &r, sizeof r);
}

static void t_vreinterpretq_p16_s8(void)
{
    int8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_p16_s8(a0); show("vreinterpretq_p16_s8", &r, sizeof r);
}

static void t_vreinterpretq_p16_u16(void)
{
    uint16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_p16_u16(a0); show("vreinterpretq_p16_u16", &r, sizeof r);
}

static void t_vreinterpretq_p16_s16(void)
{
    int16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_p16_s16(a0); show("vreinterpretq_p16_s16", &r, sizeof r);
}

static void t_vreinterpretq_p16_u32(void)
{
    uint32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_p16_u32(a0); show("vreinterpretq_p16_u32", &r, sizeof r);
}

static void t_vreinterpretq_p16_s32(void)
{
    int32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_p16_s32(a0); show("vreinterpretq_p16_s32", &r, sizeof r);
}

static void t_vreinterpretq_p16_u64(void)
{
    uint64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_p16_u64(a0); show("vreinterpretq_p16_u64", &r, sizeof r);
}

static void t_vreinterpretq_p16_s64(void)
{
    int64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_p16_s64(a0); show("vreinterpretq_p16_s64", &r, sizeof r);
}

static void t_vreinterpretq_p16_f32(void)
{
    float32x4_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    __auto_type r = vreinterpretq_p16_f32(a0); show("vreinterpretq_p16_f32", &r, sizeof r);
}

static void t_vreinterpretq_p16_f64(void)
{
    float64x2_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    __auto_type r = vreinterpretq_p16_f64(a0); show("vreinterpretq_p16_f64", &r, sizeof r);
}

static void t_vreinterpretq_p16_p8(void)
{
    poly8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_p16_p8(a0); show("vreinterpretq_p16_p8", &r, sizeof r);
}

static void t_vreinterpretq_p16_p64(void)
{
    poly64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_p16_p64(a0); show("vreinterpretq_p16_p64", &r, sizeof r);
}

static void t_vreinterpretq_p64_u8(void)
{
    uint8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_p64_u8(a0); show("vreinterpretq_p64_u8", &r, sizeof r);
}

static void t_vreinterpretq_p64_s8(void)
{
    int8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_p64_s8(a0); show("vreinterpretq_p64_s8", &r, sizeof r);
}

static void t_vreinterpretq_p64_u16(void)
{
    uint16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_p64_u16(a0); show("vreinterpretq_p64_u16", &r, sizeof r);
}

static void t_vreinterpretq_p64_s16(void)
{
    int16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_p64_s16(a0); show("vreinterpretq_p64_s16", &r, sizeof r);
}

static void t_vreinterpretq_p64_u32(void)
{
    uint32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_p64_u32(a0); show("vreinterpretq_p64_u32", &r, sizeof r);
}

static void t_vreinterpretq_p64_s32(void)
{
    int32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_p64_s32(a0); show("vreinterpretq_p64_s32", &r, sizeof r);
}

static void t_vreinterpretq_p64_u64(void)
{
    uint64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_p64_u64(a0); show("vreinterpretq_p64_u64", &r, sizeof r);
}

static void t_vreinterpretq_p64_s64(void)
{
    int64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_p64_s64(a0); show("vreinterpretq_p64_s64", &r, sizeof r);
}

static void t_vreinterpretq_p64_f32(void)
{
    float32x4_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    __auto_type r = vreinterpretq_p64_f32(a0); show("vreinterpretq_p64_f32", &r, sizeof r);
}

static void t_vreinterpretq_p64_f64(void)
{
    float64x2_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    __auto_type r = vreinterpretq_p64_f64(a0); show("vreinterpretq_p64_f64", &r, sizeof r);
}

static void t_vreinterpretq_p64_p8(void)
{
    poly8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_p64_p8(a0); show("vreinterpretq_p64_p8", &r, sizeof r);
}

static void t_vreinterpretq_p64_p16(void)
{
    poly16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_p64_p16(a0); show("vreinterpretq_p64_p16", &r, sizeof r);
}

static void t_vld1q_dup_u32(void)
{
    __auto_type r = vld1q_dup_u32((const unsigned int *)(pool + 0)); show("vld1q_dup_u32", &r, sizeof r);
}

static void t_vld1q_lane_u32(void)
{
    uint32x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vld1q_lane_u32((const unsigned int *)(pool + 0), a1, 3); show("vld1q_lane_u32", &r, sizeof r);
}

static void t_vld1q_u8_x4(void)
{
    __auto_type r = vld1q_u8_x4((const unsigned char *)(pool + 0)); show("vld1q_u8_x4", &r, sizeof r);
}

static void t_vld3q_u8(void)
{
    __auto_type r = vld3q_u8((const unsigned char *)(pool + 0)); show("vld3q_u8", &r, sizeof r);
}

static void t_vld3_dup_u8(void)
{
    __auto_type r = vld3_dup_u8((const unsigned char *)(pool + 0)); show("vld3_dup_u8", &r, sizeof r);
}

static void t_vld3_lane_u8(void)
{
    uint8x8x3_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vld3_lane_u8((const unsigned char *)(pool + 0), a1, 7); show("vld3_lane_u8", &r, sizeof r);
}

static void t_vld4_u32(void)
{
    __auto_type r = vld4_u32((const unsigned int *)(pool + 0)); show("vld4_u32", &r, sizeof r);
}

static void t_vst1_lane_u32(void)
{
    memset(out, 0, sizeof out);
    uint32x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    vst1_lane_u32((unsigned int *)out, a1, 1);
    show("vst1_lane_u32", out, sizeof out);
}

static void t_vst3_u8(void)
{
    memset(out, 0, sizeof out);
    uint8x8x3_t a1; memcpy(&a1, pool + 32, sizeof a1);
    vst3_u8((unsigned char *)out, a1);
    show("vst3_u8", out, sizeof out);
}

static void t_vst4q_u8(void)
{
    memset(out, 0, sizeof out);
    uint8x16x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    vst4q_u8((unsigned char *)out, a1);
    show("vst4q_u8", out, sizeof out);
}

static void t_vst4_lane_u32(void)
{
    memset(out, 0, sizeof out);
    uint32x2x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    vst4_lane_u32((unsigned int *)out, a1, 1);
    show("vst4_lane_u32", out, sizeof out);
}

static void t_vcombine_u8(void)
{
    uint8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint8x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vcombine_u8(a0, a1); show("vcombine_u8", &r, sizeof r);
}

static void t_vcombine_s8(void)
{
    int8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int8x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vcombine_s8(a0, a1); show("vcombine_s8", &r, sizeof r);
}

static void t_vcombine_u64(void)
{
    uint64x1_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint64x1_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vcombine_u64(a0, a1); show("vcombine_u64", &r, sizeof r);
}

static void t_vcombine_u16(void)
{
    uint16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint16x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vcombine_u16(a0, a1); show("vcombine_u16", &r, sizeof r);
}

static void t_vget_low_u8(void)
{
    uint8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vget_low_u8(a0); show("vget_low_u8", &r, sizeof r);
}

static void t_vget_high_u8(void)
{
    uint8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vget_high_u8(a0); show("vget_high_u8", &r, sizeof r);
}

static void t_vget_low_s8(void)
{
    int8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vget_low_s8(a0); show("vget_low_s8", &r, sizeof r);
}

static void t_vget_high_s8(void)
{
    int8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vget_high_s8(a0); show("vget_high_s8", &r, sizeof r);
}

static void t_vget_low_u64(void)
{
    uint64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vget_low_u64(a0); show("vget_low_u64", &r, sizeof r);
}

static void t_vget_high_u64(void)
{
    uint64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vget_high_u64(a0); show("vget_high_u64", &r, sizeof r);
}

static void t_vget_low_p64(void)
{
    poly64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vget_low_p64(a0); show("vget_low_p64", &r, sizeof r);
}

static void t_vaddl_u8(void)
{
    uint8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint8x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vaddl_u8(a0, a1); show("vaddl_u8", &r, sizeof r);
}

static void t_vabdl_u8(void)
{
    uint8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint8x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vabdl_u8(a0, a1); show("vabdl_u8", &r, sizeof r);
}

static void t_vabdq_u16(void)
{
    uint16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint16x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vabdq_u16(a0, a1); show("vabdq_u16", &r, sizeof r);
}

static void t_vhadd_u8(void)
{
    uint8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint8x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vhadd_u8(a0, a1); show("vhadd_u8", &r, sizeof r);
}

static void t_vmovn_u16(void)
{
    uint16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vmovn_u16(a0); show("vmovn_u16", &r, sizeof r);
}

static void t_vextq_u8(void)
{
    uint8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint8x16_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vextq_u8(a0, a1, 8); show("vextq_u8", &r, sizeof r);
}

static void t_vext_u8(void)
{
    uint8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint8x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vext_u8(a0, a1, 4); show("vext_u8", &r, sizeof r);
}

static void t_vmulq_p8(void)
{
    poly8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    poly8x16_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vmulq_p8(a0, a1); show("vmulq_p8", &r, sizeof r);
}

static void t_vmull_p64(void)
{
    poly64_t a0; memcpy(&a0, pool + 0, sizeof a0);
    poly64_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vmull_p64(a0, a1); show("vmull_p64", &r, sizeof r);
}

static void t_vmull_high_p64(void)
{
    poly64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    poly64x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vmull_high_p64(a0, a1); show("vmull_high_p64", &r, sizeof r);
}

static void t_vrev32q_u16(void)
{
    uint16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vrev32q_u16(a0); show("vrev32q_u16", &r, sizeof r);
}

static void t_vrbitq_u8(void)
{
    uint8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vrbitq_u8(a0); show("vrbitq_u8", &r, sizeof r);
}

static void t_vqtbl1q_u8(void)
{
    uint8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint8x16_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vqtbl1q_u8(a0, a1); show("vqtbl1q_u8", &r, sizeof r);
}

static void t_vqtbx1q_u8(void)
{
    uint8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint8x16_t a1; memcpy(&a1, pool + 32, sizeof a1);
    uint8x16_t a2; memcpy(&a2, pool + 64, sizeof a2);
    __auto_type r = vqtbx1q_u8(a0, a1, a2); show("vqtbx1q_u8", &r, sizeof r);
}

static void t_vqtbl4q_u8(void)
{
    uint8x16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint8x16_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vqtbl4q_u8(a0, a1); show("vqtbl4q_u8", &r, sizeof r);
}

static void t_vqtbx4q_u8(void)
{
    uint8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint8x16x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    uint8x16_t a2; memcpy(&a2, pool + 64, sizeof a2);
    __auto_type r = vqtbx4q_u8(a0, a1, a2); show("vqtbx4q_u8", &r, sizeof r);
}

static void t_vtbl2_u8(void)
{
    uint8x8x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint8x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vtbl2_u8(a0, a1); show("vtbl2_u8", &r, sizeof r);
}

static void t_vtbx2_u8(void)
{
    uint8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint8x8x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    uint8x8_t a2; memcpy(&a2, pool + 64, sizeof a2);
    __auto_type r = vtbx2_u8(a0, a1, a2); show("vtbx2_u8", &r, sizeof r);
}

static void t_vpmin_s8(void)
{
    int8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int8x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vpmin_s8(a0, a1); show("vpmin_s8", &r, sizeof r);
}

static void t_vminvq_s8(void)
{
    int8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vminvq_s8(a0); show("vminvq_s8", &r, sizeof r);
}

static void t_vminvq_u32(void)
{
    uint32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vminvq_u32(a0); show("vminvq_u32", &r, sizeof r);
}

static void t_vaeseq_u8(void)
{
    uint8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint8x16_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vaeseq_u8(a0, a1); show("vaeseq_u8", &r, sizeof r);
}

static void t_vaesmcq_u8(void)
{
    uint8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vaesmcq_u8(a0); show("vaesmcq_u8", &r, sizeof r);
}

static void t_vaesdq_u8(void)
{
    uint8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint8x16_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vaesdq_u8(a0, a1); show("vaesdq_u8", &r, sizeof r);
}

static void t_vaesimcq_u8(void)
{
    uint8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vaesimcq_u8(a0); show("vaesimcq_u8", &r, sizeof r);
}

static void t_vreinterpretq_u8_p128(void)
{
    poly128_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vreinterpretq_u8_p128(a0); show("vreinterpretq_u8_p128", &r, sizeof r);
}

static void t_vcreate_u32(void)
{
    unsigned long long a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vcreate_u32(a0); show("vcreate_u32", &r, sizeof r);
}

static void t_vcreate_u64(void)
{
    unsigned long long a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vcreate_u64(a0); show("vcreate_u64", &r, sizeof r);
}

static void t_vcombine_u32(void)
{
    uint32x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint32x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vcombine_u32(a0, a1); show("vcombine_u32", &r, sizeof r);
}

static void t_vget_low_u32(void)
{
    uint32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vget_low_u32(a0); show("vget_low_u32", &r, sizeof r);
}

static void t_vget_high_u32(void)
{
    uint32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vget_high_u32(a0); show("vget_high_u32", &r, sizeof r);
}

static void t_vmaxq_u8(void)
{
    uint8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint8x16_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vmaxq_u8(a0, a1); show("vmaxq_u8", &r, sizeof r);
}

static void t_vcntq_u8(void)
{
    uint8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vcntq_u8(a0); show("vcntq_u8", &r, sizeof r);
}

static void t_vpaddlq_u8(void)
{
    uint8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vpaddlq_u8(a0); show("vpaddlq_u8", &r, sizeof r);
}

static void t_vpaddlq_u16(void)
{
    uint16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vpaddlq_u16(a0); show("vpaddlq_u16", &r, sizeof r);
}

static void t_vpaddlq_u32(void)
{
    uint32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vpaddlq_u32(a0); show("vpaddlq_u32", &r, sizeof r);
}

static void t_vpadalq_u8(void)
{
    uint16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint8x16_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vpadalq_u8(a0, a1); show("vpadalq_u8", &r, sizeof r);
}

static void t_vmovn_u64(void)
{
    uint64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vmovn_u64(a0); show("vmovn_u64", &r, sizeof r);
}

static void t_vmovn_u32(void)
{
    uint32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vmovn_u32(a0); show("vmovn_u32", &r, sizeof r);
}

static void t_vmlal_u32(void)
{
    uint64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint32x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    uint32x2_t a2; memcpy(&a2, pool + 64, sizeof a2);
    __auto_type r = vmlal_u32(a0, a1, a2); show("vmlal_u32", &r, sizeof r);
}

static void t_vmlal_high_u32(void)
{
    uint64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint32x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    uint32x4_t a2; memcpy(&a2, pool + 64, sizeof a2);
    __auto_type r = vmlal_high_u32(a0, a1, a2); show("vmlal_high_u32", &r, sizeof r);
}

static void t_vuzpq_u32(void)
{
    uint32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint32x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vuzpq_u32(a0, a1); show("vuzpq_u32", &r, sizeof r);
}

static void t_vsha256su0q_u32(void)
{
    uint32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint32x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vsha256su0q_u32(a0, a1); show("vsha256su0q_u32", &r, sizeof r);
}

static void t_vsha256su1q_u32(void)
{
    uint32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint32x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    uint32x4_t a2; memcpy(&a2, pool + 64, sizeof a2);
    __auto_type r = vsha256su1q_u32(a0, a1, a2); show("vsha256su1q_u32", &r, sizeof r);
}

static void t_vsha256hq_u32(void)
{
    uint32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint32x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    uint32x4_t a2; memcpy(&a2, pool + 64, sizeof a2);
    __auto_type r = vsha256hq_u32(a0, a1, a2); show("vsha256hq_u32", &r, sizeof r);
}

static void t_vsha256h2q_u32(void)
{
    uint32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint32x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    uint32x4_t a2; memcpy(&a2, pool + 64, sizeof a2);
    __auto_type r = vsha256h2q_u32(a0, a1, a2); show("vsha256h2q_u32", &r, sizeof r);
}

static void t_vshrn_n_u16(void)
{
    uint16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vshrn_n_u16(a0, 3); show("vshrn_n_u16", &r, sizeof r);
}

static void t_vsli_n_u8(void)
{
    uint8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint8x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vsli_n_u8(a0, a1, 3); show("vsli_n_u8", &r, sizeof r);
}

static void t_vsriq_n_u8(void)
{
    uint8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint8x16_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vsriq_n_u8(a0, a1, 3); show("vsriq_n_u8", &r, sizeof r);
}

static void t_vld2q_u16(void)
{
    __auto_type r = vld2q_u16((const unsigned short *)(pool + 0)); show("vld2q_u16", &r, sizeof r);
}

static void t_vld4q_u8(void)
{
    __auto_type r = vld4q_u8((const unsigned char *)(pool + 0)); show("vld4q_u8", &r, sizeof r);
}

static void t_vmaxvq_u8(void)
{
    uint8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vmaxvq_u8(a0); show("vmaxvq_u8", &r, sizeof r);
}

static void t_vminvq_u8(void)
{
    uint8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vminvq_u8(a0); show("vminvq_u8", &r, sizeof r);
}

static void t_vmaxvq_u32(void)
{
    uint32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vmaxvq_u32(a0); show("vmaxvq_u32", &r, sizeof r);
}

static void t_vmaxvq_s16(void)
{
    int16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vmaxvq_s16(a0); show("vmaxvq_s16", &r, sizeof r);
}

static void t_vmaxvq_u16(void)
{
    uint16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vmaxvq_u16(a0); show("vmaxvq_u16", &r, sizeof r);
}

static void t_vqsubq_u8(void)
{
    uint8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint8x16_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vqsubq_u8(a0, a1); show("vqsubq_u8", &r, sizeof r);
}

static void t_vqaddq_u8(void)
{
    uint8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint8x16_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vqaddq_u8(a0, a1); show("vqaddq_u8", &r, sizeof r);
}

static void t_vqaddq_s16(void)
{
    int16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int16x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vqaddq_s16(a0, a1); show("vqaddq_s16", &r, sizeof r);
}

static void t_vqsubq_u16(void)
{
    uint16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint16x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vqsubq_u16(a0, a1); show("vqsubq_u16", &r, sizeof r);
}

static void t_vmaxq_s16(void)
{
    int16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int16x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vmaxq_s16(a0, a1); show("vmaxq_s16", &r, sizeof r);
}

static void t_vget_low_f32(void)
{
    float32x4_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    __auto_type r = vget_low_f32(a0); show("vget_low_f32", &r, sizeof r);
}

static void t_vget_high_f32(void)
{
    float32x4_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    __auto_type r = vget_high_f32(a0); show("vget_high_f32", &r, sizeof r);
}

static void t_vcombine_f32(void)
{
    float32x2_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    float32x2_t a1; memcpy(&a1, fpool + 32, sizeof a1);
    __auto_type r = vcombine_f32(a0, a1); show("vcombine_f32", &r, sizeof r);
}

static void t_vrev64q_f32(void)
{
    float32x4_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    __auto_type r = vrev64q_f32(a0); show("vrev64q_f32", &r, sizeof r);
}

static void t_vcvtq_s32_f32(void)
{
    float32x4_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    __auto_type r = vcvtq_s32_f32(a0); show("vcvtq_s32_f32", &r, sizeof r);
}

static void t_vqaddq_s32(void)
{
    int32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int32x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vqaddq_s32(a0, a1); show("vqaddq_s32", &r, sizeof r);
}

static void t_vqmovn_s32(void)
{
    int32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vqmovn_s32(a0); show("vqmovn_s32", &r, sizeof r);
}

static void t_vst1_lane_s16(void)
{
    memset(out, 0, sizeof out);
    int16x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    vst1_lane_s16((short *)out, a1, 3);
    show("vst1_lane_s16", out, sizeof out);
}

static void t_vmaxq_u16(void)
{
    uint16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint16x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vmaxq_u16(a0, a1); show("vmaxq_u16", &r, sizeof r);
}

static void t_vminq_u16(void)
{
    uint16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint16x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vminq_u16(a0, a1); show("vminq_u16", &r, sizeof r);
}

static void t_vminq_s16(void)
{
    int16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int16x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vminq_s16(a0, a1); show("vminq_s16", &r, sizeof r);
}

static void t_vcgtzq_s16(void)
{
    int16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vcgtzq_s16(a0); show("vcgtzq_s16", &r, sizeof r);
}

static void t_vcgtzq_s32(void)
{
    int32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vcgtzq_s32(a0); show("vcgtzq_s32", &r, sizeof r);
}

static void t_vcltzq_s16(void)
{
    int16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vcltzq_s16(a0); show("vcltzq_s16", &r, sizeof r);
}

static void t_vqtbl2q_u8(void)
{
    uint8x16x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint8x16_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vqtbl2q_u8(a0, a1); show("vqtbl2q_u8", &r, sizeof r);
}

static void t_vqtbx2q_u8(void)
{
    uint8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint8x16x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    uint8x16_t a2; memcpy(&a2, pool + 64, sizeof a2);
    __auto_type r = vqtbx2q_u8(a0, a1, a2); show("vqtbx2q_u8", &r, sizeof r);
}

static void t_vqtbl3q_u8(void)
{
    uint8x16x3_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint8x16_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vqtbl3q_u8(a0, a1); show("vqtbl3q_u8", &r, sizeof r);
}

static void t_vuzp1q_u16(void)
{
    uint16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint16x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vuzp1q_u16(a0, a1); show("vuzp1q_u16", &r, sizeof r);
}

static void t_vuzp2q_u16(void)
{
    uint16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint16x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vuzp2q_u16(a0, a1); show("vuzp2q_u16", &r, sizeof r);
}

static void t_vuzp1q_u8(void)
{
    uint8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint8x16_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vuzp1q_u8(a0, a1); show("vuzp1q_u8", &r, sizeof r);
}

static void t_vuzp2q_u8(void)
{
    uint8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint8x16_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vuzp2q_u8(a0, a1); show("vuzp2q_u8", &r, sizeof r);
}

static void t_vuzp1q_u32(void)
{
    uint32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint32x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vuzp1q_u32(a0, a1); show("vuzp1q_u32", &r, sizeof r);
}

static void t_vuzp2q_u32(void)
{
    uint32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint32x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vuzp2q_u32(a0, a1); show("vuzp2q_u32", &r, sizeof r);
}

static void t_vzip1q_u16(void)
{
    uint16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint16x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vzip1q_u16(a0, a1); show("vzip1q_u16", &r, sizeof r);
}

static void t_vzip2q_u16(void)
{
    uint16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint16x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vzip2q_u16(a0, a1); show("vzip2q_u16", &r, sizeof r);
}

static void t_vzip1q_u32(void)
{
    uint32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint32x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vzip1q_u32(a0, a1); show("vzip1q_u32", &r, sizeof r);
}

static void t_vzip2q_u32(void)
{
    uint32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint32x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vzip2q_u32(a0, a1); show("vzip2q_u32", &r, sizeof r);
}

static void t_vmovl_u16(void)
{
    uint16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vmovl_u16(a0); show("vmovl_u16", &r, sizeof r);
}

static void t_vmovl_s16(void)
{
    int16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vmovl_s16(a0); show("vmovl_s16", &r, sizeof r);
}

static void t_vmovl_high_u16(void)
{
    uint16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vmovl_high_u16(a0); show("vmovl_high_u16", &r, sizeof r);
}

static void t_vmovl_high_u8(void)
{
    uint8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vmovl_high_u8(a0); show("vmovl_high_u8", &r, sizeof r);
}

static void t_vget_low_u16(void)
{
    uint16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vget_low_u16(a0); show("vget_low_u16", &r, sizeof r);
}

static void t_vget_high_u16(void)
{
    uint16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vget_high_u16(a0); show("vget_high_u16", &r, sizeof r);
}

static void t_vtrn1_u8(void)
{
    uint8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint8x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vtrn1_u8(a0, a1); show("vtrn1_u8", &r, sizeof r);
}

static void t_vtrn2_u8(void)
{
    uint8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint8x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vtrn2_u8(a0, a1); show("vtrn2_u8", &r, sizeof r);
}

static void t_vtrn_u8(void)
{
    uint8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint8x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vtrn_u8(a0, a1); show("vtrn_u8", &r, sizeof r);
}

static void t_vtrn1_s8(void)
{
    int8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int8x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vtrn1_s8(a0, a1); show("vtrn1_s8", &r, sizeof r);
}

static void t_vtrn2_s8(void)
{
    int8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int8x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vtrn2_s8(a0, a1); show("vtrn2_s8", &r, sizeof r);
}

static void t_vtrn_s8(void)
{
    int8x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int8x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vtrn_s8(a0, a1); show("vtrn_s8", &r, sizeof r);
}

static void t_vtrn1_u16(void)
{
    uint16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint16x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vtrn1_u16(a0, a1); show("vtrn1_u16", &r, sizeof r);
}

static void t_vtrn2_u16(void)
{
    uint16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint16x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vtrn2_u16(a0, a1); show("vtrn2_u16", &r, sizeof r);
}

static void t_vtrn_u16(void)
{
    uint16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint16x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vtrn_u16(a0, a1); show("vtrn_u16", &r, sizeof r);
}

static void t_vtrn1_s16(void)
{
    int16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int16x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vtrn1_s16(a0, a1); show("vtrn1_s16", &r, sizeof r);
}

static void t_vtrn2_s16(void)
{
    int16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int16x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vtrn2_s16(a0, a1); show("vtrn2_s16", &r, sizeof r);
}

static void t_vtrn_s16(void)
{
    int16x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int16x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vtrn_s16(a0, a1); show("vtrn_s16", &r, sizeof r);
}

static void t_vtrn1_u32(void)
{
    uint32x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint32x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vtrn1_u32(a0, a1); show("vtrn1_u32", &r, sizeof r);
}

static void t_vtrn2_u32(void)
{
    uint32x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint32x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vtrn2_u32(a0, a1); show("vtrn2_u32", &r, sizeof r);
}

static void t_vtrn_u32(void)
{
    uint32x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint32x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vtrn_u32(a0, a1); show("vtrn_u32", &r, sizeof r);
}

static void t_vtrn1_s32(void)
{
    int32x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int32x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vtrn1_s32(a0, a1); show("vtrn1_s32", &r, sizeof r);
}

static void t_vtrn2_s32(void)
{
    int32x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int32x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vtrn2_s32(a0, a1); show("vtrn2_s32", &r, sizeof r);
}

static void t_vtrn_s32(void)
{
    int32x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int32x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vtrn_s32(a0, a1); show("vtrn_s32", &r, sizeof r);
}

static void t_vtrn1_f32(void)
{
    float32x2_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    float32x2_t a1; memcpy(&a1, fpool + 32, sizeof a1);
    __auto_type r = vtrn1_f32(a0, a1); show("vtrn1_f32", &r, sizeof r);
}

static void t_vtrn2_f32(void)
{
    float32x2_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    float32x2_t a1; memcpy(&a1, fpool + 32, sizeof a1);
    __auto_type r = vtrn2_f32(a0, a1); show("vtrn2_f32", &r, sizeof r);
}

static void t_vtrn_f32(void)
{
    float32x2_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    float32x2_t a1; memcpy(&a1, fpool + 32, sizeof a1);
    __auto_type r = vtrn_f32(a0, a1); show("vtrn_f32", &r, sizeof r);
}

static void t_vtrn1q_u8(void)
{
    uint8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint8x16_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vtrn1q_u8(a0, a1); show("vtrn1q_u8", &r, sizeof r);
}

static void t_vtrn2q_u8(void)
{
    uint8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint8x16_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vtrn2q_u8(a0, a1); show("vtrn2q_u8", &r, sizeof r);
}

static void t_vtrnq_u8(void)
{
    uint8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint8x16_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vtrnq_u8(a0, a1); show("vtrnq_u8", &r, sizeof r);
}

static void t_vtrn1q_s8(void)
{
    int8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int8x16_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vtrn1q_s8(a0, a1); show("vtrn1q_s8", &r, sizeof r);
}

static void t_vtrn2q_s8(void)
{
    int8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int8x16_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vtrn2q_s8(a0, a1); show("vtrn2q_s8", &r, sizeof r);
}

static void t_vtrnq_s8(void)
{
    int8x16_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int8x16_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vtrnq_s8(a0, a1); show("vtrnq_s8", &r, sizeof r);
}

static void t_vtrn1q_u16(void)
{
    uint16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint16x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vtrn1q_u16(a0, a1); show("vtrn1q_u16", &r, sizeof r);
}

static void t_vtrn2q_u16(void)
{
    uint16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint16x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vtrn2q_u16(a0, a1); show("vtrn2q_u16", &r, sizeof r);
}

static void t_vtrnq_u16(void)
{
    uint16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint16x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vtrnq_u16(a0, a1); show("vtrnq_u16", &r, sizeof r);
}

static void t_vtrn1q_s16(void)
{
    int16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int16x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vtrn1q_s16(a0, a1); show("vtrn1q_s16", &r, sizeof r);
}

static void t_vtrn2q_s16(void)
{
    int16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int16x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vtrn2q_s16(a0, a1); show("vtrn2q_s16", &r, sizeof r);
}

static void t_vtrnq_s16(void)
{
    int16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int16x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vtrnq_s16(a0, a1); show("vtrnq_s16", &r, sizeof r);
}

static void t_vtrn1q_u32(void)
{
    uint32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint32x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vtrn1q_u32(a0, a1); show("vtrn1q_u32", &r, sizeof r);
}

static void t_vtrn2q_u32(void)
{
    uint32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint32x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vtrn2q_u32(a0, a1); show("vtrn2q_u32", &r, sizeof r);
}

static void t_vtrnq_u32(void)
{
    uint32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint32x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vtrnq_u32(a0, a1); show("vtrnq_u32", &r, sizeof r);
}

static void t_vtrn1q_s32(void)
{
    int32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int32x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vtrn1q_s32(a0, a1); show("vtrn1q_s32", &r, sizeof r);
}

static void t_vtrn2q_s32(void)
{
    int32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int32x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vtrn2q_s32(a0, a1); show("vtrn2q_s32", &r, sizeof r);
}

static void t_vtrnq_s32(void)
{
    int32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int32x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vtrnq_s32(a0, a1); show("vtrnq_s32", &r, sizeof r);
}

static void t_vtrn1q_f32(void)
{
    float32x4_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    float32x4_t a1; memcpy(&a1, fpool + 32, sizeof a1);
    __auto_type r = vtrn1q_f32(a0, a1); show("vtrn1q_f32", &r, sizeof r);
}

static void t_vtrn2q_f32(void)
{
    float32x4_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    float32x4_t a1; memcpy(&a1, fpool + 32, sizeof a1);
    __auto_type r = vtrn2q_f32(a0, a1); show("vtrn2q_f32", &r, sizeof r);
}

static void t_vtrnq_f32(void)
{
    float32x4_t a0; memcpy(&a0, fpool + 0, sizeof a0);
    float32x4_t a1; memcpy(&a1, fpool + 32, sizeof a1);
    __auto_type r = vtrnq_f32(a0, a1); show("vtrnq_f32", &r, sizeof r);
}

static void t_vsriq_n_u32(void)
{
    uint32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint32x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vsriq_n_u32(a0, a1, 3); show("vsriq_n_u32", &r, sizeof r);
}

static void t_vsliq_n_u32(void)
{
    uint32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint32x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vsliq_n_u32(a0, a1, 3); show("vsliq_n_u32", &r, sizeof r);
}

static void t_vextq_u64(void)
{
    uint64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint64x2_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vextq_u64(a0, a1, 1); show("vextq_u64", &r, sizeof r);
}

static void t_vextq_u32(void)
{
    uint32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint32x4_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vextq_u32(a0, a1, 2); show("vextq_u32", &r, sizeof r);
}

static void t_vshrn_n_u64(void)
{
    uint64x2_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vshrn_n_u64(a0, 3); show("vshrn_n_u64", &r, sizeof r);
}

static void t_vshrn_n_u32(void)
{
    uint32x4_t a0; memcpy(&a0, pool + 0, sizeof a0);
    __auto_type r = vshrn_n_u32(a0, 3); show("vshrn_n_u32", &r, sizeof r);
}

static void t_vextq_u16(void)
{
    uint16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    uint16x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vextq_u16(a0, a1, 4); show("vextq_u16", &r, sizeof r);
}

static void t_vextq_s16(void)
{
    int16x8_t a0; memcpy(&a0, pool + 0, sizeof a0);
    int16x8_t a1; memcpy(&a1, pool + 32, sizeof a1);
    __auto_type r = vextq_s16(a0, a1, 4); show("vextq_s16", &r, sizeof r);
}

static void run_all(void)
{
    t_vdup_n_u8();
    t_vmov_n_u8();
    t_vld1_u8();
    t_vst1_u8();
    t_vget_lane_u8();
    t_vset_lane_u8();
    t_vdup_n_s8();
    t_vmov_n_s8();
    t_vld1_s8();
    t_vst1_s8();
    t_vget_lane_s8();
    t_vset_lane_s8();
    t_vdup_n_u16();
    t_vmov_n_u16();
    t_vld1_u16();
    t_vst1_u16();
    t_vget_lane_u16();
    t_vset_lane_u16();
    t_vdup_n_s16();
    t_vmov_n_s16();
    t_vld1_s16();
    t_vst1_s16();
    t_vget_lane_s16();
    t_vset_lane_s16();
    t_vdup_n_u32();
    t_vmov_n_u32();
    t_vld1_u32();
    t_vst1_u32();
    t_vget_lane_u32();
    t_vset_lane_u32();
    t_vdup_n_s32();
    t_vmov_n_s32();
    t_vld1_s32();
    t_vst1_s32();
    t_vget_lane_s32();
    t_vset_lane_s32();
    t_vdup_n_u64();
    t_vmov_n_u64();
    t_vld1_u64();
    t_vst1_u64();
    t_vget_lane_u64();
    t_vset_lane_u64();
    t_vdup_n_s64();
    t_vmov_n_s64();
    t_vld1_s64();
    t_vst1_s64();
    t_vget_lane_s64();
    t_vset_lane_s64();
    t_vdup_n_f32();
    t_vmov_n_f32();
    t_vld1_f32();
    t_vst1_f32();
    t_vget_lane_f32();
    t_vset_lane_f32();
    t_vdup_n_f64();
    t_vmov_n_f64();
    t_vld1_f64();
    t_vst1_f64();
    t_vget_lane_f64();
    t_vset_lane_f64();
    t_vld1_p8();
    t_vst1_p8();
    t_vget_lane_p8();
    t_vset_lane_p8();
    t_vld1_p16();
    t_vst1_p16();
    t_vget_lane_p16();
    t_vset_lane_p16();
    t_vld1_p64();
    t_vst1_p64();
    t_vget_lane_p64();
    t_vset_lane_p64();
    t_vadd_u8();
    t_vsub_u8();
    t_vmul_u8();
    t_vceq_u8();
    t_vcge_u8();
    t_vcgt_u8();
    t_vcle_u8();
    t_vclt_u8();
    t_vbsl_u8();
    t_vadd_s8();
    t_vsub_s8();
    t_vmul_s8();
    t_vceq_s8();
    t_vcge_s8();
    t_vcgt_s8();
    t_vcle_s8();
    t_vclt_s8();
    t_vneg_s8();
    t_vbsl_s8();
    t_vadd_u16();
    t_vsub_u16();
    t_vmul_u16();
    t_vceq_u16();
    t_vcge_u16();
    t_vcgt_u16();
    t_vcle_u16();
    t_vclt_u16();
    t_vbsl_u16();
    t_vadd_s16();
    t_vsub_s16();
    t_vmul_s16();
    t_vceq_s16();
    t_vcge_s16();
    t_vcgt_s16();
    t_vcle_s16();
    t_vclt_s16();
    t_vneg_s16();
    t_vbsl_s16();
    t_vadd_u32();
    t_vsub_u32();
    t_vmul_u32();
    t_vceq_u32();
    t_vcge_u32();
    t_vcgt_u32();
    t_vcle_u32();
    t_vclt_u32();
    t_vbsl_u32();
    t_vadd_s32();
    t_vsub_s32();
    t_vmul_s32();
    t_vceq_s32();
    t_vcge_s32();
    t_vcgt_s32();
    t_vcle_s32();
    t_vclt_s32();
    t_vneg_s32();
    t_vbsl_s32();
    t_vadd_u64();
    t_vsub_u64();
    t_vceq_u64();
    t_vcge_u64();
    t_vcgt_u64();
    t_vcle_u64();
    t_vclt_u64();
    t_vbsl_u64();
    t_vadd_s64();
    t_vsub_s64();
    t_vceq_s64();
    t_vcge_s64();
    t_vcgt_s64();
    t_vcle_s64();
    t_vclt_s64();
    t_vneg_s64();
    t_vbsl_s64();
    t_vadd_f32();
    t_vsub_f32();
    t_vmul_f32();
    t_vdiv_f32();
    t_vceq_f32();
    t_vcge_f32();
    t_vcgt_f32();
    t_vcle_f32();
    t_vclt_f32();
    t_vneg_f32();
    t_vbsl_f32();
    t_vadd_f64();
    t_vsub_f64();
    t_vmul_f64();
    t_vdiv_f64();
    t_vceq_f64();
    t_vcge_f64();
    t_vcgt_f64();
    t_vcle_f64();
    t_vclt_f64();
    t_vneg_f64();
    t_vbsl_f64();
    t_vand_u8();
    t_vorr_u8();
    t_veor_u8();
    t_vbic_u8();
    t_vorn_u8();
    t_vtst_u8();
    t_vmvn_u8();
    t_vmla_u8();
    t_vmls_u8();
    t_vshl_n_u8();
    t_vshr_n_u8();
    t_vand_s8();
    t_vorr_s8();
    t_veor_s8();
    t_vbic_s8();
    t_vorn_s8();
    t_vtst_s8();
    t_vmvn_s8();
    t_vmla_s8();
    t_vmls_s8();
    t_vshl_n_s8();
    t_vshr_n_s8();
    t_vand_u16();
    t_vorr_u16();
    t_veor_u16();
    t_vbic_u16();
    t_vorn_u16();
    t_vtst_u16();
    t_vmvn_u16();
    t_vmla_u16();
    t_vmls_u16();
    t_vshl_n_u16();
    t_vshr_n_u16();
    t_vand_s16();
    t_vorr_s16();
    t_veor_s16();
    t_vbic_s16();
    t_vorn_s16();
    t_vtst_s16();
    t_vmvn_s16();
    t_vmla_s16();
    t_vmls_s16();
    t_vshl_n_s16();
    t_vshr_n_s16();
    t_vand_u32();
    t_vorr_u32();
    t_veor_u32();
    t_vbic_u32();
    t_vorn_u32();
    t_vtst_u32();
    t_vmvn_u32();
    t_vmla_u32();
    t_vmls_u32();
    t_vshl_n_u32();
    t_vshr_n_u32();
    t_vand_s32();
    t_vorr_s32();
    t_veor_s32();
    t_vbic_s32();
    t_vorn_s32();
    t_vtst_s32();
    t_vmvn_s32();
    t_vmla_s32();
    t_vmls_s32();
    t_vshl_n_s32();
    t_vshr_n_s32();
    t_vand_u64();
    t_vorr_u64();
    t_veor_u64();
    t_vbic_u64();
    t_vorn_u64();
    t_vtst_u64();
    t_vshl_n_u64();
    t_vshr_n_u64();
    t_vand_s64();
    t_vorr_s64();
    t_veor_s64();
    t_vbic_s64();
    t_vorn_s64();
    t_vtst_s64();
    t_vshl_n_s64();
    t_vshr_n_s64();
    t_vreinterpret_u8_s8();
    t_vreinterpret_u8_u16();
    t_vreinterpret_u8_s16();
    t_vreinterpret_u8_u32();
    t_vreinterpret_u8_s32();
    t_vreinterpret_u8_u64();
    t_vreinterpret_u8_s64();
    t_vreinterpret_u8_f32();
    t_vreinterpret_u8_f64();
    t_vreinterpret_u8_p8();
    t_vreinterpret_u8_p16();
    t_vreinterpret_u8_p64();
    t_vreinterpret_s8_u8();
    t_vreinterpret_s8_u16();
    t_vreinterpret_s8_s16();
    t_vreinterpret_s8_u32();
    t_vreinterpret_s8_s32();
    t_vreinterpret_s8_u64();
    t_vreinterpret_s8_s64();
    t_vreinterpret_s8_f32();
    t_vreinterpret_s8_f64();
    t_vreinterpret_s8_p8();
    t_vreinterpret_s8_p16();
    t_vreinterpret_s8_p64();
    t_vreinterpret_u16_u8();
    t_vreinterpret_u16_s8();
    t_vreinterpret_u16_s16();
    t_vreinterpret_u16_u32();
    t_vreinterpret_u16_s32();
    t_vreinterpret_u16_u64();
    t_vreinterpret_u16_s64();
    t_vreinterpret_u16_f32();
    t_vreinterpret_u16_f64();
    t_vreinterpret_u16_p8();
    t_vreinterpret_u16_p16();
    t_vreinterpret_u16_p64();
    t_vreinterpret_s16_u8();
    t_vreinterpret_s16_s8();
    t_vreinterpret_s16_u16();
    t_vreinterpret_s16_u32();
    t_vreinterpret_s16_s32();
    t_vreinterpret_s16_u64();
    t_vreinterpret_s16_s64();
    t_vreinterpret_s16_f32();
    t_vreinterpret_s16_f64();
    t_vreinterpret_s16_p8();
    t_vreinterpret_s16_p16();
    t_vreinterpret_s16_p64();
    t_vreinterpret_u32_u8();
    t_vreinterpret_u32_s8();
    t_vreinterpret_u32_u16();
    t_vreinterpret_u32_s16();
    t_vreinterpret_u32_s32();
    t_vreinterpret_u32_u64();
    t_vreinterpret_u32_s64();
    t_vreinterpret_u32_f32();
    t_vreinterpret_u32_f64();
    t_vreinterpret_u32_p8();
    t_vreinterpret_u32_p16();
    t_vreinterpret_u32_p64();
    t_vreinterpret_s32_u8();
    t_vreinterpret_s32_s8();
    t_vreinterpret_s32_u16();
    t_vreinterpret_s32_s16();
    t_vreinterpret_s32_u32();
    t_vreinterpret_s32_u64();
    t_vreinterpret_s32_s64();
    t_vreinterpret_s32_f32();
    t_vreinterpret_s32_f64();
    t_vreinterpret_s32_p8();
    t_vreinterpret_s32_p16();
    t_vreinterpret_s32_p64();
    t_vreinterpret_u64_u8();
    t_vreinterpret_u64_s8();
    t_vreinterpret_u64_u16();
    t_vreinterpret_u64_s16();
    t_vreinterpret_u64_u32();
    t_vreinterpret_u64_s32();
    t_vreinterpret_u64_s64();
    t_vreinterpret_u64_f32();
    t_vreinterpret_u64_f64();
    t_vreinterpret_u64_p8();
    t_vreinterpret_u64_p16();
    t_vreinterpret_u64_p64();
    t_vreinterpret_s64_u8();
    t_vreinterpret_s64_s8();
    t_vreinterpret_s64_u16();
    t_vreinterpret_s64_s16();
    t_vreinterpret_s64_u32();
    t_vreinterpret_s64_s32();
    t_vreinterpret_s64_u64();
    t_vreinterpret_s64_f32();
    t_vreinterpret_s64_f64();
    t_vreinterpret_s64_p8();
    t_vreinterpret_s64_p16();
    t_vreinterpret_s64_p64();
    t_vreinterpret_f32_u8();
    t_vreinterpret_f32_s8();
    t_vreinterpret_f32_u16();
    t_vreinterpret_f32_s16();
    t_vreinterpret_f32_u32();
    t_vreinterpret_f32_s32();
    t_vreinterpret_f32_u64();
    t_vreinterpret_f32_s64();
    t_vreinterpret_f32_f64();
    t_vreinterpret_f32_p8();
    t_vreinterpret_f32_p16();
    t_vreinterpret_f32_p64();
    t_vreinterpret_f64_u8();
    t_vreinterpret_f64_s8();
    t_vreinterpret_f64_u16();
    t_vreinterpret_f64_s16();
    t_vreinterpret_f64_u32();
    t_vreinterpret_f64_s32();
    t_vreinterpret_f64_u64();
    t_vreinterpret_f64_s64();
    t_vreinterpret_f64_f32();
    t_vreinterpret_f64_p8();
    t_vreinterpret_f64_p16();
    t_vreinterpret_f64_p64();
    t_vreinterpret_p8_u8();
    t_vreinterpret_p8_s8();
    t_vreinterpret_p8_u16();
    t_vreinterpret_p8_s16();
    t_vreinterpret_p8_u32();
    t_vreinterpret_p8_s32();
    t_vreinterpret_p8_u64();
    t_vreinterpret_p8_s64();
    t_vreinterpret_p8_f32();
    t_vreinterpret_p8_f64();
    t_vreinterpret_p8_p16();
    t_vreinterpret_p8_p64();
    t_vreinterpret_p16_u8();
    t_vreinterpret_p16_s8();
    t_vreinterpret_p16_u16();
    t_vreinterpret_p16_s16();
    t_vreinterpret_p16_u32();
    t_vreinterpret_p16_s32();
    t_vreinterpret_p16_u64();
    t_vreinterpret_p16_s64();
    t_vreinterpret_p16_f32();
    t_vreinterpret_p16_f64();
    t_vreinterpret_p16_p8();
    t_vreinterpret_p16_p64();
    t_vreinterpret_p64_u8();
    t_vreinterpret_p64_s8();
    t_vreinterpret_p64_u16();
    t_vreinterpret_p64_s16();
    t_vreinterpret_p64_u32();
    t_vreinterpret_p64_s32();
    t_vreinterpret_p64_u64();
    t_vreinterpret_p64_s64();
    t_vreinterpret_p64_f32();
    t_vreinterpret_p64_f64();
    t_vreinterpret_p64_p8();
    t_vreinterpret_p64_p16();
    t_vdupq_n_u8();
    t_vmovq_n_u8();
    t_vld1q_u8();
    t_vst1q_u8();
    t_vgetq_lane_u8();
    t_vsetq_lane_u8();
    t_vdupq_n_s8();
    t_vmovq_n_s8();
    t_vld1q_s8();
    t_vst1q_s8();
    t_vgetq_lane_s8();
    t_vsetq_lane_s8();
    t_vdupq_n_u16();
    t_vmovq_n_u16();
    t_vld1q_u16();
    t_vst1q_u16();
    t_vgetq_lane_u16();
    t_vsetq_lane_u16();
    t_vdupq_n_s16();
    t_vmovq_n_s16();
    t_vld1q_s16();
    t_vst1q_s16();
    t_vgetq_lane_s16();
    t_vsetq_lane_s16();
    t_vdupq_n_u32();
    t_vmovq_n_u32();
    t_vld1q_u32();
    t_vst1q_u32();
    t_vgetq_lane_u32();
    t_vsetq_lane_u32();
    t_vdupq_n_s32();
    t_vmovq_n_s32();
    t_vld1q_s32();
    t_vst1q_s32();
    t_vgetq_lane_s32();
    t_vsetq_lane_s32();
    t_vdupq_n_u64();
    t_vmovq_n_u64();
    t_vld1q_u64();
    t_vst1q_u64();
    t_vgetq_lane_u64();
    t_vsetq_lane_u64();
    t_vdupq_n_s64();
    t_vmovq_n_s64();
    t_vld1q_s64();
    t_vst1q_s64();
    t_vgetq_lane_s64();
    t_vsetq_lane_s64();
    t_vdupq_n_f32();
    t_vmovq_n_f32();
    t_vld1q_f32();
    t_vst1q_f32();
    t_vgetq_lane_f32();
    t_vsetq_lane_f32();
    t_vdupq_n_f64();
    t_vmovq_n_f64();
    t_vld1q_f64();
    t_vst1q_f64();
    t_vgetq_lane_f64();
    t_vsetq_lane_f64();
    t_vld1q_p8();
    t_vst1q_p8();
    t_vgetq_lane_p8();
    t_vsetq_lane_p8();
    t_vld1q_p16();
    t_vst1q_p16();
    t_vgetq_lane_p16();
    t_vsetq_lane_p16();
    t_vld1q_p64();
    t_vst1q_p64();
    t_vgetq_lane_p64();
    t_vsetq_lane_p64();
    t_vaddq_u8();
    t_vsubq_u8();
    t_vmulq_u8();
    t_vceqq_u8();
    t_vcgeq_u8();
    t_vcgtq_u8();
    t_vcleq_u8();
    t_vcltq_u8();
    t_vbslq_u8();
    t_vaddq_s8();
    t_vsubq_s8();
    t_vmulq_s8();
    t_vceqq_s8();
    t_vcgeq_s8();
    t_vcgtq_s8();
    t_vcleq_s8();
    t_vcltq_s8();
    t_vnegq_s8();
    t_vbslq_s8();
    t_vaddq_u16();
    t_vsubq_u16();
    t_vmulq_u16();
    t_vceqq_u16();
    t_vcgeq_u16();
    t_vcgtq_u16();
    t_vcleq_u16();
    t_vcltq_u16();
    t_vbslq_u16();
    t_vaddq_s16();
    t_vsubq_s16();
    t_vmulq_s16();
    t_vceqq_s16();
    t_vcgeq_s16();
    t_vcgtq_s16();
    t_vcleq_s16();
    t_vcltq_s16();
    t_vnegq_s16();
    t_vbslq_s16();
    t_vaddq_u32();
    t_vsubq_u32();
    t_vmulq_u32();
    t_vceqq_u32();
    t_vcgeq_u32();
    t_vcgtq_u32();
    t_vcleq_u32();
    t_vcltq_u32();
    t_vbslq_u32();
    t_vaddq_s32();
    t_vsubq_s32();
    t_vmulq_s32();
    t_vceqq_s32();
    t_vcgeq_s32();
    t_vcgtq_s32();
    t_vcleq_s32();
    t_vcltq_s32();
    t_vnegq_s32();
    t_vbslq_s32();
    t_vaddq_u64();
    t_vsubq_u64();
    t_vceqq_u64();
    t_vcgeq_u64();
    t_vcgtq_u64();
    t_vcleq_u64();
    t_vcltq_u64();
    t_vbslq_u64();
    t_vaddq_s64();
    t_vsubq_s64();
    t_vceqq_s64();
    t_vcgeq_s64();
    t_vcgtq_s64();
    t_vcleq_s64();
    t_vcltq_s64();
    t_vnegq_s64();
    t_vbslq_s64();
    t_vaddq_f32();
    t_vsubq_f32();
    t_vmulq_f32();
    t_vdivq_f32();
    t_vceqq_f32();
    t_vcgeq_f32();
    t_vcgtq_f32();
    t_vcleq_f32();
    t_vcltq_f32();
    t_vnegq_f32();
    t_vbslq_f32();
    t_vaddq_f64();
    t_vsubq_f64();
    t_vmulq_f64();
    t_vdivq_f64();
    t_vceqq_f64();
    t_vcgeq_f64();
    t_vcgtq_f64();
    t_vcleq_f64();
    t_vcltq_f64();
    t_vnegq_f64();
    t_vbslq_f64();
    t_vandq_u8();
    t_vorrq_u8();
    t_veorq_u8();
    t_vbicq_u8();
    t_vornq_u8();
    t_vtstq_u8();
    t_vmvnq_u8();
    t_vmlaq_u8();
    t_vmlsq_u8();
    t_vshlq_n_u8();
    t_vshrq_n_u8();
    t_vandq_s8();
    t_vorrq_s8();
    t_veorq_s8();
    t_vbicq_s8();
    t_vornq_s8();
    t_vtstq_s8();
    t_vmvnq_s8();
    t_vmlaq_s8();
    t_vmlsq_s8();
    t_vshlq_n_s8();
    t_vshrq_n_s8();
    t_vandq_u16();
    t_vorrq_u16();
    t_veorq_u16();
    t_vbicq_u16();
    t_vornq_u16();
    t_vtstq_u16();
    t_vmvnq_u16();
    t_vmlaq_u16();
    t_vmlsq_u16();
    t_vshlq_n_u16();
    t_vshrq_n_u16();
    t_vandq_s16();
    t_vorrq_s16();
    t_veorq_s16();
    t_vbicq_s16();
    t_vornq_s16();
    t_vtstq_s16();
    t_vmvnq_s16();
    t_vmlaq_s16();
    t_vmlsq_s16();
    t_vshlq_n_s16();
    t_vshrq_n_s16();
    t_vandq_u32();
    t_vorrq_u32();
    t_veorq_u32();
    t_vbicq_u32();
    t_vornq_u32();
    t_vtstq_u32();
    t_vmvnq_u32();
    t_vmlaq_u32();
    t_vmlsq_u32();
    t_vshlq_n_u32();
    t_vshrq_n_u32();
    t_vandq_s32();
    t_vorrq_s32();
    t_veorq_s32();
    t_vbicq_s32();
    t_vornq_s32();
    t_vtstq_s32();
    t_vmvnq_s32();
    t_vmlaq_s32();
    t_vmlsq_s32();
    t_vshlq_n_s32();
    t_vshrq_n_s32();
    t_vandq_u64();
    t_vorrq_u64();
    t_veorq_u64();
    t_vbicq_u64();
    t_vornq_u64();
    t_vtstq_u64();
    t_vshlq_n_u64();
    t_vshrq_n_u64();
    t_vandq_s64();
    t_vorrq_s64();
    t_veorq_s64();
    t_vbicq_s64();
    t_vornq_s64();
    t_vtstq_s64();
    t_vshlq_n_s64();
    t_vshrq_n_s64();
    t_vreinterpretq_u8_s8();
    t_vreinterpretq_u8_u16();
    t_vreinterpretq_u8_s16();
    t_vreinterpretq_u8_u32();
    t_vreinterpretq_u8_s32();
    t_vreinterpretq_u8_u64();
    t_vreinterpretq_u8_s64();
    t_vreinterpretq_u8_f32();
    t_vreinterpretq_u8_f64();
    t_vreinterpretq_u8_p8();
    t_vreinterpretq_u8_p16();
    t_vreinterpretq_u8_p64();
    t_vreinterpretq_s8_u8();
    t_vreinterpretq_s8_u16();
    t_vreinterpretq_s8_s16();
    t_vreinterpretq_s8_u32();
    t_vreinterpretq_s8_s32();
    t_vreinterpretq_s8_u64();
    t_vreinterpretq_s8_s64();
    t_vreinterpretq_s8_f32();
    t_vreinterpretq_s8_f64();
    t_vreinterpretq_s8_p8();
    t_vreinterpretq_s8_p16();
    t_vreinterpretq_s8_p64();
    t_vreinterpretq_u16_u8();
    t_vreinterpretq_u16_s8();
    t_vreinterpretq_u16_s16();
    t_vreinterpretq_u16_u32();
    t_vreinterpretq_u16_s32();
    t_vreinterpretq_u16_u64();
    t_vreinterpretq_u16_s64();
    t_vreinterpretq_u16_f32();
    t_vreinterpretq_u16_f64();
    t_vreinterpretq_u16_p8();
    t_vreinterpretq_u16_p16();
    t_vreinterpretq_u16_p64();
    t_vreinterpretq_s16_u8();
    t_vreinterpretq_s16_s8();
    t_vreinterpretq_s16_u16();
    t_vreinterpretq_s16_u32();
    t_vreinterpretq_s16_s32();
    t_vreinterpretq_s16_u64();
    t_vreinterpretq_s16_s64();
    t_vreinterpretq_s16_f32();
    t_vreinterpretq_s16_f64();
    t_vreinterpretq_s16_p8();
    t_vreinterpretq_s16_p16();
    t_vreinterpretq_s16_p64();
    t_vreinterpretq_u32_u8();
    t_vreinterpretq_u32_s8();
    t_vreinterpretq_u32_u16();
    t_vreinterpretq_u32_s16();
    t_vreinterpretq_u32_s32();
    t_vreinterpretq_u32_u64();
    t_vreinterpretq_u32_s64();
    t_vreinterpretq_u32_f32();
    t_vreinterpretq_u32_f64();
    t_vreinterpretq_u32_p8();
    t_vreinterpretq_u32_p16();
    t_vreinterpretq_u32_p64();
    t_vreinterpretq_s32_u8();
    t_vreinterpretq_s32_s8();
    t_vreinterpretq_s32_u16();
    t_vreinterpretq_s32_s16();
    t_vreinterpretq_s32_u32();
    t_vreinterpretq_s32_u64();
    t_vreinterpretq_s32_s64();
    t_vreinterpretq_s32_f32();
    t_vreinterpretq_s32_f64();
    t_vreinterpretq_s32_p8();
    t_vreinterpretq_s32_p16();
    t_vreinterpretq_s32_p64();
    t_vreinterpretq_u64_u8();
    t_vreinterpretq_u64_s8();
    t_vreinterpretq_u64_u16();
    t_vreinterpretq_u64_s16();
    t_vreinterpretq_u64_u32();
    t_vreinterpretq_u64_s32();
    t_vreinterpretq_u64_s64();
    t_vreinterpretq_u64_f32();
    t_vreinterpretq_u64_f64();
    t_vreinterpretq_u64_p8();
    t_vreinterpretq_u64_p16();
    t_vreinterpretq_u64_p64();
    t_vreinterpretq_s64_u8();
    t_vreinterpretq_s64_s8();
    t_vreinterpretq_s64_u16();
    t_vreinterpretq_s64_s16();
    t_vreinterpretq_s64_u32();
    t_vreinterpretq_s64_s32();
    t_vreinterpretq_s64_u64();
    t_vreinterpretq_s64_f32();
    t_vreinterpretq_s64_f64();
    t_vreinterpretq_s64_p8();
    t_vreinterpretq_s64_p16();
    t_vreinterpretq_s64_p64();
    t_vreinterpretq_f32_u8();
    t_vreinterpretq_f32_s8();
    t_vreinterpretq_f32_u16();
    t_vreinterpretq_f32_s16();
    t_vreinterpretq_f32_u32();
    t_vreinterpretq_f32_s32();
    t_vreinterpretq_f32_u64();
    t_vreinterpretq_f32_s64();
    t_vreinterpretq_f32_f64();
    t_vreinterpretq_f32_p8();
    t_vreinterpretq_f32_p16();
    t_vreinterpretq_f32_p64();
    t_vreinterpretq_f64_u8();
    t_vreinterpretq_f64_s8();
    t_vreinterpretq_f64_u16();
    t_vreinterpretq_f64_s16();
    t_vreinterpretq_f64_u32();
    t_vreinterpretq_f64_s32();
    t_vreinterpretq_f64_u64();
    t_vreinterpretq_f64_s64();
    t_vreinterpretq_f64_f32();
    t_vreinterpretq_f64_p8();
    t_vreinterpretq_f64_p16();
    t_vreinterpretq_f64_p64();
    t_vreinterpretq_p8_u8();
    t_vreinterpretq_p8_s8();
    t_vreinterpretq_p8_u16();
    t_vreinterpretq_p8_s16();
    t_vreinterpretq_p8_u32();
    t_vreinterpretq_p8_s32();
    t_vreinterpretq_p8_u64();
    t_vreinterpretq_p8_s64();
    t_vreinterpretq_p8_f32();
    t_vreinterpretq_p8_f64();
    t_vreinterpretq_p8_p16();
    t_vreinterpretq_p8_p64();
    t_vreinterpretq_p16_u8();
    t_vreinterpretq_p16_s8();
    t_vreinterpretq_p16_u16();
    t_vreinterpretq_p16_s16();
    t_vreinterpretq_p16_u32();
    t_vreinterpretq_p16_s32();
    t_vreinterpretq_p16_u64();
    t_vreinterpretq_p16_s64();
    t_vreinterpretq_p16_f32();
    t_vreinterpretq_p16_f64();
    t_vreinterpretq_p16_p8();
    t_vreinterpretq_p16_p64();
    t_vreinterpretq_p64_u8();
    t_vreinterpretq_p64_s8();
    t_vreinterpretq_p64_u16();
    t_vreinterpretq_p64_s16();
    t_vreinterpretq_p64_u32();
    t_vreinterpretq_p64_s32();
    t_vreinterpretq_p64_u64();
    t_vreinterpretq_p64_s64();
    t_vreinterpretq_p64_f32();
    t_vreinterpretq_p64_f64();
    t_vreinterpretq_p64_p8();
    t_vreinterpretq_p64_p16();
    t_vld1q_dup_u32();
    t_vld1q_lane_u32();
    t_vld1q_u8_x4();
    t_vld3q_u8();
    t_vld3_dup_u8();
    t_vld3_lane_u8();
    t_vld4_u32();
    t_vst1_lane_u32();
    t_vst3_u8();
    t_vst4q_u8();
    t_vst4_lane_u32();
    t_vcombine_u8();
    t_vcombine_s8();
    t_vcombine_u64();
    t_vcombine_u16();
    t_vget_low_u8();
    t_vget_high_u8();
    t_vget_low_s8();
    t_vget_high_s8();
    t_vget_low_u64();
    t_vget_high_u64();
    t_vget_low_p64();
    t_vaddl_u8();
    t_vabdl_u8();
    t_vabdq_u16();
    t_vhadd_u8();
    t_vmovn_u16();
    t_vextq_u8();
    t_vext_u8();
    t_vmulq_p8();
    t_vmull_p64();
    t_vmull_high_p64();
    t_vrev32q_u16();
    t_vrbitq_u8();
    t_vqtbl1q_u8();
    t_vqtbx1q_u8();
    t_vqtbl4q_u8();
    t_vqtbx4q_u8();
    t_vtbl2_u8();
    t_vtbx2_u8();
    t_vpmin_s8();
    t_vminvq_s8();
    t_vminvq_u32();
    t_vaeseq_u8();
    t_vaesmcq_u8();
    t_vaesdq_u8();
    t_vaesimcq_u8();
    t_vreinterpretq_u8_p128();
    t_vcreate_u32();
    t_vcreate_u64();
    t_vcombine_u32();
    t_vget_low_u32();
    t_vget_high_u32();
    t_vmaxq_u8();
    t_vcntq_u8();
    t_vpaddlq_u8();
    t_vpaddlq_u16();
    t_vpaddlq_u32();
    t_vpadalq_u8();
    t_vmovn_u64();
    t_vmovn_u32();
    t_vmlal_u32();
    t_vmlal_high_u32();
    t_vuzpq_u32();
    t_vsha256su0q_u32();
    t_vsha256su1q_u32();
    t_vsha256hq_u32();
    t_vsha256h2q_u32();
    t_vshrn_n_u16();
    t_vsli_n_u8();
    t_vsriq_n_u8();
    t_vld2q_u16();
    t_vld4q_u8();
    t_vmaxvq_u8();
    t_vminvq_u8();
    t_vmaxvq_u32();
    t_vmaxvq_s16();
    t_vmaxvq_u16();
    t_vqsubq_u8();
    t_vqaddq_u8();
    t_vqaddq_s16();
    t_vqsubq_u16();
    t_vmaxq_s16();
    t_vget_low_f32();
    t_vget_high_f32();
    t_vcombine_f32();
    t_vrev64q_f32();
    t_vcvtq_s32_f32();
    t_vqaddq_s32();
    t_vqmovn_s32();
    t_vst1_lane_s16();
    t_vmaxq_u16();
    t_vminq_u16();
    t_vminq_s16();
    t_vcgtzq_s16();
    t_vcgtzq_s32();
    t_vcltzq_s16();
    t_vqtbl2q_u8();
    t_vqtbx2q_u8();
    t_vqtbl3q_u8();
    t_vuzp1q_u16();
    t_vuzp2q_u16();
    t_vuzp1q_u8();
    t_vuzp2q_u8();
    t_vuzp1q_u32();
    t_vuzp2q_u32();
    t_vzip1q_u16();
    t_vzip2q_u16();
    t_vzip1q_u32();
    t_vzip2q_u32();
    t_vmovl_u16();
    t_vmovl_s16();
    t_vmovl_high_u16();
    t_vmovl_high_u8();
    t_vget_low_u16();
    t_vget_high_u16();
    t_vtrn1_u8();
    t_vtrn2_u8();
    t_vtrn_u8();
    t_vtrn1_s8();
    t_vtrn2_s8();
    t_vtrn_s8();
    t_vtrn1_u16();
    t_vtrn2_u16();
    t_vtrn_u16();
    t_vtrn1_s16();
    t_vtrn2_s16();
    t_vtrn_s16();
    t_vtrn1_u32();
    t_vtrn2_u32();
    t_vtrn_u32();
    t_vtrn1_s32();
    t_vtrn2_s32();
    t_vtrn_s32();
    t_vtrn1_f32();
    t_vtrn2_f32();
    t_vtrn_f32();
    t_vtrn1q_u8();
    t_vtrn2q_u8();
    t_vtrnq_u8();
    t_vtrn1q_s8();
    t_vtrn2q_s8();
    t_vtrnq_s8();
    t_vtrn1q_u16();
    t_vtrn2q_u16();
    t_vtrnq_u16();
    t_vtrn1q_s16();
    t_vtrn2q_s16();
    t_vtrnq_s16();
    t_vtrn1q_u32();
    t_vtrn2q_u32();
    t_vtrnq_u32();
    t_vtrn1q_s32();
    t_vtrn2q_s32();
    t_vtrnq_s32();
    t_vtrn1q_f32();
    t_vtrn2q_f32();
    t_vtrnq_f32();
    t_vsriq_n_u32();
    t_vsliq_n_u32();
    t_vextq_u64();
    t_vextq_u32();
    t_vshrn_n_u64();
    t_vshrn_n_u32();
    t_vextq_u16();
    t_vextq_s16();
}

int main(void)
{
    for (int kind = 0; kind < 3; kind++) {
        set_pools(kind);
        printf("kind %d\n", kind);
        run_all();
    }
    return 0;
}
