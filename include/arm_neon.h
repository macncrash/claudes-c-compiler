/* CCC compiler bundled arm_neon.h - ARM NEON intrinsics */
#ifndef _ARM_NEON_H_INCLUDED
#define _ARM_NEON_H_INCLUDED

/* ===== Scalar types (ACLE) ===== */
typedef float float32_t;
typedef double float64_t;
typedef unsigned long long poly64_t;
typedef __uint128_t poly128_t;

/* ===== 128-bit vector types (Q registers) ===== */

typedef unsigned char uint8x16_t __attribute__((__vector_size__(16)));

typedef signed char int8x16_t __attribute__((__vector_size__(16)));

typedef unsigned short uint16x8_t __attribute__((__vector_size__(16)));

typedef short int16x8_t __attribute__((__vector_size__(16)));

typedef unsigned int uint32x4_t __attribute__((__vector_size__(16)));

typedef int int32x4_t __attribute__((__vector_size__(16)));

typedef unsigned long long uint64x2_t __attribute__((__vector_size__(16)));

typedef long long int64x2_t __attribute__((__vector_size__(16)));

typedef float float32x4_t __attribute__((__vector_size__(16)));

typedef double float64x2_t __attribute__((__vector_size__(16)));

/* Polynomial 128-bit types */
typedef unsigned char poly8x16_t __attribute__((__vector_size__(16)));

typedef unsigned short poly16x8_t __attribute__((__vector_size__(16)));

typedef unsigned long long poly64x2_t __attribute__((__vector_size__(16)));

/* ===== 64-bit vector types (D registers) ===== */

typedef unsigned char uint8x8_t __attribute__((__vector_size__(8)));

typedef signed char int8x8_t __attribute__((__vector_size__(8)));

typedef unsigned short uint16x4_t __attribute__((__vector_size__(8)));

typedef short int16x4_t __attribute__((__vector_size__(8)));

typedef unsigned int uint32x2_t __attribute__((__vector_size__(8)));

typedef int int32x2_t __attribute__((__vector_size__(8)));

typedef unsigned long long uint64x1_t __attribute__((__vector_size__(8)));

typedef long long int64x1_t __attribute__((__vector_size__(8)));

typedef float float32x2_t __attribute__((__vector_size__(8)));

typedef unsigned char poly8x8_t __attribute__((__vector_size__(8)));

typedef unsigned long long poly64x1_t __attribute__((__vector_size__(8)));

/* ===== Array-of-vectors (structure) types ===== */

/* uint8x8 multi-vector */
typedef double float64x1_t __attribute__((__vector_size__(8)));
typedef unsigned short poly16x4_t __attribute__((__vector_size__(8)));

typedef struct { uint8x8_t val[2]; } uint8x8x2_t;
typedef struct { uint8x8_t val[3]; } uint8x8x3_t;
typedef struct { uint8x8_t val[4]; } uint8x8x4_t;

/* uint8x16 multi-vector */
typedef struct { uint8x16_t val[2]; } uint8x16x2_t;
typedef struct { uint8x16_t val[3]; } uint8x16x3_t;
typedef struct { uint8x16_t val[4]; } uint8x16x4_t;

/* uint16x4 multi-vector */
typedef struct { uint16x4_t val[2]; } uint16x4x2_t;
typedef struct { uint16x4_t val[4]; } uint16x4x4_t;

/* uint16x8 multi-vector */
typedef struct { uint16x8_t val[2]; } uint16x8x2_t;
typedef struct { uint16x8_t val[4]; } uint16x8x4_t;

/* uint32x2 multi-vector */
typedef struct { uint32x2_t val[2]; } uint32x2x2_t;
typedef struct { uint32x2_t val[4]; } uint32x2x4_t;

/* uint32x4 multi-vector */
typedef struct { uint32x4_t val[2]; } uint32x4x2_t;
typedef struct { uint32x4_t val[4]; } uint32x4x4_t;

/* int8x8 multi-vector */
typedef struct { int8x8_t val[2]; } int8x8x2_t;

/* ================================================================== */
/*                          LOAD INTRINSICS                           */
/* ================================================================== */

/* --- vld1q: load one 128-bit vector --- */

/* --- vld1q_dup: load and broadcast --- */

typedef struct { int16x4_t val[2]; } int16x4x2_t;
typedef struct { int32x2_t val[2]; } int32x2x2_t;
typedef struct { float32x2_t val[2]; } float32x2x2_t;
typedef struct { int8x16_t val[2]; } int8x16x2_t;
typedef struct { int16x8_t val[2]; } int16x8x2_t;
typedef struct { int32x4_t val[2]; } int32x4x2_t;
typedef struct { float32x4_t val[2]; } float32x4x2_t;

/* ===== Regular families, as vector operations ===== */

static __inline__ uint8x8_t __attribute__((__always_inline__))
vdup_n_u8(unsigned char __a)
{
    return (uint8x8_t){ __a, __a, __a, __a, __a, __a, __a, __a };
}

static __inline__ uint8x8_t __attribute__((__always_inline__))
vmov_n_u8(unsigned char __a)
{
    return (uint8x8_t){ __a, __a, __a, __a, __a, __a, __a, __a };
}

static __inline__ uint8x8_t __attribute__((__always_inline__))
vld1_u8(const unsigned char *__p)
{
    uint8x8_t __ret;
    __builtin_memcpy(&__ret, __p, sizeof __ret);
    return __ret;
}

static __inline__ void __attribute__((__always_inline__))
vst1_u8(unsigned char *__p, uint8x8_t __a)
{
    __builtin_memcpy(__p, &__a, sizeof __a);
}

static __inline__ unsigned char __attribute__((__always_inline__))
vget_lane_u8(uint8x8_t __a, int __lane)
{
    return __a[__lane];
}

static __inline__ uint8x8_t __attribute__((__always_inline__))
vset_lane_u8(unsigned char __v, uint8x8_t __a, int __lane)
{
    __a[__lane] = __v;
    return __a;
}

static __inline__ int8x8_t __attribute__((__always_inline__))
vdup_n_s8(signed char __a)
{
    return (int8x8_t){ __a, __a, __a, __a, __a, __a, __a, __a };
}

static __inline__ int8x8_t __attribute__((__always_inline__))
vmov_n_s8(signed char __a)
{
    return (int8x8_t){ __a, __a, __a, __a, __a, __a, __a, __a };
}

static __inline__ int8x8_t __attribute__((__always_inline__))
vld1_s8(const signed char *__p)
{
    int8x8_t __ret;
    __builtin_memcpy(&__ret, __p, sizeof __ret);
    return __ret;
}

static __inline__ void __attribute__((__always_inline__))
vst1_s8(signed char *__p, int8x8_t __a)
{
    __builtin_memcpy(__p, &__a, sizeof __a);
}

static __inline__ signed char __attribute__((__always_inline__))
vget_lane_s8(int8x8_t __a, int __lane)
{
    return __a[__lane];
}

static __inline__ int8x8_t __attribute__((__always_inline__))
vset_lane_s8(signed char __v, int8x8_t __a, int __lane)
{
    __a[__lane] = __v;
    return __a;
}

static __inline__ uint16x4_t __attribute__((__always_inline__))
vdup_n_u16(unsigned short __a)
{
    return (uint16x4_t){ __a, __a, __a, __a };
}

static __inline__ uint16x4_t __attribute__((__always_inline__))
vmov_n_u16(unsigned short __a)
{
    return (uint16x4_t){ __a, __a, __a, __a };
}

static __inline__ uint16x4_t __attribute__((__always_inline__))
vld1_u16(const unsigned short *__p)
{
    uint16x4_t __ret;
    __builtin_memcpy(&__ret, __p, sizeof __ret);
    return __ret;
}

static __inline__ void __attribute__((__always_inline__))
vst1_u16(unsigned short *__p, uint16x4_t __a)
{
    __builtin_memcpy(__p, &__a, sizeof __a);
}

static __inline__ unsigned short __attribute__((__always_inline__))
vget_lane_u16(uint16x4_t __a, int __lane)
{
    return __a[__lane];
}

static __inline__ uint16x4_t __attribute__((__always_inline__))
vset_lane_u16(unsigned short __v, uint16x4_t __a, int __lane)
{
    __a[__lane] = __v;
    return __a;
}

static __inline__ int16x4_t __attribute__((__always_inline__))
vdup_n_s16(short __a)
{
    return (int16x4_t){ __a, __a, __a, __a };
}

static __inline__ int16x4_t __attribute__((__always_inline__))
vmov_n_s16(short __a)
{
    return (int16x4_t){ __a, __a, __a, __a };
}

static __inline__ int16x4_t __attribute__((__always_inline__))
vld1_s16(const short *__p)
{
    int16x4_t __ret;
    __builtin_memcpy(&__ret, __p, sizeof __ret);
    return __ret;
}

static __inline__ void __attribute__((__always_inline__))
vst1_s16(short *__p, int16x4_t __a)
{
    __builtin_memcpy(__p, &__a, sizeof __a);
}

static __inline__ short __attribute__((__always_inline__))
vget_lane_s16(int16x4_t __a, int __lane)
{
    return __a[__lane];
}

static __inline__ int16x4_t __attribute__((__always_inline__))
vset_lane_s16(short __v, int16x4_t __a, int __lane)
{
    __a[__lane] = __v;
    return __a;
}

static __inline__ uint32x2_t __attribute__((__always_inline__))
vdup_n_u32(unsigned int __a)
{
    return (uint32x2_t){ __a, __a };
}

static __inline__ uint32x2_t __attribute__((__always_inline__))
vmov_n_u32(unsigned int __a)
{
    return (uint32x2_t){ __a, __a };
}

static __inline__ uint32x2_t __attribute__((__always_inline__))
vld1_u32(const unsigned int *__p)
{
    uint32x2_t __ret;
    __builtin_memcpy(&__ret, __p, sizeof __ret);
    return __ret;
}

static __inline__ void __attribute__((__always_inline__))
vst1_u32(unsigned int *__p, uint32x2_t __a)
{
    __builtin_memcpy(__p, &__a, sizeof __a);
}

static __inline__ unsigned int __attribute__((__always_inline__))
vget_lane_u32(uint32x2_t __a, int __lane)
{
    return __a[__lane];
}

static __inline__ uint32x2_t __attribute__((__always_inline__))
vset_lane_u32(unsigned int __v, uint32x2_t __a, int __lane)
{
    __a[__lane] = __v;
    return __a;
}

static __inline__ int32x2_t __attribute__((__always_inline__))
vdup_n_s32(int __a)
{
    return (int32x2_t){ __a, __a };
}

static __inline__ int32x2_t __attribute__((__always_inline__))
vmov_n_s32(int __a)
{
    return (int32x2_t){ __a, __a };
}

static __inline__ int32x2_t __attribute__((__always_inline__))
vld1_s32(const int *__p)
{
    int32x2_t __ret;
    __builtin_memcpy(&__ret, __p, sizeof __ret);
    return __ret;
}

static __inline__ void __attribute__((__always_inline__))
vst1_s32(int *__p, int32x2_t __a)
{
    __builtin_memcpy(__p, &__a, sizeof __a);
}

static __inline__ int __attribute__((__always_inline__))
vget_lane_s32(int32x2_t __a, int __lane)
{
    return __a[__lane];
}

static __inline__ int32x2_t __attribute__((__always_inline__))
vset_lane_s32(int __v, int32x2_t __a, int __lane)
{
    __a[__lane] = __v;
    return __a;
}

static __inline__ uint64x1_t __attribute__((__always_inline__))
vdup_n_u64(unsigned long long __a)
{
    return (uint64x1_t){ __a };
}

static __inline__ uint64x1_t __attribute__((__always_inline__))
vmov_n_u64(unsigned long long __a)
{
    return (uint64x1_t){ __a };
}

static __inline__ uint64x1_t __attribute__((__always_inline__))
vld1_u64(const unsigned long long *__p)
{
    uint64x1_t __ret;
    __builtin_memcpy(&__ret, __p, sizeof __ret);
    return __ret;
}

static __inline__ void __attribute__((__always_inline__))
vst1_u64(unsigned long long *__p, uint64x1_t __a)
{
    __builtin_memcpy(__p, &__a, sizeof __a);
}

static __inline__ unsigned long long __attribute__((__always_inline__))
vget_lane_u64(uint64x1_t __a, int __lane)
{
    return __a[__lane];
}

static __inline__ uint64x1_t __attribute__((__always_inline__))
vset_lane_u64(unsigned long long __v, uint64x1_t __a, int __lane)
{
    __a[__lane] = __v;
    return __a;
}

static __inline__ int64x1_t __attribute__((__always_inline__))
vdup_n_s64(long long __a)
{
    return (int64x1_t){ __a };
}

static __inline__ int64x1_t __attribute__((__always_inline__))
vmov_n_s64(long long __a)
{
    return (int64x1_t){ __a };
}

static __inline__ int64x1_t __attribute__((__always_inline__))
vld1_s64(const long long *__p)
{
    int64x1_t __ret;
    __builtin_memcpy(&__ret, __p, sizeof __ret);
    return __ret;
}

static __inline__ void __attribute__((__always_inline__))
vst1_s64(long long *__p, int64x1_t __a)
{
    __builtin_memcpy(__p, &__a, sizeof __a);
}

static __inline__ long long __attribute__((__always_inline__))
vget_lane_s64(int64x1_t __a, int __lane)
{
    return __a[__lane];
}

static __inline__ int64x1_t __attribute__((__always_inline__))
vset_lane_s64(long long __v, int64x1_t __a, int __lane)
{
    __a[__lane] = __v;
    return __a;
}

static __inline__ float32x2_t __attribute__((__always_inline__))
vdup_n_f32(float __a)
{
    return (float32x2_t){ __a, __a };
}

static __inline__ float32x2_t __attribute__((__always_inline__))
vmov_n_f32(float __a)
{
    return (float32x2_t){ __a, __a };
}

static __inline__ float32x2_t __attribute__((__always_inline__))
vld1_f32(const float *__p)
{
    float32x2_t __ret;
    __builtin_memcpy(&__ret, __p, sizeof __ret);
    return __ret;
}

static __inline__ void __attribute__((__always_inline__))
vst1_f32(float *__p, float32x2_t __a)
{
    __builtin_memcpy(__p, &__a, sizeof __a);
}

static __inline__ float __attribute__((__always_inline__))
vget_lane_f32(float32x2_t __a, int __lane)
{
    return __a[__lane];
}

static __inline__ float32x2_t __attribute__((__always_inline__))
vset_lane_f32(float __v, float32x2_t __a, int __lane)
{
    __a[__lane] = __v;
    return __a;
}

static __inline__ float64x1_t __attribute__((__always_inline__))
vdup_n_f64(double __a)
{
    return (float64x1_t){ __a };
}

static __inline__ float64x1_t __attribute__((__always_inline__))
vmov_n_f64(double __a)
{
    return (float64x1_t){ __a };
}

static __inline__ float64x1_t __attribute__((__always_inline__))
vld1_f64(const double *__p)
{
    float64x1_t __ret;
    __builtin_memcpy(&__ret, __p, sizeof __ret);
    return __ret;
}

static __inline__ void __attribute__((__always_inline__))
vst1_f64(double *__p, float64x1_t __a)
{
    __builtin_memcpy(__p, &__a, sizeof __a);
}

static __inline__ double __attribute__((__always_inline__))
vget_lane_f64(float64x1_t __a, int __lane)
{
    return __a[__lane];
}

static __inline__ float64x1_t __attribute__((__always_inline__))
vset_lane_f64(double __v, float64x1_t __a, int __lane)
{
    __a[__lane] = __v;
    return __a;
}

static __inline__ poly8x8_t __attribute__((__always_inline__))
vld1_p8(const unsigned char *__p)
{
    poly8x8_t __ret;
    __builtin_memcpy(&__ret, __p, sizeof __ret);
    return __ret;
}

static __inline__ void __attribute__((__always_inline__))
vst1_p8(unsigned char *__p, poly8x8_t __a)
{
    __builtin_memcpy(__p, &__a, sizeof __a);
}

static __inline__ unsigned char __attribute__((__always_inline__))
vget_lane_p8(poly8x8_t __a, int __lane)
{
    return __a[__lane];
}

static __inline__ poly8x8_t __attribute__((__always_inline__))
vset_lane_p8(unsigned char __v, poly8x8_t __a, int __lane)
{
    __a[__lane] = __v;
    return __a;
}

static __inline__ poly16x4_t __attribute__((__always_inline__))
vld1_p16(const unsigned short *__p)
{
    poly16x4_t __ret;
    __builtin_memcpy(&__ret, __p, sizeof __ret);
    return __ret;
}

static __inline__ void __attribute__((__always_inline__))
vst1_p16(unsigned short *__p, poly16x4_t __a)
{
    __builtin_memcpy(__p, &__a, sizeof __a);
}

static __inline__ unsigned short __attribute__((__always_inline__))
vget_lane_p16(poly16x4_t __a, int __lane)
{
    return __a[__lane];
}

static __inline__ poly16x4_t __attribute__((__always_inline__))
vset_lane_p16(unsigned short __v, poly16x4_t __a, int __lane)
{
    __a[__lane] = __v;
    return __a;
}

static __inline__ poly64x1_t __attribute__((__always_inline__))
vld1_p64(const unsigned long long *__p)
{
    poly64x1_t __ret;
    __builtin_memcpy(&__ret, __p, sizeof __ret);
    return __ret;
}

static __inline__ void __attribute__((__always_inline__))
vst1_p64(unsigned long long *__p, poly64x1_t __a)
{
    __builtin_memcpy(__p, &__a, sizeof __a);
}

static __inline__ unsigned long long __attribute__((__always_inline__))
vget_lane_p64(poly64x1_t __a, int __lane)
{
    return __a[__lane];
}

static __inline__ poly64x1_t __attribute__((__always_inline__))
vset_lane_p64(unsigned long long __v, poly64x1_t __a, int __lane)
{
    __a[__lane] = __v;
    return __a;
}

static __inline__ uint8x8_t __attribute__((__always_inline__))
vadd_u8(uint8x8_t __a, uint8x8_t __b)
{
    return __a + __b;
}

static __inline__ uint8x8_t __attribute__((__always_inline__))
vsub_u8(uint8x8_t __a, uint8x8_t __b)
{
    return __a - __b;
}

static __inline__ uint8x8_t __attribute__((__always_inline__))
vmul_u8(uint8x8_t __a, uint8x8_t __b)
{
    return __a * __b;
}

static __inline__ uint8x8_t __attribute__((__always_inline__))
vceq_u8(uint8x8_t __a, uint8x8_t __b)
{
    return (uint8x8_t)(__a == __b);
}

static __inline__ uint8x8_t __attribute__((__always_inline__))
vcge_u8(uint8x8_t __a, uint8x8_t __b)
{
    return (uint8x8_t)(__a >= __b);
}

static __inline__ uint8x8_t __attribute__((__always_inline__))
vcgt_u8(uint8x8_t __a, uint8x8_t __b)
{
    return (uint8x8_t)(__a > __b);
}

static __inline__ uint8x8_t __attribute__((__always_inline__))
vcle_u8(uint8x8_t __a, uint8x8_t __b)
{
    return (uint8x8_t)(__a <= __b);
}

static __inline__ uint8x8_t __attribute__((__always_inline__))
vclt_u8(uint8x8_t __a, uint8x8_t __b)
{
    return (uint8x8_t)(__a < __b);
}

static __inline__ uint8x8_t __attribute__((__always_inline__))
vbsl_u8(uint8x8_t __s, uint8x8_t __a, uint8x8_t __b)
{
    return (uint8x8_t)((__s & (uint8x8_t)__a) | (~__s & (uint8x8_t)__b));
}

static __inline__ int8x8_t __attribute__((__always_inline__))
vadd_s8(int8x8_t __a, int8x8_t __b)
{
    return __a + __b;
}

static __inline__ int8x8_t __attribute__((__always_inline__))
vsub_s8(int8x8_t __a, int8x8_t __b)
{
    return __a - __b;
}

static __inline__ int8x8_t __attribute__((__always_inline__))
vmul_s8(int8x8_t __a, int8x8_t __b)
{
    return __a * __b;
}

static __inline__ uint8x8_t __attribute__((__always_inline__))
vceq_s8(int8x8_t __a, int8x8_t __b)
{
    return (uint8x8_t)(__a == __b);
}

static __inline__ uint8x8_t __attribute__((__always_inline__))
vcge_s8(int8x8_t __a, int8x8_t __b)
{
    return (uint8x8_t)(__a >= __b);
}

static __inline__ uint8x8_t __attribute__((__always_inline__))
vcgt_s8(int8x8_t __a, int8x8_t __b)
{
    return (uint8x8_t)(__a > __b);
}

static __inline__ uint8x8_t __attribute__((__always_inline__))
vcle_s8(int8x8_t __a, int8x8_t __b)
{
    return (uint8x8_t)(__a <= __b);
}

static __inline__ uint8x8_t __attribute__((__always_inline__))
vclt_s8(int8x8_t __a, int8x8_t __b)
{
    return (uint8x8_t)(__a < __b);
}

static __inline__ int8x8_t __attribute__((__always_inline__))
vneg_s8(int8x8_t __a)
{
    return -__a;
}

static __inline__ int8x8_t __attribute__((__always_inline__))
vbsl_s8(uint8x8_t __s, int8x8_t __a, int8x8_t __b)
{
    return (int8x8_t)((__s & (uint8x8_t)__a) | (~__s & (uint8x8_t)__b));
}

static __inline__ uint16x4_t __attribute__((__always_inline__))
vadd_u16(uint16x4_t __a, uint16x4_t __b)
{
    return __a + __b;
}

static __inline__ uint16x4_t __attribute__((__always_inline__))
vsub_u16(uint16x4_t __a, uint16x4_t __b)
{
    return __a - __b;
}

static __inline__ uint16x4_t __attribute__((__always_inline__))
vmul_u16(uint16x4_t __a, uint16x4_t __b)
{
    return __a * __b;
}

static __inline__ uint16x4_t __attribute__((__always_inline__))
vceq_u16(uint16x4_t __a, uint16x4_t __b)
{
    return (uint16x4_t)(__a == __b);
}

static __inline__ uint16x4_t __attribute__((__always_inline__))
vcge_u16(uint16x4_t __a, uint16x4_t __b)
{
    return (uint16x4_t)(__a >= __b);
}

static __inline__ uint16x4_t __attribute__((__always_inline__))
vcgt_u16(uint16x4_t __a, uint16x4_t __b)
{
    return (uint16x4_t)(__a > __b);
}

static __inline__ uint16x4_t __attribute__((__always_inline__))
vcle_u16(uint16x4_t __a, uint16x4_t __b)
{
    return (uint16x4_t)(__a <= __b);
}

static __inline__ uint16x4_t __attribute__((__always_inline__))
vclt_u16(uint16x4_t __a, uint16x4_t __b)
{
    return (uint16x4_t)(__a < __b);
}

static __inline__ uint16x4_t __attribute__((__always_inline__))
vbsl_u16(uint16x4_t __s, uint16x4_t __a, uint16x4_t __b)
{
    return (uint16x4_t)((__s & (uint16x4_t)__a) | (~__s & (uint16x4_t)__b));
}

static __inline__ int16x4_t __attribute__((__always_inline__))
vadd_s16(int16x4_t __a, int16x4_t __b)
{
    return __a + __b;
}

static __inline__ int16x4_t __attribute__((__always_inline__))
vsub_s16(int16x4_t __a, int16x4_t __b)
{
    return __a - __b;
}

static __inline__ int16x4_t __attribute__((__always_inline__))
vmul_s16(int16x4_t __a, int16x4_t __b)
{
    return __a * __b;
}

static __inline__ uint16x4_t __attribute__((__always_inline__))
vceq_s16(int16x4_t __a, int16x4_t __b)
{
    return (uint16x4_t)(__a == __b);
}

static __inline__ uint16x4_t __attribute__((__always_inline__))
vcge_s16(int16x4_t __a, int16x4_t __b)
{
    return (uint16x4_t)(__a >= __b);
}

static __inline__ uint16x4_t __attribute__((__always_inline__))
vcgt_s16(int16x4_t __a, int16x4_t __b)
{
    return (uint16x4_t)(__a > __b);
}

static __inline__ uint16x4_t __attribute__((__always_inline__))
vcle_s16(int16x4_t __a, int16x4_t __b)
{
    return (uint16x4_t)(__a <= __b);
}

static __inline__ uint16x4_t __attribute__((__always_inline__))
vclt_s16(int16x4_t __a, int16x4_t __b)
{
    return (uint16x4_t)(__a < __b);
}

static __inline__ int16x4_t __attribute__((__always_inline__))
vneg_s16(int16x4_t __a)
{
    return -__a;
}

static __inline__ int16x4_t __attribute__((__always_inline__))
vbsl_s16(uint16x4_t __s, int16x4_t __a, int16x4_t __b)
{
    return (int16x4_t)((__s & (uint16x4_t)__a) | (~__s & (uint16x4_t)__b));
}

static __inline__ uint32x2_t __attribute__((__always_inline__))
vadd_u32(uint32x2_t __a, uint32x2_t __b)
{
    return __a + __b;
}

static __inline__ uint32x2_t __attribute__((__always_inline__))
vsub_u32(uint32x2_t __a, uint32x2_t __b)
{
    return __a - __b;
}

static __inline__ uint32x2_t __attribute__((__always_inline__))
vmul_u32(uint32x2_t __a, uint32x2_t __b)
{
    return __a * __b;
}

static __inline__ uint32x2_t __attribute__((__always_inline__))
vceq_u32(uint32x2_t __a, uint32x2_t __b)
{
    return (uint32x2_t)(__a == __b);
}

static __inline__ uint32x2_t __attribute__((__always_inline__))
vcge_u32(uint32x2_t __a, uint32x2_t __b)
{
    return (uint32x2_t)(__a >= __b);
}

static __inline__ uint32x2_t __attribute__((__always_inline__))
vcgt_u32(uint32x2_t __a, uint32x2_t __b)
{
    return (uint32x2_t)(__a > __b);
}

static __inline__ uint32x2_t __attribute__((__always_inline__))
vcle_u32(uint32x2_t __a, uint32x2_t __b)
{
    return (uint32x2_t)(__a <= __b);
}

static __inline__ uint32x2_t __attribute__((__always_inline__))
vclt_u32(uint32x2_t __a, uint32x2_t __b)
{
    return (uint32x2_t)(__a < __b);
}

static __inline__ uint32x2_t __attribute__((__always_inline__))
vbsl_u32(uint32x2_t __s, uint32x2_t __a, uint32x2_t __b)
{
    return (uint32x2_t)((__s & (uint32x2_t)__a) | (~__s & (uint32x2_t)__b));
}

static __inline__ int32x2_t __attribute__((__always_inline__))
vadd_s32(int32x2_t __a, int32x2_t __b)
{
    return __a + __b;
}

static __inline__ int32x2_t __attribute__((__always_inline__))
vsub_s32(int32x2_t __a, int32x2_t __b)
{
    return __a - __b;
}

static __inline__ int32x2_t __attribute__((__always_inline__))
vmul_s32(int32x2_t __a, int32x2_t __b)
{
    return __a * __b;
}

static __inline__ uint32x2_t __attribute__((__always_inline__))
vceq_s32(int32x2_t __a, int32x2_t __b)
{
    return (uint32x2_t)(__a == __b);
}

static __inline__ uint32x2_t __attribute__((__always_inline__))
vcge_s32(int32x2_t __a, int32x2_t __b)
{
    return (uint32x2_t)(__a >= __b);
}

static __inline__ uint32x2_t __attribute__((__always_inline__))
vcgt_s32(int32x2_t __a, int32x2_t __b)
{
    return (uint32x2_t)(__a > __b);
}

static __inline__ uint32x2_t __attribute__((__always_inline__))
vcle_s32(int32x2_t __a, int32x2_t __b)
{
    return (uint32x2_t)(__a <= __b);
}

static __inline__ uint32x2_t __attribute__((__always_inline__))
vclt_s32(int32x2_t __a, int32x2_t __b)
{
    return (uint32x2_t)(__a < __b);
}

static __inline__ int32x2_t __attribute__((__always_inline__))
vneg_s32(int32x2_t __a)
{
    return -__a;
}

static __inline__ int32x2_t __attribute__((__always_inline__))
vbsl_s32(uint32x2_t __s, int32x2_t __a, int32x2_t __b)
{
    return (int32x2_t)((__s & (uint32x2_t)__a) | (~__s & (uint32x2_t)__b));
}

static __inline__ uint64x1_t __attribute__((__always_inline__))
vadd_u64(uint64x1_t __a, uint64x1_t __b)
{
    return __a + __b;
}

static __inline__ uint64x1_t __attribute__((__always_inline__))
vsub_u64(uint64x1_t __a, uint64x1_t __b)
{
    return __a - __b;
}

static __inline__ uint64x1_t __attribute__((__always_inline__))
vceq_u64(uint64x1_t __a, uint64x1_t __b)
{
    return (uint64x1_t)(__a == __b);
}

static __inline__ uint64x1_t __attribute__((__always_inline__))
vcge_u64(uint64x1_t __a, uint64x1_t __b)
{
    return (uint64x1_t)(__a >= __b);
}

static __inline__ uint64x1_t __attribute__((__always_inline__))
vcgt_u64(uint64x1_t __a, uint64x1_t __b)
{
    return (uint64x1_t)(__a > __b);
}

static __inline__ uint64x1_t __attribute__((__always_inline__))
vcle_u64(uint64x1_t __a, uint64x1_t __b)
{
    return (uint64x1_t)(__a <= __b);
}

static __inline__ uint64x1_t __attribute__((__always_inline__))
vclt_u64(uint64x1_t __a, uint64x1_t __b)
{
    return (uint64x1_t)(__a < __b);
}

static __inline__ uint64x1_t __attribute__((__always_inline__))
vbsl_u64(uint64x1_t __s, uint64x1_t __a, uint64x1_t __b)
{
    return (uint64x1_t)((__s & (uint64x1_t)__a) | (~__s & (uint64x1_t)__b));
}

static __inline__ int64x1_t __attribute__((__always_inline__))
vadd_s64(int64x1_t __a, int64x1_t __b)
{
    return __a + __b;
}

static __inline__ int64x1_t __attribute__((__always_inline__))
vsub_s64(int64x1_t __a, int64x1_t __b)
{
    return __a - __b;
}

static __inline__ uint64x1_t __attribute__((__always_inline__))
vceq_s64(int64x1_t __a, int64x1_t __b)
{
    return (uint64x1_t)(__a == __b);
}

static __inline__ uint64x1_t __attribute__((__always_inline__))
vcge_s64(int64x1_t __a, int64x1_t __b)
{
    return (uint64x1_t)(__a >= __b);
}

static __inline__ uint64x1_t __attribute__((__always_inline__))
vcgt_s64(int64x1_t __a, int64x1_t __b)
{
    return (uint64x1_t)(__a > __b);
}

static __inline__ uint64x1_t __attribute__((__always_inline__))
vcle_s64(int64x1_t __a, int64x1_t __b)
{
    return (uint64x1_t)(__a <= __b);
}

static __inline__ uint64x1_t __attribute__((__always_inline__))
vclt_s64(int64x1_t __a, int64x1_t __b)
{
    return (uint64x1_t)(__a < __b);
}

static __inline__ int64x1_t __attribute__((__always_inline__))
vneg_s64(int64x1_t __a)
{
    return -__a;
}

static __inline__ int64x1_t __attribute__((__always_inline__))
vbsl_s64(uint64x1_t __s, int64x1_t __a, int64x1_t __b)
{
    return (int64x1_t)((__s & (uint64x1_t)__a) | (~__s & (uint64x1_t)__b));
}

static __inline__ float32x2_t __attribute__((__always_inline__))
vadd_f32(float32x2_t __a, float32x2_t __b)
{
    return __a + __b;
}

static __inline__ float32x2_t __attribute__((__always_inline__))
vsub_f32(float32x2_t __a, float32x2_t __b)
{
    return __a - __b;
}

static __inline__ float32x2_t __attribute__((__always_inline__))
vmul_f32(float32x2_t __a, float32x2_t __b)
{
    return __a * __b;
}

static __inline__ float32x2_t __attribute__((__always_inline__))
vdiv_f32(float32x2_t __a, float32x2_t __b)
{
    return __a / __b;
}

static __inline__ uint32x2_t __attribute__((__always_inline__))
vceq_f32(float32x2_t __a, float32x2_t __b)
{
    return (uint32x2_t)(__a == __b);
}

static __inline__ uint32x2_t __attribute__((__always_inline__))
vcge_f32(float32x2_t __a, float32x2_t __b)
{
    return (uint32x2_t)(__a >= __b);
}

static __inline__ uint32x2_t __attribute__((__always_inline__))
vcgt_f32(float32x2_t __a, float32x2_t __b)
{
    return (uint32x2_t)(__a > __b);
}

static __inline__ uint32x2_t __attribute__((__always_inline__))
vcle_f32(float32x2_t __a, float32x2_t __b)
{
    return (uint32x2_t)(__a <= __b);
}

static __inline__ uint32x2_t __attribute__((__always_inline__))
vclt_f32(float32x2_t __a, float32x2_t __b)
{
    return (uint32x2_t)(__a < __b);
}

static __inline__ float32x2_t __attribute__((__always_inline__))
vneg_f32(float32x2_t __a)
{
    return -__a;
}

static __inline__ float32x2_t __attribute__((__always_inline__))
vbsl_f32(uint32x2_t __s, float32x2_t __a, float32x2_t __b)
{
    return (float32x2_t)((__s & (uint32x2_t)__a) | (~__s & (uint32x2_t)__b));
}

static __inline__ float64x1_t __attribute__((__always_inline__))
vadd_f64(float64x1_t __a, float64x1_t __b)
{
    return __a + __b;
}

static __inline__ float64x1_t __attribute__((__always_inline__))
vsub_f64(float64x1_t __a, float64x1_t __b)
{
    return __a - __b;
}

static __inline__ float64x1_t __attribute__((__always_inline__))
vmul_f64(float64x1_t __a, float64x1_t __b)
{
    return __a * __b;
}

static __inline__ float64x1_t __attribute__((__always_inline__))
vdiv_f64(float64x1_t __a, float64x1_t __b)
{
    return __a / __b;
}

static __inline__ uint64x1_t __attribute__((__always_inline__))
vceq_f64(float64x1_t __a, float64x1_t __b)
{
    return (uint64x1_t)(__a == __b);
}

static __inline__ uint64x1_t __attribute__((__always_inline__))
vcge_f64(float64x1_t __a, float64x1_t __b)
{
    return (uint64x1_t)(__a >= __b);
}

static __inline__ uint64x1_t __attribute__((__always_inline__))
vcgt_f64(float64x1_t __a, float64x1_t __b)
{
    return (uint64x1_t)(__a > __b);
}

static __inline__ uint64x1_t __attribute__((__always_inline__))
vcle_f64(float64x1_t __a, float64x1_t __b)
{
    return (uint64x1_t)(__a <= __b);
}

static __inline__ uint64x1_t __attribute__((__always_inline__))
vclt_f64(float64x1_t __a, float64x1_t __b)
{
    return (uint64x1_t)(__a < __b);
}

static __inline__ float64x1_t __attribute__((__always_inline__))
vneg_f64(float64x1_t __a)
{
    return -__a;
}

static __inline__ float64x1_t __attribute__((__always_inline__))
vbsl_f64(uint64x1_t __s, float64x1_t __a, float64x1_t __b)
{
    return (float64x1_t)((__s & (uint64x1_t)__a) | (~__s & (uint64x1_t)__b));
}

static __inline__ uint8x8_t __attribute__((__always_inline__))
vand_u8(uint8x8_t __a, uint8x8_t __b)
{
    return __a & __b;
}

static __inline__ uint8x8_t __attribute__((__always_inline__))
vorr_u8(uint8x8_t __a, uint8x8_t __b)
{
    return __a | __b;
}

static __inline__ uint8x8_t __attribute__((__always_inline__))
veor_u8(uint8x8_t __a, uint8x8_t __b)
{
    return __a ^ __b;
}

static __inline__ uint8x8_t __attribute__((__always_inline__))
vbic_u8(uint8x8_t __a, uint8x8_t __b)
{
    return __a & ~__b;
}

static __inline__ uint8x8_t __attribute__((__always_inline__))
vorn_u8(uint8x8_t __a, uint8x8_t __b)
{
    return __a | ~__b;
}

static __inline__ uint8x8_t __attribute__((__always_inline__))
vtst_u8(uint8x8_t __a, uint8x8_t __b)
{
    return (uint8x8_t)((__a & __b) != 0);
}

static __inline__ uint8x8_t __attribute__((__always_inline__))
vmvn_u8(uint8x8_t __a)
{
    return ~__a;
}

static __inline__ uint8x8_t __attribute__((__always_inline__))
vmla_u8(uint8x8_t __a, uint8x8_t __b, uint8x8_t __c)
{
    return __a + __b * __c;
}

static __inline__ uint8x8_t __attribute__((__always_inline__))
vmls_u8(uint8x8_t __a, uint8x8_t __b, uint8x8_t __c)
{
    return __a - __b * __c;
}

static __inline__ uint8x8_t __attribute__((__always_inline__))
vshl_n_u8(uint8x8_t __a, int __n)
{
    return __a << __n;
}

static __inline__ uint8x8_t __attribute__((__always_inline__))
vshr_n_u8(uint8x8_t __a, int __n)
{
    if (__n == 8)
        return __a ^ __a;
    return __a >> __n;
}

static __inline__ int8x8_t __attribute__((__always_inline__))
vand_s8(int8x8_t __a, int8x8_t __b)
{
    return __a & __b;
}

static __inline__ int8x8_t __attribute__((__always_inline__))
vorr_s8(int8x8_t __a, int8x8_t __b)
{
    return __a | __b;
}

static __inline__ int8x8_t __attribute__((__always_inline__))
veor_s8(int8x8_t __a, int8x8_t __b)
{
    return __a ^ __b;
}

static __inline__ int8x8_t __attribute__((__always_inline__))
vbic_s8(int8x8_t __a, int8x8_t __b)
{
    return __a & ~__b;
}

static __inline__ int8x8_t __attribute__((__always_inline__))
vorn_s8(int8x8_t __a, int8x8_t __b)
{
    return __a | ~__b;
}

static __inline__ uint8x8_t __attribute__((__always_inline__))
vtst_s8(int8x8_t __a, int8x8_t __b)
{
    return (uint8x8_t)((__a & __b) != 0);
}

static __inline__ int8x8_t __attribute__((__always_inline__))
vmvn_s8(int8x8_t __a)
{
    return ~__a;
}

static __inline__ int8x8_t __attribute__((__always_inline__))
vmla_s8(int8x8_t __a, int8x8_t __b, int8x8_t __c)
{
    return __a + __b * __c;
}

static __inline__ int8x8_t __attribute__((__always_inline__))
vmls_s8(int8x8_t __a, int8x8_t __b, int8x8_t __c)
{
    return __a - __b * __c;
}

static __inline__ int8x8_t __attribute__((__always_inline__))
vshl_n_s8(int8x8_t __a, int __n)
{
    return __a << __n;
}

static __inline__ int8x8_t __attribute__((__always_inline__))
vshr_n_s8(int8x8_t __a, int __n)
{
    return __a >> (__n == 8 ? 7 : __n);
}

static __inline__ uint16x4_t __attribute__((__always_inline__))
vand_u16(uint16x4_t __a, uint16x4_t __b)
{
    return __a & __b;
}

static __inline__ uint16x4_t __attribute__((__always_inline__))
vorr_u16(uint16x4_t __a, uint16x4_t __b)
{
    return __a | __b;
}

static __inline__ uint16x4_t __attribute__((__always_inline__))
veor_u16(uint16x4_t __a, uint16x4_t __b)
{
    return __a ^ __b;
}

static __inline__ uint16x4_t __attribute__((__always_inline__))
vbic_u16(uint16x4_t __a, uint16x4_t __b)
{
    return __a & ~__b;
}

static __inline__ uint16x4_t __attribute__((__always_inline__))
vorn_u16(uint16x4_t __a, uint16x4_t __b)
{
    return __a | ~__b;
}

static __inline__ uint16x4_t __attribute__((__always_inline__))
vtst_u16(uint16x4_t __a, uint16x4_t __b)
{
    return (uint16x4_t)((__a & __b) != 0);
}

static __inline__ uint16x4_t __attribute__((__always_inline__))
vmvn_u16(uint16x4_t __a)
{
    return ~__a;
}

static __inline__ uint16x4_t __attribute__((__always_inline__))
vmla_u16(uint16x4_t __a, uint16x4_t __b, uint16x4_t __c)
{
    return __a + __b * __c;
}

static __inline__ uint16x4_t __attribute__((__always_inline__))
vmls_u16(uint16x4_t __a, uint16x4_t __b, uint16x4_t __c)
{
    return __a - __b * __c;
}

static __inline__ uint16x4_t __attribute__((__always_inline__))
vshl_n_u16(uint16x4_t __a, int __n)
{
    return __a << __n;
}

static __inline__ uint16x4_t __attribute__((__always_inline__))
vshr_n_u16(uint16x4_t __a, int __n)
{
    if (__n == 16)
        return __a ^ __a;
    return __a >> __n;
}

static __inline__ int16x4_t __attribute__((__always_inline__))
vand_s16(int16x4_t __a, int16x4_t __b)
{
    return __a & __b;
}

static __inline__ int16x4_t __attribute__((__always_inline__))
vorr_s16(int16x4_t __a, int16x4_t __b)
{
    return __a | __b;
}

static __inline__ int16x4_t __attribute__((__always_inline__))
veor_s16(int16x4_t __a, int16x4_t __b)
{
    return __a ^ __b;
}

static __inline__ int16x4_t __attribute__((__always_inline__))
vbic_s16(int16x4_t __a, int16x4_t __b)
{
    return __a & ~__b;
}

static __inline__ int16x4_t __attribute__((__always_inline__))
vorn_s16(int16x4_t __a, int16x4_t __b)
{
    return __a | ~__b;
}

static __inline__ uint16x4_t __attribute__((__always_inline__))
vtst_s16(int16x4_t __a, int16x4_t __b)
{
    return (uint16x4_t)((__a & __b) != 0);
}

static __inline__ int16x4_t __attribute__((__always_inline__))
vmvn_s16(int16x4_t __a)
{
    return ~__a;
}

static __inline__ int16x4_t __attribute__((__always_inline__))
vmla_s16(int16x4_t __a, int16x4_t __b, int16x4_t __c)
{
    return __a + __b * __c;
}

static __inline__ int16x4_t __attribute__((__always_inline__))
vmls_s16(int16x4_t __a, int16x4_t __b, int16x4_t __c)
{
    return __a - __b * __c;
}

static __inline__ int16x4_t __attribute__((__always_inline__))
vshl_n_s16(int16x4_t __a, int __n)
{
    return __a << __n;
}

static __inline__ int16x4_t __attribute__((__always_inline__))
vshr_n_s16(int16x4_t __a, int __n)
{
    return __a >> (__n == 16 ? 15 : __n);
}

static __inline__ uint32x2_t __attribute__((__always_inline__))
vand_u32(uint32x2_t __a, uint32x2_t __b)
{
    return __a & __b;
}

static __inline__ uint32x2_t __attribute__((__always_inline__))
vorr_u32(uint32x2_t __a, uint32x2_t __b)
{
    return __a | __b;
}

static __inline__ uint32x2_t __attribute__((__always_inline__))
veor_u32(uint32x2_t __a, uint32x2_t __b)
{
    return __a ^ __b;
}

static __inline__ uint32x2_t __attribute__((__always_inline__))
vbic_u32(uint32x2_t __a, uint32x2_t __b)
{
    return __a & ~__b;
}

static __inline__ uint32x2_t __attribute__((__always_inline__))
vorn_u32(uint32x2_t __a, uint32x2_t __b)
{
    return __a | ~__b;
}

static __inline__ uint32x2_t __attribute__((__always_inline__))
vtst_u32(uint32x2_t __a, uint32x2_t __b)
{
    return (uint32x2_t)((__a & __b) != 0);
}

static __inline__ uint32x2_t __attribute__((__always_inline__))
vmvn_u32(uint32x2_t __a)
{
    return ~__a;
}

static __inline__ uint32x2_t __attribute__((__always_inline__))
vmla_u32(uint32x2_t __a, uint32x2_t __b, uint32x2_t __c)
{
    return __a + __b * __c;
}

static __inline__ uint32x2_t __attribute__((__always_inline__))
vmls_u32(uint32x2_t __a, uint32x2_t __b, uint32x2_t __c)
{
    return __a - __b * __c;
}

static __inline__ uint32x2_t __attribute__((__always_inline__))
vshl_n_u32(uint32x2_t __a, int __n)
{
    return __a << __n;
}

static __inline__ uint32x2_t __attribute__((__always_inline__))
vshr_n_u32(uint32x2_t __a, int __n)
{
    if (__n == 32)
        return __a ^ __a;
    return __a >> __n;
}

static __inline__ int32x2_t __attribute__((__always_inline__))
vand_s32(int32x2_t __a, int32x2_t __b)
{
    return __a & __b;
}

static __inline__ int32x2_t __attribute__((__always_inline__))
vorr_s32(int32x2_t __a, int32x2_t __b)
{
    return __a | __b;
}

static __inline__ int32x2_t __attribute__((__always_inline__))
veor_s32(int32x2_t __a, int32x2_t __b)
{
    return __a ^ __b;
}

static __inline__ int32x2_t __attribute__((__always_inline__))
vbic_s32(int32x2_t __a, int32x2_t __b)
{
    return __a & ~__b;
}

static __inline__ int32x2_t __attribute__((__always_inline__))
vorn_s32(int32x2_t __a, int32x2_t __b)
{
    return __a | ~__b;
}

static __inline__ uint32x2_t __attribute__((__always_inline__))
vtst_s32(int32x2_t __a, int32x2_t __b)
{
    return (uint32x2_t)((__a & __b) != 0);
}

static __inline__ int32x2_t __attribute__((__always_inline__))
vmvn_s32(int32x2_t __a)
{
    return ~__a;
}

static __inline__ int32x2_t __attribute__((__always_inline__))
vmla_s32(int32x2_t __a, int32x2_t __b, int32x2_t __c)
{
    return __a + __b * __c;
}

static __inline__ int32x2_t __attribute__((__always_inline__))
vmls_s32(int32x2_t __a, int32x2_t __b, int32x2_t __c)
{
    return __a - __b * __c;
}

static __inline__ int32x2_t __attribute__((__always_inline__))
vshl_n_s32(int32x2_t __a, int __n)
{
    return __a << __n;
}

static __inline__ int32x2_t __attribute__((__always_inline__))
vshr_n_s32(int32x2_t __a, int __n)
{
    return __a >> (__n == 32 ? 31 : __n);
}

static __inline__ uint64x1_t __attribute__((__always_inline__))
vand_u64(uint64x1_t __a, uint64x1_t __b)
{
    return __a & __b;
}

static __inline__ uint64x1_t __attribute__((__always_inline__))
vorr_u64(uint64x1_t __a, uint64x1_t __b)
{
    return __a | __b;
}

static __inline__ uint64x1_t __attribute__((__always_inline__))
veor_u64(uint64x1_t __a, uint64x1_t __b)
{
    return __a ^ __b;
}

static __inline__ uint64x1_t __attribute__((__always_inline__))
vbic_u64(uint64x1_t __a, uint64x1_t __b)
{
    return __a & ~__b;
}

static __inline__ uint64x1_t __attribute__((__always_inline__))
vorn_u64(uint64x1_t __a, uint64x1_t __b)
{
    return __a | ~__b;
}

static __inline__ uint64x1_t __attribute__((__always_inline__))
vtst_u64(uint64x1_t __a, uint64x1_t __b)
{
    return (uint64x1_t)((__a & __b) != 0);
}

static __inline__ uint64x1_t __attribute__((__always_inline__))
vshl_n_u64(uint64x1_t __a, int __n)
{
    return __a << __n;
}

static __inline__ uint64x1_t __attribute__((__always_inline__))
vshr_n_u64(uint64x1_t __a, int __n)
{
    if (__n == 64)
        return __a ^ __a;
    return __a >> __n;
}

static __inline__ int64x1_t __attribute__((__always_inline__))
vand_s64(int64x1_t __a, int64x1_t __b)
{
    return __a & __b;
}

static __inline__ int64x1_t __attribute__((__always_inline__))
vorr_s64(int64x1_t __a, int64x1_t __b)
{
    return __a | __b;
}

static __inline__ int64x1_t __attribute__((__always_inline__))
veor_s64(int64x1_t __a, int64x1_t __b)
{
    return __a ^ __b;
}

static __inline__ int64x1_t __attribute__((__always_inline__))
vbic_s64(int64x1_t __a, int64x1_t __b)
{
    return __a & ~__b;
}

static __inline__ int64x1_t __attribute__((__always_inline__))
vorn_s64(int64x1_t __a, int64x1_t __b)
{
    return __a | ~__b;
}

static __inline__ uint64x1_t __attribute__((__always_inline__))
vtst_s64(int64x1_t __a, int64x1_t __b)
{
    return (uint64x1_t)((__a & __b) != 0);
}

static __inline__ int64x1_t __attribute__((__always_inline__))
vshl_n_s64(int64x1_t __a, int __n)
{
    return __a << __n;
}

static __inline__ int64x1_t __attribute__((__always_inline__))
vshr_n_s64(int64x1_t __a, int __n)
{
    return __a >> (__n == 64 ? 63 : __n);
}

static __inline__ uint8x8_t __attribute__((__always_inline__))
vreinterpret_u8_s8(int8x8_t __a)
{
    return (uint8x8_t)__a;
}

static __inline__ uint8x8_t __attribute__((__always_inline__))
vreinterpret_u8_u16(uint16x4_t __a)
{
    return (uint8x8_t)__a;
}

static __inline__ uint8x8_t __attribute__((__always_inline__))
vreinterpret_u8_s16(int16x4_t __a)
{
    return (uint8x8_t)__a;
}

static __inline__ uint8x8_t __attribute__((__always_inline__))
vreinterpret_u8_u32(uint32x2_t __a)
{
    return (uint8x8_t)__a;
}

static __inline__ uint8x8_t __attribute__((__always_inline__))
vreinterpret_u8_s32(int32x2_t __a)
{
    return (uint8x8_t)__a;
}

static __inline__ uint8x8_t __attribute__((__always_inline__))
vreinterpret_u8_u64(uint64x1_t __a)
{
    return (uint8x8_t)__a;
}

static __inline__ uint8x8_t __attribute__((__always_inline__))
vreinterpret_u8_s64(int64x1_t __a)
{
    return (uint8x8_t)__a;
}

static __inline__ uint8x8_t __attribute__((__always_inline__))
vreinterpret_u8_f32(float32x2_t __a)
{
    return (uint8x8_t)__a;
}

static __inline__ uint8x8_t __attribute__((__always_inline__))
vreinterpret_u8_f64(float64x1_t __a)
{
    return (uint8x8_t)__a;
}

static __inline__ uint8x8_t __attribute__((__always_inline__))
vreinterpret_u8_p8(poly8x8_t __a)
{
    return (uint8x8_t)__a;
}

static __inline__ uint8x8_t __attribute__((__always_inline__))
vreinterpret_u8_p16(poly16x4_t __a)
{
    return (uint8x8_t)__a;
}

static __inline__ uint8x8_t __attribute__((__always_inline__))
vreinterpret_u8_p64(poly64x1_t __a)
{
    return (uint8x8_t)__a;
}

static __inline__ int8x8_t __attribute__((__always_inline__))
vreinterpret_s8_u8(uint8x8_t __a)
{
    return (int8x8_t)__a;
}

static __inline__ int8x8_t __attribute__((__always_inline__))
vreinterpret_s8_u16(uint16x4_t __a)
{
    return (int8x8_t)__a;
}

static __inline__ int8x8_t __attribute__((__always_inline__))
vreinterpret_s8_s16(int16x4_t __a)
{
    return (int8x8_t)__a;
}

static __inline__ int8x8_t __attribute__((__always_inline__))
vreinterpret_s8_u32(uint32x2_t __a)
{
    return (int8x8_t)__a;
}

static __inline__ int8x8_t __attribute__((__always_inline__))
vreinterpret_s8_s32(int32x2_t __a)
{
    return (int8x8_t)__a;
}

static __inline__ int8x8_t __attribute__((__always_inline__))
vreinterpret_s8_u64(uint64x1_t __a)
{
    return (int8x8_t)__a;
}

static __inline__ int8x8_t __attribute__((__always_inline__))
vreinterpret_s8_s64(int64x1_t __a)
{
    return (int8x8_t)__a;
}

static __inline__ int8x8_t __attribute__((__always_inline__))
vreinterpret_s8_f32(float32x2_t __a)
{
    return (int8x8_t)__a;
}

static __inline__ int8x8_t __attribute__((__always_inline__))
vreinterpret_s8_f64(float64x1_t __a)
{
    return (int8x8_t)__a;
}

static __inline__ int8x8_t __attribute__((__always_inline__))
vreinterpret_s8_p8(poly8x8_t __a)
{
    return (int8x8_t)__a;
}

static __inline__ int8x8_t __attribute__((__always_inline__))
vreinterpret_s8_p16(poly16x4_t __a)
{
    return (int8x8_t)__a;
}

static __inline__ int8x8_t __attribute__((__always_inline__))
vreinterpret_s8_p64(poly64x1_t __a)
{
    return (int8x8_t)__a;
}

static __inline__ uint16x4_t __attribute__((__always_inline__))
vreinterpret_u16_u8(uint8x8_t __a)
{
    return (uint16x4_t)__a;
}

static __inline__ uint16x4_t __attribute__((__always_inline__))
vreinterpret_u16_s8(int8x8_t __a)
{
    return (uint16x4_t)__a;
}

static __inline__ uint16x4_t __attribute__((__always_inline__))
vreinterpret_u16_s16(int16x4_t __a)
{
    return (uint16x4_t)__a;
}

static __inline__ uint16x4_t __attribute__((__always_inline__))
vreinterpret_u16_u32(uint32x2_t __a)
{
    return (uint16x4_t)__a;
}

static __inline__ uint16x4_t __attribute__((__always_inline__))
vreinterpret_u16_s32(int32x2_t __a)
{
    return (uint16x4_t)__a;
}

static __inline__ uint16x4_t __attribute__((__always_inline__))
vreinterpret_u16_u64(uint64x1_t __a)
{
    return (uint16x4_t)__a;
}

static __inline__ uint16x4_t __attribute__((__always_inline__))
vreinterpret_u16_s64(int64x1_t __a)
{
    return (uint16x4_t)__a;
}

static __inline__ uint16x4_t __attribute__((__always_inline__))
vreinterpret_u16_f32(float32x2_t __a)
{
    return (uint16x4_t)__a;
}

static __inline__ uint16x4_t __attribute__((__always_inline__))
vreinterpret_u16_f64(float64x1_t __a)
{
    return (uint16x4_t)__a;
}

static __inline__ uint16x4_t __attribute__((__always_inline__))
vreinterpret_u16_p8(poly8x8_t __a)
{
    return (uint16x4_t)__a;
}

static __inline__ uint16x4_t __attribute__((__always_inline__))
vreinterpret_u16_p16(poly16x4_t __a)
{
    return (uint16x4_t)__a;
}

static __inline__ uint16x4_t __attribute__((__always_inline__))
vreinterpret_u16_p64(poly64x1_t __a)
{
    return (uint16x4_t)__a;
}

static __inline__ int16x4_t __attribute__((__always_inline__))
vreinterpret_s16_u8(uint8x8_t __a)
{
    return (int16x4_t)__a;
}

static __inline__ int16x4_t __attribute__((__always_inline__))
vreinterpret_s16_s8(int8x8_t __a)
{
    return (int16x4_t)__a;
}

static __inline__ int16x4_t __attribute__((__always_inline__))
vreinterpret_s16_u16(uint16x4_t __a)
{
    return (int16x4_t)__a;
}

static __inline__ int16x4_t __attribute__((__always_inline__))
vreinterpret_s16_u32(uint32x2_t __a)
{
    return (int16x4_t)__a;
}

static __inline__ int16x4_t __attribute__((__always_inline__))
vreinterpret_s16_s32(int32x2_t __a)
{
    return (int16x4_t)__a;
}

static __inline__ int16x4_t __attribute__((__always_inline__))
vreinterpret_s16_u64(uint64x1_t __a)
{
    return (int16x4_t)__a;
}

static __inline__ int16x4_t __attribute__((__always_inline__))
vreinterpret_s16_s64(int64x1_t __a)
{
    return (int16x4_t)__a;
}

static __inline__ int16x4_t __attribute__((__always_inline__))
vreinterpret_s16_f32(float32x2_t __a)
{
    return (int16x4_t)__a;
}

static __inline__ int16x4_t __attribute__((__always_inline__))
vreinterpret_s16_f64(float64x1_t __a)
{
    return (int16x4_t)__a;
}

static __inline__ int16x4_t __attribute__((__always_inline__))
vreinterpret_s16_p8(poly8x8_t __a)
{
    return (int16x4_t)__a;
}

static __inline__ int16x4_t __attribute__((__always_inline__))
vreinterpret_s16_p16(poly16x4_t __a)
{
    return (int16x4_t)__a;
}

static __inline__ int16x4_t __attribute__((__always_inline__))
vreinterpret_s16_p64(poly64x1_t __a)
{
    return (int16x4_t)__a;
}

static __inline__ uint32x2_t __attribute__((__always_inline__))
vreinterpret_u32_u8(uint8x8_t __a)
{
    return (uint32x2_t)__a;
}

static __inline__ uint32x2_t __attribute__((__always_inline__))
vreinterpret_u32_s8(int8x8_t __a)
{
    return (uint32x2_t)__a;
}

static __inline__ uint32x2_t __attribute__((__always_inline__))
vreinterpret_u32_u16(uint16x4_t __a)
{
    return (uint32x2_t)__a;
}

static __inline__ uint32x2_t __attribute__((__always_inline__))
vreinterpret_u32_s16(int16x4_t __a)
{
    return (uint32x2_t)__a;
}

static __inline__ uint32x2_t __attribute__((__always_inline__))
vreinterpret_u32_s32(int32x2_t __a)
{
    return (uint32x2_t)__a;
}

static __inline__ uint32x2_t __attribute__((__always_inline__))
vreinterpret_u32_u64(uint64x1_t __a)
{
    return (uint32x2_t)__a;
}

static __inline__ uint32x2_t __attribute__((__always_inline__))
vreinterpret_u32_s64(int64x1_t __a)
{
    return (uint32x2_t)__a;
}

static __inline__ uint32x2_t __attribute__((__always_inline__))
vreinterpret_u32_f32(float32x2_t __a)
{
    return (uint32x2_t)__a;
}

static __inline__ uint32x2_t __attribute__((__always_inline__))
vreinterpret_u32_f64(float64x1_t __a)
{
    return (uint32x2_t)__a;
}

static __inline__ uint32x2_t __attribute__((__always_inline__))
vreinterpret_u32_p8(poly8x8_t __a)
{
    return (uint32x2_t)__a;
}

static __inline__ uint32x2_t __attribute__((__always_inline__))
vreinterpret_u32_p16(poly16x4_t __a)
{
    return (uint32x2_t)__a;
}

static __inline__ uint32x2_t __attribute__((__always_inline__))
vreinterpret_u32_p64(poly64x1_t __a)
{
    return (uint32x2_t)__a;
}

static __inline__ int32x2_t __attribute__((__always_inline__))
vreinterpret_s32_u8(uint8x8_t __a)
{
    return (int32x2_t)__a;
}

static __inline__ int32x2_t __attribute__((__always_inline__))
vreinterpret_s32_s8(int8x8_t __a)
{
    return (int32x2_t)__a;
}

static __inline__ int32x2_t __attribute__((__always_inline__))
vreinterpret_s32_u16(uint16x4_t __a)
{
    return (int32x2_t)__a;
}

static __inline__ int32x2_t __attribute__((__always_inline__))
vreinterpret_s32_s16(int16x4_t __a)
{
    return (int32x2_t)__a;
}

static __inline__ int32x2_t __attribute__((__always_inline__))
vreinterpret_s32_u32(uint32x2_t __a)
{
    return (int32x2_t)__a;
}

static __inline__ int32x2_t __attribute__((__always_inline__))
vreinterpret_s32_u64(uint64x1_t __a)
{
    return (int32x2_t)__a;
}

static __inline__ int32x2_t __attribute__((__always_inline__))
vreinterpret_s32_s64(int64x1_t __a)
{
    return (int32x2_t)__a;
}

static __inline__ int32x2_t __attribute__((__always_inline__))
vreinterpret_s32_f32(float32x2_t __a)
{
    return (int32x2_t)__a;
}

static __inline__ int32x2_t __attribute__((__always_inline__))
vreinterpret_s32_f64(float64x1_t __a)
{
    return (int32x2_t)__a;
}

static __inline__ int32x2_t __attribute__((__always_inline__))
vreinterpret_s32_p8(poly8x8_t __a)
{
    return (int32x2_t)__a;
}

static __inline__ int32x2_t __attribute__((__always_inline__))
vreinterpret_s32_p16(poly16x4_t __a)
{
    return (int32x2_t)__a;
}

static __inline__ int32x2_t __attribute__((__always_inline__))
vreinterpret_s32_p64(poly64x1_t __a)
{
    return (int32x2_t)__a;
}

static __inline__ uint64x1_t __attribute__((__always_inline__))
vreinterpret_u64_u8(uint8x8_t __a)
{
    return (uint64x1_t)__a;
}

static __inline__ uint64x1_t __attribute__((__always_inline__))
vreinterpret_u64_s8(int8x8_t __a)
{
    return (uint64x1_t)__a;
}

static __inline__ uint64x1_t __attribute__((__always_inline__))
vreinterpret_u64_u16(uint16x4_t __a)
{
    return (uint64x1_t)__a;
}

static __inline__ uint64x1_t __attribute__((__always_inline__))
vreinterpret_u64_s16(int16x4_t __a)
{
    return (uint64x1_t)__a;
}

static __inline__ uint64x1_t __attribute__((__always_inline__))
vreinterpret_u64_u32(uint32x2_t __a)
{
    return (uint64x1_t)__a;
}

static __inline__ uint64x1_t __attribute__((__always_inline__))
vreinterpret_u64_s32(int32x2_t __a)
{
    return (uint64x1_t)__a;
}

static __inline__ uint64x1_t __attribute__((__always_inline__))
vreinterpret_u64_s64(int64x1_t __a)
{
    return (uint64x1_t)__a;
}

static __inline__ uint64x1_t __attribute__((__always_inline__))
vreinterpret_u64_f32(float32x2_t __a)
{
    return (uint64x1_t)__a;
}

static __inline__ uint64x1_t __attribute__((__always_inline__))
vreinterpret_u64_f64(float64x1_t __a)
{
    return (uint64x1_t)__a;
}

static __inline__ uint64x1_t __attribute__((__always_inline__))
vreinterpret_u64_p8(poly8x8_t __a)
{
    return (uint64x1_t)__a;
}

static __inline__ uint64x1_t __attribute__((__always_inline__))
vreinterpret_u64_p16(poly16x4_t __a)
{
    return (uint64x1_t)__a;
}

static __inline__ uint64x1_t __attribute__((__always_inline__))
vreinterpret_u64_p64(poly64x1_t __a)
{
    return (uint64x1_t)__a;
}

static __inline__ int64x1_t __attribute__((__always_inline__))
vreinterpret_s64_u8(uint8x8_t __a)
{
    return (int64x1_t)__a;
}

static __inline__ int64x1_t __attribute__((__always_inline__))
vreinterpret_s64_s8(int8x8_t __a)
{
    return (int64x1_t)__a;
}

static __inline__ int64x1_t __attribute__((__always_inline__))
vreinterpret_s64_u16(uint16x4_t __a)
{
    return (int64x1_t)__a;
}

static __inline__ int64x1_t __attribute__((__always_inline__))
vreinterpret_s64_s16(int16x4_t __a)
{
    return (int64x1_t)__a;
}

static __inline__ int64x1_t __attribute__((__always_inline__))
vreinterpret_s64_u32(uint32x2_t __a)
{
    return (int64x1_t)__a;
}

static __inline__ int64x1_t __attribute__((__always_inline__))
vreinterpret_s64_s32(int32x2_t __a)
{
    return (int64x1_t)__a;
}

static __inline__ int64x1_t __attribute__((__always_inline__))
vreinterpret_s64_u64(uint64x1_t __a)
{
    return (int64x1_t)__a;
}

static __inline__ int64x1_t __attribute__((__always_inline__))
vreinterpret_s64_f32(float32x2_t __a)
{
    return (int64x1_t)__a;
}

static __inline__ int64x1_t __attribute__((__always_inline__))
vreinterpret_s64_f64(float64x1_t __a)
{
    return (int64x1_t)__a;
}

static __inline__ int64x1_t __attribute__((__always_inline__))
vreinterpret_s64_p8(poly8x8_t __a)
{
    return (int64x1_t)__a;
}

static __inline__ int64x1_t __attribute__((__always_inline__))
vreinterpret_s64_p16(poly16x4_t __a)
{
    return (int64x1_t)__a;
}

static __inline__ int64x1_t __attribute__((__always_inline__))
vreinterpret_s64_p64(poly64x1_t __a)
{
    return (int64x1_t)__a;
}

static __inline__ float32x2_t __attribute__((__always_inline__))
vreinterpret_f32_u8(uint8x8_t __a)
{
    return (float32x2_t)__a;
}

static __inline__ float32x2_t __attribute__((__always_inline__))
vreinterpret_f32_s8(int8x8_t __a)
{
    return (float32x2_t)__a;
}

static __inline__ float32x2_t __attribute__((__always_inline__))
vreinterpret_f32_u16(uint16x4_t __a)
{
    return (float32x2_t)__a;
}

static __inline__ float32x2_t __attribute__((__always_inline__))
vreinterpret_f32_s16(int16x4_t __a)
{
    return (float32x2_t)__a;
}

static __inline__ float32x2_t __attribute__((__always_inline__))
vreinterpret_f32_u32(uint32x2_t __a)
{
    return (float32x2_t)__a;
}

static __inline__ float32x2_t __attribute__((__always_inline__))
vreinterpret_f32_s32(int32x2_t __a)
{
    return (float32x2_t)__a;
}

static __inline__ float32x2_t __attribute__((__always_inline__))
vreinterpret_f32_u64(uint64x1_t __a)
{
    return (float32x2_t)__a;
}

static __inline__ float32x2_t __attribute__((__always_inline__))
vreinterpret_f32_s64(int64x1_t __a)
{
    return (float32x2_t)__a;
}

static __inline__ float32x2_t __attribute__((__always_inline__))
vreinterpret_f32_f64(float64x1_t __a)
{
    return (float32x2_t)__a;
}

static __inline__ float32x2_t __attribute__((__always_inline__))
vreinterpret_f32_p8(poly8x8_t __a)
{
    return (float32x2_t)__a;
}

static __inline__ float32x2_t __attribute__((__always_inline__))
vreinterpret_f32_p16(poly16x4_t __a)
{
    return (float32x2_t)__a;
}

static __inline__ float32x2_t __attribute__((__always_inline__))
vreinterpret_f32_p64(poly64x1_t __a)
{
    return (float32x2_t)__a;
}

static __inline__ float64x1_t __attribute__((__always_inline__))
vreinterpret_f64_u8(uint8x8_t __a)
{
    return (float64x1_t)__a;
}

static __inline__ float64x1_t __attribute__((__always_inline__))
vreinterpret_f64_s8(int8x8_t __a)
{
    return (float64x1_t)__a;
}

static __inline__ float64x1_t __attribute__((__always_inline__))
vreinterpret_f64_u16(uint16x4_t __a)
{
    return (float64x1_t)__a;
}

static __inline__ float64x1_t __attribute__((__always_inline__))
vreinterpret_f64_s16(int16x4_t __a)
{
    return (float64x1_t)__a;
}

static __inline__ float64x1_t __attribute__((__always_inline__))
vreinterpret_f64_u32(uint32x2_t __a)
{
    return (float64x1_t)__a;
}

static __inline__ float64x1_t __attribute__((__always_inline__))
vreinterpret_f64_s32(int32x2_t __a)
{
    return (float64x1_t)__a;
}

static __inline__ float64x1_t __attribute__((__always_inline__))
vreinterpret_f64_u64(uint64x1_t __a)
{
    return (float64x1_t)__a;
}

static __inline__ float64x1_t __attribute__((__always_inline__))
vreinterpret_f64_s64(int64x1_t __a)
{
    return (float64x1_t)__a;
}

static __inline__ float64x1_t __attribute__((__always_inline__))
vreinterpret_f64_f32(float32x2_t __a)
{
    return (float64x1_t)__a;
}

static __inline__ float64x1_t __attribute__((__always_inline__))
vreinterpret_f64_p8(poly8x8_t __a)
{
    return (float64x1_t)__a;
}

static __inline__ float64x1_t __attribute__((__always_inline__))
vreinterpret_f64_p16(poly16x4_t __a)
{
    return (float64x1_t)__a;
}

static __inline__ float64x1_t __attribute__((__always_inline__))
vreinterpret_f64_p64(poly64x1_t __a)
{
    return (float64x1_t)__a;
}

static __inline__ poly8x8_t __attribute__((__always_inline__))
vreinterpret_p8_u8(uint8x8_t __a)
{
    return (poly8x8_t)__a;
}

static __inline__ poly8x8_t __attribute__((__always_inline__))
vreinterpret_p8_s8(int8x8_t __a)
{
    return (poly8x8_t)__a;
}

static __inline__ poly8x8_t __attribute__((__always_inline__))
vreinterpret_p8_u16(uint16x4_t __a)
{
    return (poly8x8_t)__a;
}

static __inline__ poly8x8_t __attribute__((__always_inline__))
vreinterpret_p8_s16(int16x4_t __a)
{
    return (poly8x8_t)__a;
}

static __inline__ poly8x8_t __attribute__((__always_inline__))
vreinterpret_p8_u32(uint32x2_t __a)
{
    return (poly8x8_t)__a;
}

static __inline__ poly8x8_t __attribute__((__always_inline__))
vreinterpret_p8_s32(int32x2_t __a)
{
    return (poly8x8_t)__a;
}

static __inline__ poly8x8_t __attribute__((__always_inline__))
vreinterpret_p8_u64(uint64x1_t __a)
{
    return (poly8x8_t)__a;
}

static __inline__ poly8x8_t __attribute__((__always_inline__))
vreinterpret_p8_s64(int64x1_t __a)
{
    return (poly8x8_t)__a;
}

static __inline__ poly8x8_t __attribute__((__always_inline__))
vreinterpret_p8_f32(float32x2_t __a)
{
    return (poly8x8_t)__a;
}

static __inline__ poly8x8_t __attribute__((__always_inline__))
vreinterpret_p8_f64(float64x1_t __a)
{
    return (poly8x8_t)__a;
}

static __inline__ poly8x8_t __attribute__((__always_inline__))
vreinterpret_p8_p16(poly16x4_t __a)
{
    return (poly8x8_t)__a;
}

static __inline__ poly8x8_t __attribute__((__always_inline__))
vreinterpret_p8_p64(poly64x1_t __a)
{
    return (poly8x8_t)__a;
}

static __inline__ poly16x4_t __attribute__((__always_inline__))
vreinterpret_p16_u8(uint8x8_t __a)
{
    return (poly16x4_t)__a;
}

static __inline__ poly16x4_t __attribute__((__always_inline__))
vreinterpret_p16_s8(int8x8_t __a)
{
    return (poly16x4_t)__a;
}

static __inline__ poly16x4_t __attribute__((__always_inline__))
vreinterpret_p16_u16(uint16x4_t __a)
{
    return (poly16x4_t)__a;
}

static __inline__ poly16x4_t __attribute__((__always_inline__))
vreinterpret_p16_s16(int16x4_t __a)
{
    return (poly16x4_t)__a;
}

static __inline__ poly16x4_t __attribute__((__always_inline__))
vreinterpret_p16_u32(uint32x2_t __a)
{
    return (poly16x4_t)__a;
}

static __inline__ poly16x4_t __attribute__((__always_inline__))
vreinterpret_p16_s32(int32x2_t __a)
{
    return (poly16x4_t)__a;
}

static __inline__ poly16x4_t __attribute__((__always_inline__))
vreinterpret_p16_u64(uint64x1_t __a)
{
    return (poly16x4_t)__a;
}

static __inline__ poly16x4_t __attribute__((__always_inline__))
vreinterpret_p16_s64(int64x1_t __a)
{
    return (poly16x4_t)__a;
}

static __inline__ poly16x4_t __attribute__((__always_inline__))
vreinterpret_p16_f32(float32x2_t __a)
{
    return (poly16x4_t)__a;
}

static __inline__ poly16x4_t __attribute__((__always_inline__))
vreinterpret_p16_f64(float64x1_t __a)
{
    return (poly16x4_t)__a;
}

static __inline__ poly16x4_t __attribute__((__always_inline__))
vreinterpret_p16_p8(poly8x8_t __a)
{
    return (poly16x4_t)__a;
}

static __inline__ poly16x4_t __attribute__((__always_inline__))
vreinterpret_p16_p64(poly64x1_t __a)
{
    return (poly16x4_t)__a;
}

static __inline__ poly64x1_t __attribute__((__always_inline__))
vreinterpret_p64_u8(uint8x8_t __a)
{
    return (poly64x1_t)__a;
}

static __inline__ poly64x1_t __attribute__((__always_inline__))
vreinterpret_p64_s8(int8x8_t __a)
{
    return (poly64x1_t)__a;
}

static __inline__ poly64x1_t __attribute__((__always_inline__))
vreinterpret_p64_u16(uint16x4_t __a)
{
    return (poly64x1_t)__a;
}

static __inline__ poly64x1_t __attribute__((__always_inline__))
vreinterpret_p64_s16(int16x4_t __a)
{
    return (poly64x1_t)__a;
}

static __inline__ poly64x1_t __attribute__((__always_inline__))
vreinterpret_p64_u32(uint32x2_t __a)
{
    return (poly64x1_t)__a;
}

static __inline__ poly64x1_t __attribute__((__always_inline__))
vreinterpret_p64_s32(int32x2_t __a)
{
    return (poly64x1_t)__a;
}

static __inline__ poly64x1_t __attribute__((__always_inline__))
vreinterpret_p64_u64(uint64x1_t __a)
{
    return (poly64x1_t)__a;
}

static __inline__ poly64x1_t __attribute__((__always_inline__))
vreinterpret_p64_s64(int64x1_t __a)
{
    return (poly64x1_t)__a;
}

static __inline__ poly64x1_t __attribute__((__always_inline__))
vreinterpret_p64_f32(float32x2_t __a)
{
    return (poly64x1_t)__a;
}

static __inline__ poly64x1_t __attribute__((__always_inline__))
vreinterpret_p64_f64(float64x1_t __a)
{
    return (poly64x1_t)__a;
}

static __inline__ poly64x1_t __attribute__((__always_inline__))
vreinterpret_p64_p8(poly8x8_t __a)
{
    return (poly64x1_t)__a;
}

static __inline__ poly64x1_t __attribute__((__always_inline__))
vreinterpret_p64_p16(poly16x4_t __a)
{
    return (poly64x1_t)__a;
}

static __inline__ uint8x16_t __attribute__((__always_inline__))
vdupq_n_u8(unsigned char __a)
{
    return (uint8x16_t){ __a, __a, __a, __a, __a, __a, __a, __a, __a, __a, __a, __a, __a, __a, __a, __a };
}

static __inline__ uint8x16_t __attribute__((__always_inline__))
vmovq_n_u8(unsigned char __a)
{
    return (uint8x16_t){ __a, __a, __a, __a, __a, __a, __a, __a, __a, __a, __a, __a, __a, __a, __a, __a };
}

static __inline__ uint8x16_t __attribute__((__always_inline__))
vld1q_u8(const unsigned char *__p)
{
    uint8x16_t __ret;
    __builtin_memcpy(&__ret, __p, sizeof __ret);
    return __ret;
}

static __inline__ void __attribute__((__always_inline__))
vst1q_u8(unsigned char *__p, uint8x16_t __a)
{
    __builtin_memcpy(__p, &__a, sizeof __a);
}

static __inline__ unsigned char __attribute__((__always_inline__))
vgetq_lane_u8(uint8x16_t __a, int __lane)
{
    return __a[__lane];
}

static __inline__ uint8x16_t __attribute__((__always_inline__))
vsetq_lane_u8(unsigned char __v, uint8x16_t __a, int __lane)
{
    __a[__lane] = __v;
    return __a;
}

static __inline__ int8x16_t __attribute__((__always_inline__))
vdupq_n_s8(signed char __a)
{
    return (int8x16_t){ __a, __a, __a, __a, __a, __a, __a, __a, __a, __a, __a, __a, __a, __a, __a, __a };
}

static __inline__ int8x16_t __attribute__((__always_inline__))
vmovq_n_s8(signed char __a)
{
    return (int8x16_t){ __a, __a, __a, __a, __a, __a, __a, __a, __a, __a, __a, __a, __a, __a, __a, __a };
}

static __inline__ int8x16_t __attribute__((__always_inline__))
vld1q_s8(const signed char *__p)
{
    int8x16_t __ret;
    __builtin_memcpy(&__ret, __p, sizeof __ret);
    return __ret;
}

static __inline__ void __attribute__((__always_inline__))
vst1q_s8(signed char *__p, int8x16_t __a)
{
    __builtin_memcpy(__p, &__a, sizeof __a);
}

static __inline__ signed char __attribute__((__always_inline__))
vgetq_lane_s8(int8x16_t __a, int __lane)
{
    return __a[__lane];
}

static __inline__ int8x16_t __attribute__((__always_inline__))
vsetq_lane_s8(signed char __v, int8x16_t __a, int __lane)
{
    __a[__lane] = __v;
    return __a;
}

static __inline__ uint16x8_t __attribute__((__always_inline__))
vdupq_n_u16(unsigned short __a)
{
    return (uint16x8_t){ __a, __a, __a, __a, __a, __a, __a, __a };
}

static __inline__ uint16x8_t __attribute__((__always_inline__))
vmovq_n_u16(unsigned short __a)
{
    return (uint16x8_t){ __a, __a, __a, __a, __a, __a, __a, __a };
}

static __inline__ uint16x8_t __attribute__((__always_inline__))
vld1q_u16(const unsigned short *__p)
{
    uint16x8_t __ret;
    __builtin_memcpy(&__ret, __p, sizeof __ret);
    return __ret;
}

static __inline__ void __attribute__((__always_inline__))
vst1q_u16(unsigned short *__p, uint16x8_t __a)
{
    __builtin_memcpy(__p, &__a, sizeof __a);
}

static __inline__ unsigned short __attribute__((__always_inline__))
vgetq_lane_u16(uint16x8_t __a, int __lane)
{
    return __a[__lane];
}

static __inline__ uint16x8_t __attribute__((__always_inline__))
vsetq_lane_u16(unsigned short __v, uint16x8_t __a, int __lane)
{
    __a[__lane] = __v;
    return __a;
}

static __inline__ int16x8_t __attribute__((__always_inline__))
vdupq_n_s16(short __a)
{
    return (int16x8_t){ __a, __a, __a, __a, __a, __a, __a, __a };
}

static __inline__ int16x8_t __attribute__((__always_inline__))
vmovq_n_s16(short __a)
{
    return (int16x8_t){ __a, __a, __a, __a, __a, __a, __a, __a };
}

static __inline__ int16x8_t __attribute__((__always_inline__))
vld1q_s16(const short *__p)
{
    int16x8_t __ret;
    __builtin_memcpy(&__ret, __p, sizeof __ret);
    return __ret;
}

static __inline__ void __attribute__((__always_inline__))
vst1q_s16(short *__p, int16x8_t __a)
{
    __builtin_memcpy(__p, &__a, sizeof __a);
}

static __inline__ short __attribute__((__always_inline__))
vgetq_lane_s16(int16x8_t __a, int __lane)
{
    return __a[__lane];
}

static __inline__ int16x8_t __attribute__((__always_inline__))
vsetq_lane_s16(short __v, int16x8_t __a, int __lane)
{
    __a[__lane] = __v;
    return __a;
}

static __inline__ uint32x4_t __attribute__((__always_inline__))
vdupq_n_u32(unsigned int __a)
{
    return (uint32x4_t){ __a, __a, __a, __a };
}

static __inline__ uint32x4_t __attribute__((__always_inline__))
vmovq_n_u32(unsigned int __a)
{
    return (uint32x4_t){ __a, __a, __a, __a };
}

static __inline__ uint32x4_t __attribute__((__always_inline__))
vld1q_u32(const unsigned int *__p)
{
    uint32x4_t __ret;
    __builtin_memcpy(&__ret, __p, sizeof __ret);
    return __ret;
}

static __inline__ void __attribute__((__always_inline__))
vst1q_u32(unsigned int *__p, uint32x4_t __a)
{
    __builtin_memcpy(__p, &__a, sizeof __a);
}

static __inline__ unsigned int __attribute__((__always_inline__))
vgetq_lane_u32(uint32x4_t __a, int __lane)
{
    return __a[__lane];
}

static __inline__ uint32x4_t __attribute__((__always_inline__))
vsetq_lane_u32(unsigned int __v, uint32x4_t __a, int __lane)
{
    __a[__lane] = __v;
    return __a;
}

static __inline__ int32x4_t __attribute__((__always_inline__))
vdupq_n_s32(int __a)
{
    return (int32x4_t){ __a, __a, __a, __a };
}

static __inline__ int32x4_t __attribute__((__always_inline__))
vmovq_n_s32(int __a)
{
    return (int32x4_t){ __a, __a, __a, __a };
}

static __inline__ int32x4_t __attribute__((__always_inline__))
vld1q_s32(const int *__p)
{
    int32x4_t __ret;
    __builtin_memcpy(&__ret, __p, sizeof __ret);
    return __ret;
}

static __inline__ void __attribute__((__always_inline__))
vst1q_s32(int *__p, int32x4_t __a)
{
    __builtin_memcpy(__p, &__a, sizeof __a);
}

static __inline__ int __attribute__((__always_inline__))
vgetq_lane_s32(int32x4_t __a, int __lane)
{
    return __a[__lane];
}

static __inline__ int32x4_t __attribute__((__always_inline__))
vsetq_lane_s32(int __v, int32x4_t __a, int __lane)
{
    __a[__lane] = __v;
    return __a;
}

static __inline__ uint64x2_t __attribute__((__always_inline__))
vdupq_n_u64(unsigned long long __a)
{
    return (uint64x2_t){ __a, __a };
}

static __inline__ uint64x2_t __attribute__((__always_inline__))
vmovq_n_u64(unsigned long long __a)
{
    return (uint64x2_t){ __a, __a };
}

static __inline__ uint64x2_t __attribute__((__always_inline__))
vld1q_u64(const unsigned long long *__p)
{
    uint64x2_t __ret;
    __builtin_memcpy(&__ret, __p, sizeof __ret);
    return __ret;
}

static __inline__ void __attribute__((__always_inline__))
vst1q_u64(unsigned long long *__p, uint64x2_t __a)
{
    __builtin_memcpy(__p, &__a, sizeof __a);
}

static __inline__ unsigned long long __attribute__((__always_inline__))
vgetq_lane_u64(uint64x2_t __a, int __lane)
{
    return __a[__lane];
}

static __inline__ uint64x2_t __attribute__((__always_inline__))
vsetq_lane_u64(unsigned long long __v, uint64x2_t __a, int __lane)
{
    __a[__lane] = __v;
    return __a;
}

static __inline__ int64x2_t __attribute__((__always_inline__))
vdupq_n_s64(long long __a)
{
    return (int64x2_t){ __a, __a };
}

static __inline__ int64x2_t __attribute__((__always_inline__))
vmovq_n_s64(long long __a)
{
    return (int64x2_t){ __a, __a };
}

static __inline__ int64x2_t __attribute__((__always_inline__))
vld1q_s64(const long long *__p)
{
    int64x2_t __ret;
    __builtin_memcpy(&__ret, __p, sizeof __ret);
    return __ret;
}

static __inline__ void __attribute__((__always_inline__))
vst1q_s64(long long *__p, int64x2_t __a)
{
    __builtin_memcpy(__p, &__a, sizeof __a);
}

static __inline__ long long __attribute__((__always_inline__))
vgetq_lane_s64(int64x2_t __a, int __lane)
{
    return __a[__lane];
}

static __inline__ int64x2_t __attribute__((__always_inline__))
vsetq_lane_s64(long long __v, int64x2_t __a, int __lane)
{
    __a[__lane] = __v;
    return __a;
}

static __inline__ float32x4_t __attribute__((__always_inline__))
vdupq_n_f32(float __a)
{
    return (float32x4_t){ __a, __a, __a, __a };
}

static __inline__ float32x4_t __attribute__((__always_inline__))
vmovq_n_f32(float __a)
{
    return (float32x4_t){ __a, __a, __a, __a };
}

static __inline__ float32x4_t __attribute__((__always_inline__))
vld1q_f32(const float *__p)
{
    float32x4_t __ret;
    __builtin_memcpy(&__ret, __p, sizeof __ret);
    return __ret;
}

static __inline__ void __attribute__((__always_inline__))
vst1q_f32(float *__p, float32x4_t __a)
{
    __builtin_memcpy(__p, &__a, sizeof __a);
}

static __inline__ float __attribute__((__always_inline__))
vgetq_lane_f32(float32x4_t __a, int __lane)
{
    return __a[__lane];
}

static __inline__ float32x4_t __attribute__((__always_inline__))
vsetq_lane_f32(float __v, float32x4_t __a, int __lane)
{
    __a[__lane] = __v;
    return __a;
}

static __inline__ float64x2_t __attribute__((__always_inline__))
vdupq_n_f64(double __a)
{
    return (float64x2_t){ __a, __a };
}

static __inline__ float64x2_t __attribute__((__always_inline__))
vmovq_n_f64(double __a)
{
    return (float64x2_t){ __a, __a };
}

static __inline__ float64x2_t __attribute__((__always_inline__))
vld1q_f64(const double *__p)
{
    float64x2_t __ret;
    __builtin_memcpy(&__ret, __p, sizeof __ret);
    return __ret;
}

static __inline__ void __attribute__((__always_inline__))
vst1q_f64(double *__p, float64x2_t __a)
{
    __builtin_memcpy(__p, &__a, sizeof __a);
}

static __inline__ double __attribute__((__always_inline__))
vgetq_lane_f64(float64x2_t __a, int __lane)
{
    return __a[__lane];
}

static __inline__ float64x2_t __attribute__((__always_inline__))
vsetq_lane_f64(double __v, float64x2_t __a, int __lane)
{
    __a[__lane] = __v;
    return __a;
}

static __inline__ poly8x16_t __attribute__((__always_inline__))
vld1q_p8(const unsigned char *__p)
{
    poly8x16_t __ret;
    __builtin_memcpy(&__ret, __p, sizeof __ret);
    return __ret;
}

static __inline__ void __attribute__((__always_inline__))
vst1q_p8(unsigned char *__p, poly8x16_t __a)
{
    __builtin_memcpy(__p, &__a, sizeof __a);
}

static __inline__ unsigned char __attribute__((__always_inline__))
vgetq_lane_p8(poly8x16_t __a, int __lane)
{
    return __a[__lane];
}

static __inline__ poly8x16_t __attribute__((__always_inline__))
vsetq_lane_p8(unsigned char __v, poly8x16_t __a, int __lane)
{
    __a[__lane] = __v;
    return __a;
}

static __inline__ poly16x8_t __attribute__((__always_inline__))
vld1q_p16(const unsigned short *__p)
{
    poly16x8_t __ret;
    __builtin_memcpy(&__ret, __p, sizeof __ret);
    return __ret;
}

static __inline__ void __attribute__((__always_inline__))
vst1q_p16(unsigned short *__p, poly16x8_t __a)
{
    __builtin_memcpy(__p, &__a, sizeof __a);
}

static __inline__ unsigned short __attribute__((__always_inline__))
vgetq_lane_p16(poly16x8_t __a, int __lane)
{
    return __a[__lane];
}

static __inline__ poly16x8_t __attribute__((__always_inline__))
vsetq_lane_p16(unsigned short __v, poly16x8_t __a, int __lane)
{
    __a[__lane] = __v;
    return __a;
}

static __inline__ poly64x2_t __attribute__((__always_inline__))
vld1q_p64(const unsigned long long *__p)
{
    poly64x2_t __ret;
    __builtin_memcpy(&__ret, __p, sizeof __ret);
    return __ret;
}

static __inline__ void __attribute__((__always_inline__))
vst1q_p64(unsigned long long *__p, poly64x2_t __a)
{
    __builtin_memcpy(__p, &__a, sizeof __a);
}

static __inline__ unsigned long long __attribute__((__always_inline__))
vgetq_lane_p64(poly64x2_t __a, int __lane)
{
    return __a[__lane];
}

static __inline__ poly64x2_t __attribute__((__always_inline__))
vsetq_lane_p64(unsigned long long __v, poly64x2_t __a, int __lane)
{
    __a[__lane] = __v;
    return __a;
}

static __inline__ uint8x16_t __attribute__((__always_inline__))
vaddq_u8(uint8x16_t __a, uint8x16_t __b)
{
    return __a + __b;
}

static __inline__ uint8x16_t __attribute__((__always_inline__))
vsubq_u8(uint8x16_t __a, uint8x16_t __b)
{
    return __a - __b;
}

static __inline__ uint8x16_t __attribute__((__always_inline__))
vmulq_u8(uint8x16_t __a, uint8x16_t __b)
{
    return __a * __b;
}

static __inline__ uint8x16_t __attribute__((__always_inline__))
vceqq_u8(uint8x16_t __a, uint8x16_t __b)
{
    return (uint8x16_t)(__a == __b);
}

static __inline__ uint8x16_t __attribute__((__always_inline__))
vcgeq_u8(uint8x16_t __a, uint8x16_t __b)
{
    return (uint8x16_t)(__a >= __b);
}

static __inline__ uint8x16_t __attribute__((__always_inline__))
vcgtq_u8(uint8x16_t __a, uint8x16_t __b)
{
    return (uint8x16_t)(__a > __b);
}

static __inline__ uint8x16_t __attribute__((__always_inline__))
vcleq_u8(uint8x16_t __a, uint8x16_t __b)
{
    return (uint8x16_t)(__a <= __b);
}

static __inline__ uint8x16_t __attribute__((__always_inline__))
vcltq_u8(uint8x16_t __a, uint8x16_t __b)
{
    return (uint8x16_t)(__a < __b);
}

static __inline__ uint8x16_t __attribute__((__always_inline__))
vbslq_u8(uint8x16_t __s, uint8x16_t __a, uint8x16_t __b)
{
    return (uint8x16_t)((__s & (uint8x16_t)__a) | (~__s & (uint8x16_t)__b));
}

static __inline__ int8x16_t __attribute__((__always_inline__))
vaddq_s8(int8x16_t __a, int8x16_t __b)
{
    return __a + __b;
}

static __inline__ int8x16_t __attribute__((__always_inline__))
vsubq_s8(int8x16_t __a, int8x16_t __b)
{
    return __a - __b;
}

static __inline__ int8x16_t __attribute__((__always_inline__))
vmulq_s8(int8x16_t __a, int8x16_t __b)
{
    return __a * __b;
}

static __inline__ uint8x16_t __attribute__((__always_inline__))
vceqq_s8(int8x16_t __a, int8x16_t __b)
{
    return (uint8x16_t)(__a == __b);
}

static __inline__ uint8x16_t __attribute__((__always_inline__))
vcgeq_s8(int8x16_t __a, int8x16_t __b)
{
    return (uint8x16_t)(__a >= __b);
}

static __inline__ uint8x16_t __attribute__((__always_inline__))
vcgtq_s8(int8x16_t __a, int8x16_t __b)
{
    return (uint8x16_t)(__a > __b);
}

static __inline__ uint8x16_t __attribute__((__always_inline__))
vcleq_s8(int8x16_t __a, int8x16_t __b)
{
    return (uint8x16_t)(__a <= __b);
}

static __inline__ uint8x16_t __attribute__((__always_inline__))
vcltq_s8(int8x16_t __a, int8x16_t __b)
{
    return (uint8x16_t)(__a < __b);
}

static __inline__ int8x16_t __attribute__((__always_inline__))
vnegq_s8(int8x16_t __a)
{
    return -__a;
}

static __inline__ int8x16_t __attribute__((__always_inline__))
vbslq_s8(uint8x16_t __s, int8x16_t __a, int8x16_t __b)
{
    return (int8x16_t)((__s & (uint8x16_t)__a) | (~__s & (uint8x16_t)__b));
}

static __inline__ uint16x8_t __attribute__((__always_inline__))
vaddq_u16(uint16x8_t __a, uint16x8_t __b)
{
    return __a + __b;
}

static __inline__ uint16x8_t __attribute__((__always_inline__))
vsubq_u16(uint16x8_t __a, uint16x8_t __b)
{
    return __a - __b;
}

static __inline__ uint16x8_t __attribute__((__always_inline__))
vmulq_u16(uint16x8_t __a, uint16x8_t __b)
{
    return __a * __b;
}

static __inline__ uint16x8_t __attribute__((__always_inline__))
vceqq_u16(uint16x8_t __a, uint16x8_t __b)
{
    return (uint16x8_t)(__a == __b);
}

static __inline__ uint16x8_t __attribute__((__always_inline__))
vcgeq_u16(uint16x8_t __a, uint16x8_t __b)
{
    return (uint16x8_t)(__a >= __b);
}

static __inline__ uint16x8_t __attribute__((__always_inline__))
vcgtq_u16(uint16x8_t __a, uint16x8_t __b)
{
    return (uint16x8_t)(__a > __b);
}

static __inline__ uint16x8_t __attribute__((__always_inline__))
vcleq_u16(uint16x8_t __a, uint16x8_t __b)
{
    return (uint16x8_t)(__a <= __b);
}

static __inline__ uint16x8_t __attribute__((__always_inline__))
vcltq_u16(uint16x8_t __a, uint16x8_t __b)
{
    return (uint16x8_t)(__a < __b);
}

static __inline__ uint16x8_t __attribute__((__always_inline__))
vbslq_u16(uint16x8_t __s, uint16x8_t __a, uint16x8_t __b)
{
    return (uint16x8_t)((__s & (uint16x8_t)__a) | (~__s & (uint16x8_t)__b));
}

static __inline__ int16x8_t __attribute__((__always_inline__))
vaddq_s16(int16x8_t __a, int16x8_t __b)
{
    return __a + __b;
}

static __inline__ int16x8_t __attribute__((__always_inline__))
vsubq_s16(int16x8_t __a, int16x8_t __b)
{
    return __a - __b;
}

static __inline__ int16x8_t __attribute__((__always_inline__))
vmulq_s16(int16x8_t __a, int16x8_t __b)
{
    return __a * __b;
}

static __inline__ uint16x8_t __attribute__((__always_inline__))
vceqq_s16(int16x8_t __a, int16x8_t __b)
{
    return (uint16x8_t)(__a == __b);
}

static __inline__ uint16x8_t __attribute__((__always_inline__))
vcgeq_s16(int16x8_t __a, int16x8_t __b)
{
    return (uint16x8_t)(__a >= __b);
}

static __inline__ uint16x8_t __attribute__((__always_inline__))
vcgtq_s16(int16x8_t __a, int16x8_t __b)
{
    return (uint16x8_t)(__a > __b);
}

static __inline__ uint16x8_t __attribute__((__always_inline__))
vcleq_s16(int16x8_t __a, int16x8_t __b)
{
    return (uint16x8_t)(__a <= __b);
}

static __inline__ uint16x8_t __attribute__((__always_inline__))
vcltq_s16(int16x8_t __a, int16x8_t __b)
{
    return (uint16x8_t)(__a < __b);
}

static __inline__ int16x8_t __attribute__((__always_inline__))
vnegq_s16(int16x8_t __a)
{
    return -__a;
}

static __inline__ int16x8_t __attribute__((__always_inline__))
vbslq_s16(uint16x8_t __s, int16x8_t __a, int16x8_t __b)
{
    return (int16x8_t)((__s & (uint16x8_t)__a) | (~__s & (uint16x8_t)__b));
}

static __inline__ uint32x4_t __attribute__((__always_inline__))
vaddq_u32(uint32x4_t __a, uint32x4_t __b)
{
    return __a + __b;
}

static __inline__ uint32x4_t __attribute__((__always_inline__))
vsubq_u32(uint32x4_t __a, uint32x4_t __b)
{
    return __a - __b;
}

static __inline__ uint32x4_t __attribute__((__always_inline__))
vmulq_u32(uint32x4_t __a, uint32x4_t __b)
{
    return __a * __b;
}

static __inline__ uint32x4_t __attribute__((__always_inline__))
vceqq_u32(uint32x4_t __a, uint32x4_t __b)
{
    return (uint32x4_t)(__a == __b);
}

static __inline__ uint32x4_t __attribute__((__always_inline__))
vcgeq_u32(uint32x4_t __a, uint32x4_t __b)
{
    return (uint32x4_t)(__a >= __b);
}

static __inline__ uint32x4_t __attribute__((__always_inline__))
vcgtq_u32(uint32x4_t __a, uint32x4_t __b)
{
    return (uint32x4_t)(__a > __b);
}

static __inline__ uint32x4_t __attribute__((__always_inline__))
vcleq_u32(uint32x4_t __a, uint32x4_t __b)
{
    return (uint32x4_t)(__a <= __b);
}

static __inline__ uint32x4_t __attribute__((__always_inline__))
vcltq_u32(uint32x4_t __a, uint32x4_t __b)
{
    return (uint32x4_t)(__a < __b);
}

static __inline__ uint32x4_t __attribute__((__always_inline__))
vbslq_u32(uint32x4_t __s, uint32x4_t __a, uint32x4_t __b)
{
    return (uint32x4_t)((__s & (uint32x4_t)__a) | (~__s & (uint32x4_t)__b));
}

static __inline__ int32x4_t __attribute__((__always_inline__))
vaddq_s32(int32x4_t __a, int32x4_t __b)
{
    return __a + __b;
}

static __inline__ int32x4_t __attribute__((__always_inline__))
vsubq_s32(int32x4_t __a, int32x4_t __b)
{
    return __a - __b;
}

static __inline__ int32x4_t __attribute__((__always_inline__))
vmulq_s32(int32x4_t __a, int32x4_t __b)
{
    return __a * __b;
}

static __inline__ uint32x4_t __attribute__((__always_inline__))
vceqq_s32(int32x4_t __a, int32x4_t __b)
{
    return (uint32x4_t)(__a == __b);
}

static __inline__ uint32x4_t __attribute__((__always_inline__))
vcgeq_s32(int32x4_t __a, int32x4_t __b)
{
    return (uint32x4_t)(__a >= __b);
}

static __inline__ uint32x4_t __attribute__((__always_inline__))
vcgtq_s32(int32x4_t __a, int32x4_t __b)
{
    return (uint32x4_t)(__a > __b);
}

static __inline__ uint32x4_t __attribute__((__always_inline__))
vcleq_s32(int32x4_t __a, int32x4_t __b)
{
    return (uint32x4_t)(__a <= __b);
}

static __inline__ uint32x4_t __attribute__((__always_inline__))
vcltq_s32(int32x4_t __a, int32x4_t __b)
{
    return (uint32x4_t)(__a < __b);
}

static __inline__ int32x4_t __attribute__((__always_inline__))
vnegq_s32(int32x4_t __a)
{
    return -__a;
}

static __inline__ int32x4_t __attribute__((__always_inline__))
vbslq_s32(uint32x4_t __s, int32x4_t __a, int32x4_t __b)
{
    return (int32x4_t)((__s & (uint32x4_t)__a) | (~__s & (uint32x4_t)__b));
}

static __inline__ uint64x2_t __attribute__((__always_inline__))
vaddq_u64(uint64x2_t __a, uint64x2_t __b)
{
    return __a + __b;
}

static __inline__ uint64x2_t __attribute__((__always_inline__))
vsubq_u64(uint64x2_t __a, uint64x2_t __b)
{
    return __a - __b;
}

static __inline__ uint64x2_t __attribute__((__always_inline__))
vceqq_u64(uint64x2_t __a, uint64x2_t __b)
{
    return (uint64x2_t)(__a == __b);
}

static __inline__ uint64x2_t __attribute__((__always_inline__))
vcgeq_u64(uint64x2_t __a, uint64x2_t __b)
{
    return (uint64x2_t)(__a >= __b);
}

static __inline__ uint64x2_t __attribute__((__always_inline__))
vcgtq_u64(uint64x2_t __a, uint64x2_t __b)
{
    return (uint64x2_t)(__a > __b);
}

static __inline__ uint64x2_t __attribute__((__always_inline__))
vcleq_u64(uint64x2_t __a, uint64x2_t __b)
{
    return (uint64x2_t)(__a <= __b);
}

static __inline__ uint64x2_t __attribute__((__always_inline__))
vcltq_u64(uint64x2_t __a, uint64x2_t __b)
{
    return (uint64x2_t)(__a < __b);
}

static __inline__ uint64x2_t __attribute__((__always_inline__))
vbslq_u64(uint64x2_t __s, uint64x2_t __a, uint64x2_t __b)
{
    return (uint64x2_t)((__s & (uint64x2_t)__a) | (~__s & (uint64x2_t)__b));
}

static __inline__ int64x2_t __attribute__((__always_inline__))
vaddq_s64(int64x2_t __a, int64x2_t __b)
{
    return __a + __b;
}

static __inline__ int64x2_t __attribute__((__always_inline__))
vsubq_s64(int64x2_t __a, int64x2_t __b)
{
    return __a - __b;
}

static __inline__ uint64x2_t __attribute__((__always_inline__))
vceqq_s64(int64x2_t __a, int64x2_t __b)
{
    return (uint64x2_t)(__a == __b);
}

static __inline__ uint64x2_t __attribute__((__always_inline__))
vcgeq_s64(int64x2_t __a, int64x2_t __b)
{
    return (uint64x2_t)(__a >= __b);
}

static __inline__ uint64x2_t __attribute__((__always_inline__))
vcgtq_s64(int64x2_t __a, int64x2_t __b)
{
    return (uint64x2_t)(__a > __b);
}

static __inline__ uint64x2_t __attribute__((__always_inline__))
vcleq_s64(int64x2_t __a, int64x2_t __b)
{
    return (uint64x2_t)(__a <= __b);
}

static __inline__ uint64x2_t __attribute__((__always_inline__))
vcltq_s64(int64x2_t __a, int64x2_t __b)
{
    return (uint64x2_t)(__a < __b);
}

static __inline__ int64x2_t __attribute__((__always_inline__))
vnegq_s64(int64x2_t __a)
{
    return -__a;
}

static __inline__ int64x2_t __attribute__((__always_inline__))
vbslq_s64(uint64x2_t __s, int64x2_t __a, int64x2_t __b)
{
    return (int64x2_t)((__s & (uint64x2_t)__a) | (~__s & (uint64x2_t)__b));
}

static __inline__ float32x4_t __attribute__((__always_inline__))
vaddq_f32(float32x4_t __a, float32x4_t __b)
{
    return __a + __b;
}

static __inline__ float32x4_t __attribute__((__always_inline__))
vsubq_f32(float32x4_t __a, float32x4_t __b)
{
    return __a - __b;
}

static __inline__ float32x4_t __attribute__((__always_inline__))
vmulq_f32(float32x4_t __a, float32x4_t __b)
{
    return __a * __b;
}

static __inline__ float32x4_t __attribute__((__always_inline__))
vdivq_f32(float32x4_t __a, float32x4_t __b)
{
    return __a / __b;
}

static __inline__ uint32x4_t __attribute__((__always_inline__))
vceqq_f32(float32x4_t __a, float32x4_t __b)
{
    return (uint32x4_t)(__a == __b);
}

static __inline__ uint32x4_t __attribute__((__always_inline__))
vcgeq_f32(float32x4_t __a, float32x4_t __b)
{
    return (uint32x4_t)(__a >= __b);
}

static __inline__ uint32x4_t __attribute__((__always_inline__))
vcgtq_f32(float32x4_t __a, float32x4_t __b)
{
    return (uint32x4_t)(__a > __b);
}

static __inline__ uint32x4_t __attribute__((__always_inline__))
vcleq_f32(float32x4_t __a, float32x4_t __b)
{
    return (uint32x4_t)(__a <= __b);
}

static __inline__ uint32x4_t __attribute__((__always_inline__))
vcltq_f32(float32x4_t __a, float32x4_t __b)
{
    return (uint32x4_t)(__a < __b);
}

static __inline__ float32x4_t __attribute__((__always_inline__))
vnegq_f32(float32x4_t __a)
{
    return -__a;
}

static __inline__ float32x4_t __attribute__((__always_inline__))
vbslq_f32(uint32x4_t __s, float32x4_t __a, float32x4_t __b)
{
    return (float32x4_t)((__s & (uint32x4_t)__a) | (~__s & (uint32x4_t)__b));
}

static __inline__ float64x2_t __attribute__((__always_inline__))
vaddq_f64(float64x2_t __a, float64x2_t __b)
{
    return __a + __b;
}

static __inline__ float64x2_t __attribute__((__always_inline__))
vsubq_f64(float64x2_t __a, float64x2_t __b)
{
    return __a - __b;
}

static __inline__ float64x2_t __attribute__((__always_inline__))
vmulq_f64(float64x2_t __a, float64x2_t __b)
{
    return __a * __b;
}

static __inline__ float64x2_t __attribute__((__always_inline__))
vdivq_f64(float64x2_t __a, float64x2_t __b)
{
    return __a / __b;
}

static __inline__ uint64x2_t __attribute__((__always_inline__))
vceqq_f64(float64x2_t __a, float64x2_t __b)
{
    return (uint64x2_t)(__a == __b);
}

static __inline__ uint64x2_t __attribute__((__always_inline__))
vcgeq_f64(float64x2_t __a, float64x2_t __b)
{
    return (uint64x2_t)(__a >= __b);
}

static __inline__ uint64x2_t __attribute__((__always_inline__))
vcgtq_f64(float64x2_t __a, float64x2_t __b)
{
    return (uint64x2_t)(__a > __b);
}

static __inline__ uint64x2_t __attribute__((__always_inline__))
vcleq_f64(float64x2_t __a, float64x2_t __b)
{
    return (uint64x2_t)(__a <= __b);
}

static __inline__ uint64x2_t __attribute__((__always_inline__))
vcltq_f64(float64x2_t __a, float64x2_t __b)
{
    return (uint64x2_t)(__a < __b);
}

static __inline__ float64x2_t __attribute__((__always_inline__))
vnegq_f64(float64x2_t __a)
{
    return -__a;
}

static __inline__ float64x2_t __attribute__((__always_inline__))
vbslq_f64(uint64x2_t __s, float64x2_t __a, float64x2_t __b)
{
    return (float64x2_t)((__s & (uint64x2_t)__a) | (~__s & (uint64x2_t)__b));
}

static __inline__ uint8x16_t __attribute__((__always_inline__))
vandq_u8(uint8x16_t __a, uint8x16_t __b)
{
    return __a & __b;
}

static __inline__ uint8x16_t __attribute__((__always_inline__))
vorrq_u8(uint8x16_t __a, uint8x16_t __b)
{
    return __a | __b;
}

static __inline__ uint8x16_t __attribute__((__always_inline__))
veorq_u8(uint8x16_t __a, uint8x16_t __b)
{
    return __a ^ __b;
}

static __inline__ uint8x16_t __attribute__((__always_inline__))
vbicq_u8(uint8x16_t __a, uint8x16_t __b)
{
    return __a & ~__b;
}

static __inline__ uint8x16_t __attribute__((__always_inline__))
vornq_u8(uint8x16_t __a, uint8x16_t __b)
{
    return __a | ~__b;
}

static __inline__ uint8x16_t __attribute__((__always_inline__))
vtstq_u8(uint8x16_t __a, uint8x16_t __b)
{
    return (uint8x16_t)((__a & __b) != 0);
}

static __inline__ uint8x16_t __attribute__((__always_inline__))
vmvnq_u8(uint8x16_t __a)
{
    return ~__a;
}

static __inline__ uint8x16_t __attribute__((__always_inline__))
vmlaq_u8(uint8x16_t __a, uint8x16_t __b, uint8x16_t __c)
{
    return __a + __b * __c;
}

static __inline__ uint8x16_t __attribute__((__always_inline__))
vmlsq_u8(uint8x16_t __a, uint8x16_t __b, uint8x16_t __c)
{
    return __a - __b * __c;
}

static __inline__ uint8x16_t __attribute__((__always_inline__))
vshlq_n_u8(uint8x16_t __a, int __n)
{
    return __a << __n;
}

static __inline__ uint8x16_t __attribute__((__always_inline__))
vshrq_n_u8(uint8x16_t __a, int __n)
{
    if (__n == 8)
        return __a ^ __a;
    return __a >> __n;
}

static __inline__ int8x16_t __attribute__((__always_inline__))
vandq_s8(int8x16_t __a, int8x16_t __b)
{
    return __a & __b;
}

static __inline__ int8x16_t __attribute__((__always_inline__))
vorrq_s8(int8x16_t __a, int8x16_t __b)
{
    return __a | __b;
}

static __inline__ int8x16_t __attribute__((__always_inline__))
veorq_s8(int8x16_t __a, int8x16_t __b)
{
    return __a ^ __b;
}

static __inline__ int8x16_t __attribute__((__always_inline__))
vbicq_s8(int8x16_t __a, int8x16_t __b)
{
    return __a & ~__b;
}

static __inline__ int8x16_t __attribute__((__always_inline__))
vornq_s8(int8x16_t __a, int8x16_t __b)
{
    return __a | ~__b;
}

static __inline__ uint8x16_t __attribute__((__always_inline__))
vtstq_s8(int8x16_t __a, int8x16_t __b)
{
    return (uint8x16_t)((__a & __b) != 0);
}

static __inline__ int8x16_t __attribute__((__always_inline__))
vmvnq_s8(int8x16_t __a)
{
    return ~__a;
}

static __inline__ int8x16_t __attribute__((__always_inline__))
vmlaq_s8(int8x16_t __a, int8x16_t __b, int8x16_t __c)
{
    return __a + __b * __c;
}

static __inline__ int8x16_t __attribute__((__always_inline__))
vmlsq_s8(int8x16_t __a, int8x16_t __b, int8x16_t __c)
{
    return __a - __b * __c;
}

static __inline__ int8x16_t __attribute__((__always_inline__))
vshlq_n_s8(int8x16_t __a, int __n)
{
    return __a << __n;
}

static __inline__ int8x16_t __attribute__((__always_inline__))
vshrq_n_s8(int8x16_t __a, int __n)
{
    return __a >> (__n == 8 ? 7 : __n);
}

static __inline__ uint16x8_t __attribute__((__always_inline__))
vandq_u16(uint16x8_t __a, uint16x8_t __b)
{
    return __a & __b;
}

static __inline__ uint16x8_t __attribute__((__always_inline__))
vorrq_u16(uint16x8_t __a, uint16x8_t __b)
{
    return __a | __b;
}

static __inline__ uint16x8_t __attribute__((__always_inline__))
veorq_u16(uint16x8_t __a, uint16x8_t __b)
{
    return __a ^ __b;
}

static __inline__ uint16x8_t __attribute__((__always_inline__))
vbicq_u16(uint16x8_t __a, uint16x8_t __b)
{
    return __a & ~__b;
}

static __inline__ uint16x8_t __attribute__((__always_inline__))
vornq_u16(uint16x8_t __a, uint16x8_t __b)
{
    return __a | ~__b;
}

static __inline__ uint16x8_t __attribute__((__always_inline__))
vtstq_u16(uint16x8_t __a, uint16x8_t __b)
{
    return (uint16x8_t)((__a & __b) != 0);
}

static __inline__ uint16x8_t __attribute__((__always_inline__))
vmvnq_u16(uint16x8_t __a)
{
    return ~__a;
}

static __inline__ uint16x8_t __attribute__((__always_inline__))
vmlaq_u16(uint16x8_t __a, uint16x8_t __b, uint16x8_t __c)
{
    return __a + __b * __c;
}

static __inline__ uint16x8_t __attribute__((__always_inline__))
vmlsq_u16(uint16x8_t __a, uint16x8_t __b, uint16x8_t __c)
{
    return __a - __b * __c;
}

static __inline__ uint16x8_t __attribute__((__always_inline__))
vshlq_n_u16(uint16x8_t __a, int __n)
{
    return __a << __n;
}

static __inline__ uint16x8_t __attribute__((__always_inline__))
vshrq_n_u16(uint16x8_t __a, int __n)
{
    if (__n == 16)
        return __a ^ __a;
    return __a >> __n;
}

static __inline__ int16x8_t __attribute__((__always_inline__))
vandq_s16(int16x8_t __a, int16x8_t __b)
{
    return __a & __b;
}

static __inline__ int16x8_t __attribute__((__always_inline__))
vorrq_s16(int16x8_t __a, int16x8_t __b)
{
    return __a | __b;
}

static __inline__ int16x8_t __attribute__((__always_inline__))
veorq_s16(int16x8_t __a, int16x8_t __b)
{
    return __a ^ __b;
}

static __inline__ int16x8_t __attribute__((__always_inline__))
vbicq_s16(int16x8_t __a, int16x8_t __b)
{
    return __a & ~__b;
}

static __inline__ int16x8_t __attribute__((__always_inline__))
vornq_s16(int16x8_t __a, int16x8_t __b)
{
    return __a | ~__b;
}

static __inline__ uint16x8_t __attribute__((__always_inline__))
vtstq_s16(int16x8_t __a, int16x8_t __b)
{
    return (uint16x8_t)((__a & __b) != 0);
}

static __inline__ int16x8_t __attribute__((__always_inline__))
vmvnq_s16(int16x8_t __a)
{
    return ~__a;
}

static __inline__ int16x8_t __attribute__((__always_inline__))
vmlaq_s16(int16x8_t __a, int16x8_t __b, int16x8_t __c)
{
    return __a + __b * __c;
}

static __inline__ int16x8_t __attribute__((__always_inline__))
vmlsq_s16(int16x8_t __a, int16x8_t __b, int16x8_t __c)
{
    return __a - __b * __c;
}

static __inline__ int16x8_t __attribute__((__always_inline__))
vshlq_n_s16(int16x8_t __a, int __n)
{
    return __a << __n;
}

static __inline__ int16x8_t __attribute__((__always_inline__))
vshrq_n_s16(int16x8_t __a, int __n)
{
    return __a >> (__n == 16 ? 15 : __n);
}

static __inline__ uint32x4_t __attribute__((__always_inline__))
vandq_u32(uint32x4_t __a, uint32x4_t __b)
{
    return __a & __b;
}

static __inline__ uint32x4_t __attribute__((__always_inline__))
vorrq_u32(uint32x4_t __a, uint32x4_t __b)
{
    return __a | __b;
}

static __inline__ uint32x4_t __attribute__((__always_inline__))
veorq_u32(uint32x4_t __a, uint32x4_t __b)
{
    return __a ^ __b;
}

static __inline__ uint32x4_t __attribute__((__always_inline__))
vbicq_u32(uint32x4_t __a, uint32x4_t __b)
{
    return __a & ~__b;
}

static __inline__ uint32x4_t __attribute__((__always_inline__))
vornq_u32(uint32x4_t __a, uint32x4_t __b)
{
    return __a | ~__b;
}

static __inline__ uint32x4_t __attribute__((__always_inline__))
vtstq_u32(uint32x4_t __a, uint32x4_t __b)
{
    return (uint32x4_t)((__a & __b) != 0);
}

static __inline__ uint32x4_t __attribute__((__always_inline__))
vmvnq_u32(uint32x4_t __a)
{
    return ~__a;
}

static __inline__ uint32x4_t __attribute__((__always_inline__))
vmlaq_u32(uint32x4_t __a, uint32x4_t __b, uint32x4_t __c)
{
    return __a + __b * __c;
}

static __inline__ uint32x4_t __attribute__((__always_inline__))
vmlsq_u32(uint32x4_t __a, uint32x4_t __b, uint32x4_t __c)
{
    return __a - __b * __c;
}

static __inline__ uint32x4_t __attribute__((__always_inline__))
vshlq_n_u32(uint32x4_t __a, int __n)
{
    return __a << __n;
}

static __inline__ uint32x4_t __attribute__((__always_inline__))
vshrq_n_u32(uint32x4_t __a, int __n)
{
    if (__n == 32)
        return __a ^ __a;
    return __a >> __n;
}

static __inline__ int32x4_t __attribute__((__always_inline__))
vandq_s32(int32x4_t __a, int32x4_t __b)
{
    return __a & __b;
}

static __inline__ int32x4_t __attribute__((__always_inline__))
vorrq_s32(int32x4_t __a, int32x4_t __b)
{
    return __a | __b;
}

static __inline__ int32x4_t __attribute__((__always_inline__))
veorq_s32(int32x4_t __a, int32x4_t __b)
{
    return __a ^ __b;
}

static __inline__ int32x4_t __attribute__((__always_inline__))
vbicq_s32(int32x4_t __a, int32x4_t __b)
{
    return __a & ~__b;
}

static __inline__ int32x4_t __attribute__((__always_inline__))
vornq_s32(int32x4_t __a, int32x4_t __b)
{
    return __a | ~__b;
}

static __inline__ uint32x4_t __attribute__((__always_inline__))
vtstq_s32(int32x4_t __a, int32x4_t __b)
{
    return (uint32x4_t)((__a & __b) != 0);
}

static __inline__ int32x4_t __attribute__((__always_inline__))
vmvnq_s32(int32x4_t __a)
{
    return ~__a;
}

static __inline__ int32x4_t __attribute__((__always_inline__))
vmlaq_s32(int32x4_t __a, int32x4_t __b, int32x4_t __c)
{
    return __a + __b * __c;
}

static __inline__ int32x4_t __attribute__((__always_inline__))
vmlsq_s32(int32x4_t __a, int32x4_t __b, int32x4_t __c)
{
    return __a - __b * __c;
}

static __inline__ int32x4_t __attribute__((__always_inline__))
vshlq_n_s32(int32x4_t __a, int __n)
{
    return __a << __n;
}

static __inline__ int32x4_t __attribute__((__always_inline__))
vshrq_n_s32(int32x4_t __a, int __n)
{
    return __a >> (__n == 32 ? 31 : __n);
}

static __inline__ uint64x2_t __attribute__((__always_inline__))
vandq_u64(uint64x2_t __a, uint64x2_t __b)
{
    return __a & __b;
}

static __inline__ uint64x2_t __attribute__((__always_inline__))
vorrq_u64(uint64x2_t __a, uint64x2_t __b)
{
    return __a | __b;
}

static __inline__ uint64x2_t __attribute__((__always_inline__))
veorq_u64(uint64x2_t __a, uint64x2_t __b)
{
    return __a ^ __b;
}

static __inline__ uint64x2_t __attribute__((__always_inline__))
vbicq_u64(uint64x2_t __a, uint64x2_t __b)
{
    return __a & ~__b;
}

static __inline__ uint64x2_t __attribute__((__always_inline__))
vornq_u64(uint64x2_t __a, uint64x2_t __b)
{
    return __a | ~__b;
}

static __inline__ uint64x2_t __attribute__((__always_inline__))
vtstq_u64(uint64x2_t __a, uint64x2_t __b)
{
    return (uint64x2_t)((__a & __b) != 0);
}

static __inline__ uint64x2_t __attribute__((__always_inline__))
vshlq_n_u64(uint64x2_t __a, int __n)
{
    return __a << __n;
}

static __inline__ uint64x2_t __attribute__((__always_inline__))
vshrq_n_u64(uint64x2_t __a, int __n)
{
    if (__n == 64)
        return __a ^ __a;
    return __a >> __n;
}

static __inline__ int64x2_t __attribute__((__always_inline__))
vandq_s64(int64x2_t __a, int64x2_t __b)
{
    return __a & __b;
}

static __inline__ int64x2_t __attribute__((__always_inline__))
vorrq_s64(int64x2_t __a, int64x2_t __b)
{
    return __a | __b;
}

static __inline__ int64x2_t __attribute__((__always_inline__))
veorq_s64(int64x2_t __a, int64x2_t __b)
{
    return __a ^ __b;
}

static __inline__ int64x2_t __attribute__((__always_inline__))
vbicq_s64(int64x2_t __a, int64x2_t __b)
{
    return __a & ~__b;
}

static __inline__ int64x2_t __attribute__((__always_inline__))
vornq_s64(int64x2_t __a, int64x2_t __b)
{
    return __a | ~__b;
}

static __inline__ uint64x2_t __attribute__((__always_inline__))
vtstq_s64(int64x2_t __a, int64x2_t __b)
{
    return (uint64x2_t)((__a & __b) != 0);
}

static __inline__ int64x2_t __attribute__((__always_inline__))
vshlq_n_s64(int64x2_t __a, int __n)
{
    return __a << __n;
}

static __inline__ int64x2_t __attribute__((__always_inline__))
vshrq_n_s64(int64x2_t __a, int __n)
{
    return __a >> (__n == 64 ? 63 : __n);
}

static __inline__ uint8x16_t __attribute__((__always_inline__))
vreinterpretq_u8_s8(int8x16_t __a)
{
    return (uint8x16_t)__a;
}

static __inline__ uint8x16_t __attribute__((__always_inline__))
vreinterpretq_u8_u16(uint16x8_t __a)
{
    return (uint8x16_t)__a;
}

static __inline__ uint8x16_t __attribute__((__always_inline__))
vreinterpretq_u8_s16(int16x8_t __a)
{
    return (uint8x16_t)__a;
}

static __inline__ uint8x16_t __attribute__((__always_inline__))
vreinterpretq_u8_u32(uint32x4_t __a)
{
    return (uint8x16_t)__a;
}

static __inline__ uint8x16_t __attribute__((__always_inline__))
vreinterpretq_u8_s32(int32x4_t __a)
{
    return (uint8x16_t)__a;
}

static __inline__ uint8x16_t __attribute__((__always_inline__))
vreinterpretq_u8_u64(uint64x2_t __a)
{
    return (uint8x16_t)__a;
}

static __inline__ uint8x16_t __attribute__((__always_inline__))
vreinterpretq_u8_s64(int64x2_t __a)
{
    return (uint8x16_t)__a;
}

static __inline__ uint8x16_t __attribute__((__always_inline__))
vreinterpretq_u8_f32(float32x4_t __a)
{
    return (uint8x16_t)__a;
}

static __inline__ uint8x16_t __attribute__((__always_inline__))
vreinterpretq_u8_f64(float64x2_t __a)
{
    return (uint8x16_t)__a;
}

static __inline__ uint8x16_t __attribute__((__always_inline__))
vreinterpretq_u8_p8(poly8x16_t __a)
{
    return (uint8x16_t)__a;
}

static __inline__ uint8x16_t __attribute__((__always_inline__))
vreinterpretq_u8_p16(poly16x8_t __a)
{
    return (uint8x16_t)__a;
}

static __inline__ uint8x16_t __attribute__((__always_inline__))
vreinterpretq_u8_p64(poly64x2_t __a)
{
    return (uint8x16_t)__a;
}

static __inline__ int8x16_t __attribute__((__always_inline__))
vreinterpretq_s8_u8(uint8x16_t __a)
{
    return (int8x16_t)__a;
}

static __inline__ int8x16_t __attribute__((__always_inline__))
vreinterpretq_s8_u16(uint16x8_t __a)
{
    return (int8x16_t)__a;
}

static __inline__ int8x16_t __attribute__((__always_inline__))
vreinterpretq_s8_s16(int16x8_t __a)
{
    return (int8x16_t)__a;
}

static __inline__ int8x16_t __attribute__((__always_inline__))
vreinterpretq_s8_u32(uint32x4_t __a)
{
    return (int8x16_t)__a;
}

static __inline__ int8x16_t __attribute__((__always_inline__))
vreinterpretq_s8_s32(int32x4_t __a)
{
    return (int8x16_t)__a;
}

static __inline__ int8x16_t __attribute__((__always_inline__))
vreinterpretq_s8_u64(uint64x2_t __a)
{
    return (int8x16_t)__a;
}

static __inline__ int8x16_t __attribute__((__always_inline__))
vreinterpretq_s8_s64(int64x2_t __a)
{
    return (int8x16_t)__a;
}

static __inline__ int8x16_t __attribute__((__always_inline__))
vreinterpretq_s8_f32(float32x4_t __a)
{
    return (int8x16_t)__a;
}

static __inline__ int8x16_t __attribute__((__always_inline__))
vreinterpretq_s8_f64(float64x2_t __a)
{
    return (int8x16_t)__a;
}

static __inline__ int8x16_t __attribute__((__always_inline__))
vreinterpretq_s8_p8(poly8x16_t __a)
{
    return (int8x16_t)__a;
}

static __inline__ int8x16_t __attribute__((__always_inline__))
vreinterpretq_s8_p16(poly16x8_t __a)
{
    return (int8x16_t)__a;
}

static __inline__ int8x16_t __attribute__((__always_inline__))
vreinterpretq_s8_p64(poly64x2_t __a)
{
    return (int8x16_t)__a;
}

static __inline__ uint16x8_t __attribute__((__always_inline__))
vreinterpretq_u16_u8(uint8x16_t __a)
{
    return (uint16x8_t)__a;
}

static __inline__ uint16x8_t __attribute__((__always_inline__))
vreinterpretq_u16_s8(int8x16_t __a)
{
    return (uint16x8_t)__a;
}

static __inline__ uint16x8_t __attribute__((__always_inline__))
vreinterpretq_u16_s16(int16x8_t __a)
{
    return (uint16x8_t)__a;
}

static __inline__ uint16x8_t __attribute__((__always_inline__))
vreinterpretq_u16_u32(uint32x4_t __a)
{
    return (uint16x8_t)__a;
}

static __inline__ uint16x8_t __attribute__((__always_inline__))
vreinterpretq_u16_s32(int32x4_t __a)
{
    return (uint16x8_t)__a;
}

static __inline__ uint16x8_t __attribute__((__always_inline__))
vreinterpretq_u16_u64(uint64x2_t __a)
{
    return (uint16x8_t)__a;
}

static __inline__ uint16x8_t __attribute__((__always_inline__))
vreinterpretq_u16_s64(int64x2_t __a)
{
    return (uint16x8_t)__a;
}

static __inline__ uint16x8_t __attribute__((__always_inline__))
vreinterpretq_u16_f32(float32x4_t __a)
{
    return (uint16x8_t)__a;
}

static __inline__ uint16x8_t __attribute__((__always_inline__))
vreinterpretq_u16_f64(float64x2_t __a)
{
    return (uint16x8_t)__a;
}

static __inline__ uint16x8_t __attribute__((__always_inline__))
vreinterpretq_u16_p8(poly8x16_t __a)
{
    return (uint16x8_t)__a;
}

static __inline__ uint16x8_t __attribute__((__always_inline__))
vreinterpretq_u16_p16(poly16x8_t __a)
{
    return (uint16x8_t)__a;
}

static __inline__ uint16x8_t __attribute__((__always_inline__))
vreinterpretq_u16_p64(poly64x2_t __a)
{
    return (uint16x8_t)__a;
}

static __inline__ int16x8_t __attribute__((__always_inline__))
vreinterpretq_s16_u8(uint8x16_t __a)
{
    return (int16x8_t)__a;
}

static __inline__ int16x8_t __attribute__((__always_inline__))
vreinterpretq_s16_s8(int8x16_t __a)
{
    return (int16x8_t)__a;
}

static __inline__ int16x8_t __attribute__((__always_inline__))
vreinterpretq_s16_u16(uint16x8_t __a)
{
    return (int16x8_t)__a;
}

static __inline__ int16x8_t __attribute__((__always_inline__))
vreinterpretq_s16_u32(uint32x4_t __a)
{
    return (int16x8_t)__a;
}

static __inline__ int16x8_t __attribute__((__always_inline__))
vreinterpretq_s16_s32(int32x4_t __a)
{
    return (int16x8_t)__a;
}

static __inline__ int16x8_t __attribute__((__always_inline__))
vreinterpretq_s16_u64(uint64x2_t __a)
{
    return (int16x8_t)__a;
}

static __inline__ int16x8_t __attribute__((__always_inline__))
vreinterpretq_s16_s64(int64x2_t __a)
{
    return (int16x8_t)__a;
}

static __inline__ int16x8_t __attribute__((__always_inline__))
vreinterpretq_s16_f32(float32x4_t __a)
{
    return (int16x8_t)__a;
}

static __inline__ int16x8_t __attribute__((__always_inline__))
vreinterpretq_s16_f64(float64x2_t __a)
{
    return (int16x8_t)__a;
}

static __inline__ int16x8_t __attribute__((__always_inline__))
vreinterpretq_s16_p8(poly8x16_t __a)
{
    return (int16x8_t)__a;
}

static __inline__ int16x8_t __attribute__((__always_inline__))
vreinterpretq_s16_p16(poly16x8_t __a)
{
    return (int16x8_t)__a;
}

static __inline__ int16x8_t __attribute__((__always_inline__))
vreinterpretq_s16_p64(poly64x2_t __a)
{
    return (int16x8_t)__a;
}

static __inline__ uint32x4_t __attribute__((__always_inline__))
vreinterpretq_u32_u8(uint8x16_t __a)
{
    return (uint32x4_t)__a;
}

static __inline__ uint32x4_t __attribute__((__always_inline__))
vreinterpretq_u32_s8(int8x16_t __a)
{
    return (uint32x4_t)__a;
}

static __inline__ uint32x4_t __attribute__((__always_inline__))
vreinterpretq_u32_u16(uint16x8_t __a)
{
    return (uint32x4_t)__a;
}

static __inline__ uint32x4_t __attribute__((__always_inline__))
vreinterpretq_u32_s16(int16x8_t __a)
{
    return (uint32x4_t)__a;
}

static __inline__ uint32x4_t __attribute__((__always_inline__))
vreinterpretq_u32_s32(int32x4_t __a)
{
    return (uint32x4_t)__a;
}

static __inline__ uint32x4_t __attribute__((__always_inline__))
vreinterpretq_u32_u64(uint64x2_t __a)
{
    return (uint32x4_t)__a;
}

static __inline__ uint32x4_t __attribute__((__always_inline__))
vreinterpretq_u32_s64(int64x2_t __a)
{
    return (uint32x4_t)__a;
}

static __inline__ uint32x4_t __attribute__((__always_inline__))
vreinterpretq_u32_f32(float32x4_t __a)
{
    return (uint32x4_t)__a;
}

static __inline__ uint32x4_t __attribute__((__always_inline__))
vreinterpretq_u32_f64(float64x2_t __a)
{
    return (uint32x4_t)__a;
}

static __inline__ uint32x4_t __attribute__((__always_inline__))
vreinterpretq_u32_p8(poly8x16_t __a)
{
    return (uint32x4_t)__a;
}

static __inline__ uint32x4_t __attribute__((__always_inline__))
vreinterpretq_u32_p16(poly16x8_t __a)
{
    return (uint32x4_t)__a;
}

static __inline__ uint32x4_t __attribute__((__always_inline__))
vreinterpretq_u32_p64(poly64x2_t __a)
{
    return (uint32x4_t)__a;
}

static __inline__ int32x4_t __attribute__((__always_inline__))
vreinterpretq_s32_u8(uint8x16_t __a)
{
    return (int32x4_t)__a;
}

static __inline__ int32x4_t __attribute__((__always_inline__))
vreinterpretq_s32_s8(int8x16_t __a)
{
    return (int32x4_t)__a;
}

static __inline__ int32x4_t __attribute__((__always_inline__))
vreinterpretq_s32_u16(uint16x8_t __a)
{
    return (int32x4_t)__a;
}

static __inline__ int32x4_t __attribute__((__always_inline__))
vreinterpretq_s32_s16(int16x8_t __a)
{
    return (int32x4_t)__a;
}

static __inline__ int32x4_t __attribute__((__always_inline__))
vreinterpretq_s32_u32(uint32x4_t __a)
{
    return (int32x4_t)__a;
}

static __inline__ int32x4_t __attribute__((__always_inline__))
vreinterpretq_s32_u64(uint64x2_t __a)
{
    return (int32x4_t)__a;
}

static __inline__ int32x4_t __attribute__((__always_inline__))
vreinterpretq_s32_s64(int64x2_t __a)
{
    return (int32x4_t)__a;
}

static __inline__ int32x4_t __attribute__((__always_inline__))
vreinterpretq_s32_f32(float32x4_t __a)
{
    return (int32x4_t)__a;
}

static __inline__ int32x4_t __attribute__((__always_inline__))
vreinterpretq_s32_f64(float64x2_t __a)
{
    return (int32x4_t)__a;
}

static __inline__ int32x4_t __attribute__((__always_inline__))
vreinterpretq_s32_p8(poly8x16_t __a)
{
    return (int32x4_t)__a;
}

static __inline__ int32x4_t __attribute__((__always_inline__))
vreinterpretq_s32_p16(poly16x8_t __a)
{
    return (int32x4_t)__a;
}

static __inline__ int32x4_t __attribute__((__always_inline__))
vreinterpretq_s32_p64(poly64x2_t __a)
{
    return (int32x4_t)__a;
}

static __inline__ uint64x2_t __attribute__((__always_inline__))
vreinterpretq_u64_u8(uint8x16_t __a)
{
    return (uint64x2_t)__a;
}

static __inline__ uint64x2_t __attribute__((__always_inline__))
vreinterpretq_u64_s8(int8x16_t __a)
{
    return (uint64x2_t)__a;
}

static __inline__ uint64x2_t __attribute__((__always_inline__))
vreinterpretq_u64_u16(uint16x8_t __a)
{
    return (uint64x2_t)__a;
}

static __inline__ uint64x2_t __attribute__((__always_inline__))
vreinterpretq_u64_s16(int16x8_t __a)
{
    return (uint64x2_t)__a;
}

static __inline__ uint64x2_t __attribute__((__always_inline__))
vreinterpretq_u64_u32(uint32x4_t __a)
{
    return (uint64x2_t)__a;
}

static __inline__ uint64x2_t __attribute__((__always_inline__))
vreinterpretq_u64_s32(int32x4_t __a)
{
    return (uint64x2_t)__a;
}

static __inline__ uint64x2_t __attribute__((__always_inline__))
vreinterpretq_u64_s64(int64x2_t __a)
{
    return (uint64x2_t)__a;
}

static __inline__ uint64x2_t __attribute__((__always_inline__))
vreinterpretq_u64_f32(float32x4_t __a)
{
    return (uint64x2_t)__a;
}

static __inline__ uint64x2_t __attribute__((__always_inline__))
vreinterpretq_u64_f64(float64x2_t __a)
{
    return (uint64x2_t)__a;
}

static __inline__ uint64x2_t __attribute__((__always_inline__))
vreinterpretq_u64_p8(poly8x16_t __a)
{
    return (uint64x2_t)__a;
}

static __inline__ uint64x2_t __attribute__((__always_inline__))
vreinterpretq_u64_p16(poly16x8_t __a)
{
    return (uint64x2_t)__a;
}

static __inline__ uint64x2_t __attribute__((__always_inline__))
vreinterpretq_u64_p64(poly64x2_t __a)
{
    return (uint64x2_t)__a;
}

static __inline__ int64x2_t __attribute__((__always_inline__))
vreinterpretq_s64_u8(uint8x16_t __a)
{
    return (int64x2_t)__a;
}

static __inline__ int64x2_t __attribute__((__always_inline__))
vreinterpretq_s64_s8(int8x16_t __a)
{
    return (int64x2_t)__a;
}

static __inline__ int64x2_t __attribute__((__always_inline__))
vreinterpretq_s64_u16(uint16x8_t __a)
{
    return (int64x2_t)__a;
}

static __inline__ int64x2_t __attribute__((__always_inline__))
vreinterpretq_s64_s16(int16x8_t __a)
{
    return (int64x2_t)__a;
}

static __inline__ int64x2_t __attribute__((__always_inline__))
vreinterpretq_s64_u32(uint32x4_t __a)
{
    return (int64x2_t)__a;
}

static __inline__ int64x2_t __attribute__((__always_inline__))
vreinterpretq_s64_s32(int32x4_t __a)
{
    return (int64x2_t)__a;
}

static __inline__ int64x2_t __attribute__((__always_inline__))
vreinterpretq_s64_u64(uint64x2_t __a)
{
    return (int64x2_t)__a;
}

static __inline__ int64x2_t __attribute__((__always_inline__))
vreinterpretq_s64_f32(float32x4_t __a)
{
    return (int64x2_t)__a;
}

static __inline__ int64x2_t __attribute__((__always_inline__))
vreinterpretq_s64_f64(float64x2_t __a)
{
    return (int64x2_t)__a;
}

static __inline__ int64x2_t __attribute__((__always_inline__))
vreinterpretq_s64_p8(poly8x16_t __a)
{
    return (int64x2_t)__a;
}

static __inline__ int64x2_t __attribute__((__always_inline__))
vreinterpretq_s64_p16(poly16x8_t __a)
{
    return (int64x2_t)__a;
}

static __inline__ int64x2_t __attribute__((__always_inline__))
vreinterpretq_s64_p64(poly64x2_t __a)
{
    return (int64x2_t)__a;
}

static __inline__ float32x4_t __attribute__((__always_inline__))
vreinterpretq_f32_u8(uint8x16_t __a)
{
    return (float32x4_t)__a;
}

static __inline__ float32x4_t __attribute__((__always_inline__))
vreinterpretq_f32_s8(int8x16_t __a)
{
    return (float32x4_t)__a;
}

static __inline__ float32x4_t __attribute__((__always_inline__))
vreinterpretq_f32_u16(uint16x8_t __a)
{
    return (float32x4_t)__a;
}

static __inline__ float32x4_t __attribute__((__always_inline__))
vreinterpretq_f32_s16(int16x8_t __a)
{
    return (float32x4_t)__a;
}

static __inline__ float32x4_t __attribute__((__always_inline__))
vreinterpretq_f32_u32(uint32x4_t __a)
{
    return (float32x4_t)__a;
}

static __inline__ float32x4_t __attribute__((__always_inline__))
vreinterpretq_f32_s32(int32x4_t __a)
{
    return (float32x4_t)__a;
}

static __inline__ float32x4_t __attribute__((__always_inline__))
vreinterpretq_f32_u64(uint64x2_t __a)
{
    return (float32x4_t)__a;
}

static __inline__ float32x4_t __attribute__((__always_inline__))
vreinterpretq_f32_s64(int64x2_t __a)
{
    return (float32x4_t)__a;
}

static __inline__ float32x4_t __attribute__((__always_inline__))
vreinterpretq_f32_f64(float64x2_t __a)
{
    return (float32x4_t)__a;
}

static __inline__ float32x4_t __attribute__((__always_inline__))
vreinterpretq_f32_p8(poly8x16_t __a)
{
    return (float32x4_t)__a;
}

static __inline__ float32x4_t __attribute__((__always_inline__))
vreinterpretq_f32_p16(poly16x8_t __a)
{
    return (float32x4_t)__a;
}

static __inline__ float32x4_t __attribute__((__always_inline__))
vreinterpretq_f32_p64(poly64x2_t __a)
{
    return (float32x4_t)__a;
}

static __inline__ float64x2_t __attribute__((__always_inline__))
vreinterpretq_f64_u8(uint8x16_t __a)
{
    return (float64x2_t)__a;
}

static __inline__ float64x2_t __attribute__((__always_inline__))
vreinterpretq_f64_s8(int8x16_t __a)
{
    return (float64x2_t)__a;
}

static __inline__ float64x2_t __attribute__((__always_inline__))
vreinterpretq_f64_u16(uint16x8_t __a)
{
    return (float64x2_t)__a;
}

static __inline__ float64x2_t __attribute__((__always_inline__))
vreinterpretq_f64_s16(int16x8_t __a)
{
    return (float64x2_t)__a;
}

static __inline__ float64x2_t __attribute__((__always_inline__))
vreinterpretq_f64_u32(uint32x4_t __a)
{
    return (float64x2_t)__a;
}

static __inline__ float64x2_t __attribute__((__always_inline__))
vreinterpretq_f64_s32(int32x4_t __a)
{
    return (float64x2_t)__a;
}

static __inline__ float64x2_t __attribute__((__always_inline__))
vreinterpretq_f64_u64(uint64x2_t __a)
{
    return (float64x2_t)__a;
}

static __inline__ float64x2_t __attribute__((__always_inline__))
vreinterpretq_f64_s64(int64x2_t __a)
{
    return (float64x2_t)__a;
}

static __inline__ float64x2_t __attribute__((__always_inline__))
vreinterpretq_f64_f32(float32x4_t __a)
{
    return (float64x2_t)__a;
}

static __inline__ float64x2_t __attribute__((__always_inline__))
vreinterpretq_f64_p8(poly8x16_t __a)
{
    return (float64x2_t)__a;
}

static __inline__ float64x2_t __attribute__((__always_inline__))
vreinterpretq_f64_p16(poly16x8_t __a)
{
    return (float64x2_t)__a;
}

static __inline__ float64x2_t __attribute__((__always_inline__))
vreinterpretq_f64_p64(poly64x2_t __a)
{
    return (float64x2_t)__a;
}

static __inline__ poly8x16_t __attribute__((__always_inline__))
vreinterpretq_p8_u8(uint8x16_t __a)
{
    return (poly8x16_t)__a;
}

static __inline__ poly8x16_t __attribute__((__always_inline__))
vreinterpretq_p8_s8(int8x16_t __a)
{
    return (poly8x16_t)__a;
}

static __inline__ poly8x16_t __attribute__((__always_inline__))
vreinterpretq_p8_u16(uint16x8_t __a)
{
    return (poly8x16_t)__a;
}

static __inline__ poly8x16_t __attribute__((__always_inline__))
vreinterpretq_p8_s16(int16x8_t __a)
{
    return (poly8x16_t)__a;
}

static __inline__ poly8x16_t __attribute__((__always_inline__))
vreinterpretq_p8_u32(uint32x4_t __a)
{
    return (poly8x16_t)__a;
}

static __inline__ poly8x16_t __attribute__((__always_inline__))
vreinterpretq_p8_s32(int32x4_t __a)
{
    return (poly8x16_t)__a;
}

static __inline__ poly8x16_t __attribute__((__always_inline__))
vreinterpretq_p8_u64(uint64x2_t __a)
{
    return (poly8x16_t)__a;
}

static __inline__ poly8x16_t __attribute__((__always_inline__))
vreinterpretq_p8_s64(int64x2_t __a)
{
    return (poly8x16_t)__a;
}

static __inline__ poly8x16_t __attribute__((__always_inline__))
vreinterpretq_p8_f32(float32x4_t __a)
{
    return (poly8x16_t)__a;
}

static __inline__ poly8x16_t __attribute__((__always_inline__))
vreinterpretq_p8_f64(float64x2_t __a)
{
    return (poly8x16_t)__a;
}

static __inline__ poly8x16_t __attribute__((__always_inline__))
vreinterpretq_p8_p16(poly16x8_t __a)
{
    return (poly8x16_t)__a;
}

static __inline__ poly8x16_t __attribute__((__always_inline__))
vreinterpretq_p8_p64(poly64x2_t __a)
{
    return (poly8x16_t)__a;
}

static __inline__ poly16x8_t __attribute__((__always_inline__))
vreinterpretq_p16_u8(uint8x16_t __a)
{
    return (poly16x8_t)__a;
}

static __inline__ poly16x8_t __attribute__((__always_inline__))
vreinterpretq_p16_s8(int8x16_t __a)
{
    return (poly16x8_t)__a;
}

static __inline__ poly16x8_t __attribute__((__always_inline__))
vreinterpretq_p16_u16(uint16x8_t __a)
{
    return (poly16x8_t)__a;
}

static __inline__ poly16x8_t __attribute__((__always_inline__))
vreinterpretq_p16_s16(int16x8_t __a)
{
    return (poly16x8_t)__a;
}

static __inline__ poly16x8_t __attribute__((__always_inline__))
vreinterpretq_p16_u32(uint32x4_t __a)
{
    return (poly16x8_t)__a;
}

static __inline__ poly16x8_t __attribute__((__always_inline__))
vreinterpretq_p16_s32(int32x4_t __a)
{
    return (poly16x8_t)__a;
}

static __inline__ poly16x8_t __attribute__((__always_inline__))
vreinterpretq_p16_u64(uint64x2_t __a)
{
    return (poly16x8_t)__a;
}

static __inline__ poly16x8_t __attribute__((__always_inline__))
vreinterpretq_p16_s64(int64x2_t __a)
{
    return (poly16x8_t)__a;
}

static __inline__ poly16x8_t __attribute__((__always_inline__))
vreinterpretq_p16_f32(float32x4_t __a)
{
    return (poly16x8_t)__a;
}

static __inline__ poly16x8_t __attribute__((__always_inline__))
vreinterpretq_p16_f64(float64x2_t __a)
{
    return (poly16x8_t)__a;
}

static __inline__ poly16x8_t __attribute__((__always_inline__))
vreinterpretq_p16_p8(poly8x16_t __a)
{
    return (poly16x8_t)__a;
}

static __inline__ poly16x8_t __attribute__((__always_inline__))
vreinterpretq_p16_p64(poly64x2_t __a)
{
    return (poly16x8_t)__a;
}

static __inline__ poly64x2_t __attribute__((__always_inline__))
vreinterpretq_p64_u8(uint8x16_t __a)
{
    return (poly64x2_t)__a;
}

static __inline__ poly64x2_t __attribute__((__always_inline__))
vreinterpretq_p64_s8(int8x16_t __a)
{
    return (poly64x2_t)__a;
}

static __inline__ poly64x2_t __attribute__((__always_inline__))
vreinterpretq_p64_u16(uint16x8_t __a)
{
    return (poly64x2_t)__a;
}

static __inline__ poly64x2_t __attribute__((__always_inline__))
vreinterpretq_p64_s16(int16x8_t __a)
{
    return (poly64x2_t)__a;
}

static __inline__ poly64x2_t __attribute__((__always_inline__))
vreinterpretq_p64_u32(uint32x4_t __a)
{
    return (poly64x2_t)__a;
}

static __inline__ poly64x2_t __attribute__((__always_inline__))
vreinterpretq_p64_s32(int32x4_t __a)
{
    return (poly64x2_t)__a;
}

static __inline__ poly64x2_t __attribute__((__always_inline__))
vreinterpretq_p64_u64(uint64x2_t __a)
{
    return (poly64x2_t)__a;
}

static __inline__ poly64x2_t __attribute__((__always_inline__))
vreinterpretq_p64_s64(int64x2_t __a)
{
    return (poly64x2_t)__a;
}

static __inline__ poly64x2_t __attribute__((__always_inline__))
vreinterpretq_p64_f32(float32x4_t __a)
{
    return (poly64x2_t)__a;
}

static __inline__ poly64x2_t __attribute__((__always_inline__))
vreinterpretq_p64_f64(float64x2_t __a)
{
    return (poly64x2_t)__a;
}

static __inline__ poly64x2_t __attribute__((__always_inline__))
vreinterpretq_p64_p8(poly8x16_t __a)
{
    return (poly64x2_t)__a;
}

static __inline__ poly64x2_t __attribute__((__always_inline__))
vreinterpretq_p64_p16(poly16x8_t __a)
{
    return (poly64x2_t)__a;
}

static __inline__ uint32x4_t __attribute__((__always_inline__))
vld1q_dup_u32(const unsigned int *__p)
{
    uint32x4_t __ret;
    unsigned int __v = *__p;
    __ret[0] = __v;
    __ret[1] = __v;
    __ret[2] = __v;
    __ret[3] = __v;
    return __ret;
}

/* --- vld1q_lane: load one lane --- */

static __inline__ uint32x4_t __attribute__((__always_inline__))
vld1q_lane_u32(const unsigned int *__p, uint32x4_t __v, int __lane)
{
    __v[__lane] = *__p;
    return __v;
}

/* --- vld1q multi-load --- */

static __inline__ uint8x16x4_t __attribute__((__always_inline__))
vld1q_u8_x4(const unsigned char *__p)
{
    uint8x16x4_t __ret;
    __builtin_memcpy(&__ret, __p, 64);
    return __ret;
}

/* --- vld1: load one 64-bit vector --- */

/* --- Structure loads (de-interleave) --- */

static __inline__ uint8x16x3_t __attribute__((__always_inline__))
vld3q_u8(const unsigned char *__p)
{
    /* De-interleave 48 bytes into 3 vectors of 16 bytes each */
    uint8x16x3_t __ret;
    for (int __i = 0; __i < 16; __i++) {
        __ret.val[0][__i] = __p[__i * 3 + 0];
        __ret.val[1][__i] = __p[__i * 3 + 1];
        __ret.val[2][__i] = __p[__i * 3 + 2];
    }
    return __ret;
}

static __inline__ uint8x8x3_t __attribute__((__always_inline__))
vld3_dup_u8(const unsigned char *__p)
{
    /* Load 3 bytes and duplicate each into all lanes */
    uint8x8x3_t __ret;
    for (int __i = 0; __i < 8; __i++) {
        __ret.val[0][__i] = __p[0];
        __ret.val[1][__i] = __p[1];
        __ret.val[2][__i] = __p[2];
    }
    return __ret;
}

static __inline__ uint8x8x3_t __attribute__((__always_inline__))
vld3_lane_u8(const unsigned char *__p, uint8x8x3_t __v, int __lane)
{
    __v.val[0][__lane] = __p[0];
    __v.val[1][__lane] = __p[1];
    __v.val[2][__lane] = __p[2];
    return __v;
}

static __inline__ uint32x2x4_t __attribute__((__always_inline__))
vld4_u32(const unsigned int *__p)
{
    /* De-interleave 8 uint32s into 4 vectors of 2 */
    uint32x2x4_t __ret;
    for (int __i = 0; __i < 2; __i++) {
        __ret.val[0][__i] = __p[__i * 4 + 0];
        __ret.val[1][__i] = __p[__i * 4 + 1];
        __ret.val[2][__i] = __p[__i * 4 + 2];
        __ret.val[3][__i] = __p[__i * 4 + 3];
    }
    return __ret;
}

/* ================================================================== */
/*                         STORE INTRINSICS                           */
/* ================================================================== */

/* --- vst1_lane: store one lane --- */

static __inline__ void __attribute__((__always_inline__))
vst1_lane_u32(unsigned int *__p, uint32x2_t __a, int __lane)
{
    *__p = __a[__lane];
}

/* --- Structure stores (interleave) --- */

static __inline__ void __attribute__((__always_inline__))
vst3_u8(unsigned char *__p, uint8x8x3_t __a)
{
    for (int __i = 0; __i < 8; __i++) {
        __p[__i * 3 + 0] = __a.val[0][__i];
        __p[__i * 3 + 1] = __a.val[1][__i];
        __p[__i * 3 + 2] = __a.val[2][__i];
    }
}

static __inline__ void __attribute__((__always_inline__))
vst4q_u8(unsigned char *__p, uint8x16x4_t __a)
{
    for (int __i = 0; __i < 16; __i++) {
        __p[__i * 4 + 0] = __a.val[0][__i];
        __p[__i * 4 + 1] = __a.val[1][__i];
        __p[__i * 4 + 2] = __a.val[2][__i];
        __p[__i * 4 + 3] = __a.val[3][__i];
    }
}

static __inline__ void __attribute__((__always_inline__))
vst4_lane_u32(unsigned int *__p, uint32x2x4_t __a, int __lane)
{
    __p[0] = __a.val[0][__lane];
    __p[1] = __a.val[1][__lane];
    __p[2] = __a.val[2][__lane];
    __p[3] = __a.val[3][__lane];
}

/* ================================================================== */
/*                       BITWISE OPERATIONS                           */
/* ================================================================== */

/* --- 64-bit bitwise --- */

/* ================================================================== */
/*                       SHIFT OPERATIONS                             */
/* ================================================================== */

/* ================================================================== */
/*                    DUPLICATE (BROADCAST)                            */
/* ================================================================== */

/* ================================================================== */
/*                    COMBINE / SPLIT                                  */
/* ================================================================== */

static __inline__ uint8x16_t __attribute__((__always_inline__))
vcombine_u8(uint8x8_t __lo, uint8x8_t __hi)
{
    uint8x16_t __ret;
    __builtin_memcpy(&__ret[0], &__lo[0], 8);
    __builtin_memcpy(&__ret[8], &__hi[0], 8);
    return __ret;
}

static __inline__ int8x16_t __attribute__((__always_inline__))
vcombine_s8(int8x8_t __lo, int8x8_t __hi)
{
    int8x16_t __ret;
    __builtin_memcpy(&__ret[0], &__lo[0], 8);
    __builtin_memcpy(&__ret[8], &__hi[0], 8);
    return __ret;
}

static __inline__ uint64x2_t __attribute__((__always_inline__))
vcombine_u64(uint64x1_t __lo, uint64x1_t __hi)
{
    uint64x2_t __ret;
    __ret[0] = __lo[0];
    __ret[1] = __hi[0];
    return __ret;
}

static __inline__ uint16x8_t __attribute__((__always_inline__))
vcombine_u16(uint16x4_t __lo, uint16x4_t __hi)
{
    uint16x8_t __ret;
    __builtin_memcpy(&__ret[0], &__lo[0], 8);
    __builtin_memcpy(&__ret[4], &__hi[0], 8);
    return __ret;
}

/* --- Get high/low halves --- */

static __inline__ uint8x8_t __attribute__((__always_inline__))
vget_low_u8(uint8x16_t __a)
{
    uint8x8_t __ret;
    __builtin_memcpy(&__ret[0], &__a[0], 8);
    return __ret;
}

static __inline__ uint8x8_t __attribute__((__always_inline__))
vget_high_u8(uint8x16_t __a)
{
    uint8x8_t __ret;
    __builtin_memcpy(&__ret[0], &__a[8], 8);
    return __ret;
}

static __inline__ int8x8_t __attribute__((__always_inline__))
vget_low_s8(int8x16_t __a)
{
    int8x8_t __ret;
    __builtin_memcpy(&__ret[0], &__a[0], 8);
    return __ret;
}

static __inline__ int8x8_t __attribute__((__always_inline__))
vget_high_s8(int8x16_t __a)
{
    int8x8_t __ret;
    __builtin_memcpy(&__ret[0], &__a[8], 8);
    return __ret;
}

static __inline__ uint64x1_t __attribute__((__always_inline__))
vget_low_u64(uint64x2_t __a)
{
    uint64x1_t __ret;
    __ret[0] = __a[0];
    return __ret;
}

static __inline__ uint64x1_t __attribute__((__always_inline__))
vget_high_u64(uint64x2_t __a)
{
    uint64x1_t __ret;
    __ret[0] = __a[1];
    return __ret;
}

static __inline__ poly64x1_t __attribute__((__always_inline__))
vget_low_p64(poly64x2_t __a)
{
    poly64x1_t __ret;
    __ret[0] = __a[0];
    return __ret;
}

/* ================================================================== */
/*                       LANE ACCESS                                   */
/* ================================================================== */

/* ================================================================== */
/*                    ARITHMETIC OPERATIONS                            */
/* ================================================================== */

/* --- Widening arithmetic --- */

static __inline__ uint16x8_t __attribute__((__always_inline__))
vaddl_u8(uint8x8_t __a, uint8x8_t __b)
{
    /* Widening add: u8 + u8 -> u16 */
    uint16x8_t __ret;
    for (int __i = 0; __i < 8; __i++)
        __ret[__i] = (unsigned short)__a[__i] + (unsigned short)__b[__i];
    return __ret;
}

static __inline__ uint16x8_t __attribute__((__always_inline__))
vabdl_u8(uint8x8_t __a, uint8x8_t __b)
{
    /* Widening absolute difference: |a - b| -> u16 */
    uint16x8_t __ret;
    for (int __i = 0; __i < 8; __i++) {
        int __d = (int)__a[__i] - (int)__b[__i];
        __ret[__i] = (unsigned short)(__d < 0 ? -__d : __d);
    }
    return __ret;
}

static __inline__ uint16x8_t __attribute__((__always_inline__))
vabdq_u16(uint16x8_t __a, uint16x8_t __b)
{
    uint16x8_t __ret;
    for (int __i = 0; __i < 8; __i++) {
        int __d = (int)__a[__i] - (int)__b[__i];
        __ret[__i] = (unsigned short)(__d < 0 ? -__d : __d);
    }
    return __ret;
}

static __inline__ uint8x8_t __attribute__((__always_inline__))
vhadd_u8(uint8x8_t __a, uint8x8_t __b)
{
    /* Halving add: (a + b) >> 1 without overflow */
    uint8x8_t __ret;
    for (int __i = 0; __i < 8; __i++)
        __ret[__i] = ((unsigned short)__a[__i] + (unsigned short)__b[__i]) >> 1;
    return __ret;
}

/* --- Narrowing --- */

static __inline__ uint8x8_t __attribute__((__always_inline__))
vmovn_u16(uint16x8_t __a)
{
    /* Narrow: take low 8 bits of each u16 */
    uint8x8_t __ret;
    for (int __i = 0; __i < 8; __i++)
        __ret[__i] = (unsigned char)__a[__i];
    return __ret;
}

/* ================================================================== */
/*                    COMPARISON OPERATIONS                            */
/* ================================================================== */

/* ================================================================== */
/*                    EXTRACT / SHUFFLE                                */
/* ================================================================== */

static __inline__ uint8x16_t __attribute__((__always_inline__))
vextq_u8(uint8x16_t __a, uint8x16_t __b, int __n)
{
    /* Extract: concatenate a and b, then extract 16 bytes starting at byte __n */
    uint8x16_t __ret;
    for (int __i = 0; __i < 16; __i++) {
        int __idx = __i + __n;
        __ret[__i] = (__idx < 16) ? __a[__idx] : __b[__idx - 16];
    }
    return __ret;
}

static __inline__ uint8x8_t __attribute__((__always_inline__))
vext_u8(uint8x8_t __a, uint8x8_t __b, int __n)
{
    uint8x8_t __ret;
    for (int __i = 0; __i < 8; __i++) {
        int __idx = __i + __n;
        __ret[__i] = (__idx < 8) ? __a[__idx] : __b[__idx - 8];
    }
    return __ret;
}

/* ================================================================== */
/*                 POLYNOMIAL MULTIPLICATION                           */
/* ================================================================== */

static __inline__ poly8x16_t __attribute__((__always_inline__))
vmulq_p8(poly8x16_t __a, poly8x16_t __b)
{
    /* Polynomial (carry-less) multiply per byte */
    poly8x16_t __ret;
    for (int __i = 0; __i < 16; __i++) {
        unsigned char __r = 0;
        unsigned char __av = __a[__i];
        unsigned char __bv = __b[__i];
        for (int __j = 0; __j < 8; __j++) {
            if (__bv & (1 << __j))
                __r ^= __av << __j;
        }
        __ret[__i] = __r;
    }
    return __ret;
}

/* 64-bit polynomial multiply (for crypto/GHASH).
 * Scalar C implementation of carry-less multiplication - functionally correct
 * but much slower than the hardware PMULL instruction. */
static __inline__ poly128_t __attribute__((__always_inline__))
vmull_p64(poly64_t __a, poly64_t __b)
{
    /* Carry-less multiply of two 64-bit polynomials -> 128-bit result */
    __uint128_t __r = 0;
    for (int __j = 0; __j < 64; __j++) {
        if (__b & (1ULL << __j))
            __r ^= (__uint128_t)__a << __j;
    }
    return (poly128_t)__r;
}

static __inline__ poly128_t __attribute__((__always_inline__))
vmull_high_p64(poly64x2_t __a, poly64x2_t __b)
{
    return vmull_p64(__a[1], __b[1]);
}

/* ================================================================== */
/*                    REVERSE OPERATIONS                               */
/* ================================================================== */

static __inline__ uint16x8_t __attribute__((__always_inline__))
vrev32q_u16(uint16x8_t __a)
{
    /* Reverse 16-bit elements within each 32-bit word */
    uint16x8_t __ret;
    for (int __i = 0; __i < 8; __i += 2) {
        __ret[__i] = __a[__i + 1];
        __ret[__i + 1] = __a[__i];
    }
    return __ret;
}

static __inline__ uint8x16_t __attribute__((__always_inline__))
vrbitq_u8(uint8x16_t __a)
{
    /* Reverse bits within each byte */
    uint8x16_t __ret;
    for (int __i = 0; __i < 16; __i++) {
        unsigned char __v = __a[__i];
        unsigned char __r = 0;
        for (int __j = 0; __j < 8; __j++)
            __r |= ((__v >> __j) & 1) << (7 - __j);
        __ret[__i] = __r;
    }
    return __ret;
}

/* ================================================================== */
/*                   TABLE LOOKUP OPERATIONS                           */
/* ================================================================== */

static __inline__ uint8x16_t __attribute__((__always_inline__))
vqtbl1q_u8(uint8x16_t __a, uint8x16_t __b)
{
    uint8x16_t __ret;
    for (int __i = 0; __i < 16; __i++) {
        unsigned char __idx = __b[__i];
        __ret[__i] = (__idx < 16) ? __a[__idx] : 0;
    }
    return __ret;
}

static __inline__ uint8x16_t __attribute__((__always_inline__))
vqtbx1q_u8(uint8x16_t __def, uint8x16_t __a, uint8x16_t __b)
{
    uint8x16_t __ret;
    for (int __i = 0; __i < 16; __i++) {
        unsigned char __idx = __b[__i];
        __ret[__i] = (__idx < 16) ? __a[__idx] : __def[__i];
    }
    return __ret;
}

static __inline__ uint8x16_t __attribute__((__always_inline__))
vqtbl4q_u8(uint8x16x4_t __a, uint8x16_t __b)
{
    uint8x16_t __ret;
    for (int __i = 0; __i < 16; __i++) {
        unsigned char __idx = __b[__i];
        if (__idx < 16)
            __ret[__i] = __a.val[0][__idx];
        else if (__idx < 32)
            __ret[__i] = __a.val[1][__idx - 16];
        else if (__idx < 48)
            __ret[__i] = __a.val[2][__idx - 32];
        else if (__idx < 64)
            __ret[__i] = __a.val[3][__idx - 48];
        else
            __ret[__i] = 0;
    }
    return __ret;
}

static __inline__ uint8x16_t __attribute__((__always_inline__))
vqtbx4q_u8(uint8x16_t __def, uint8x16x4_t __a, uint8x16_t __b)
{
    uint8x16_t __ret;
    for (int __i = 0; __i < 16; __i++) {
        unsigned char __idx = __b[__i];
        if (__idx < 16)
            __ret[__i] = __a.val[0][__idx];
        else if (__idx < 32)
            __ret[__i] = __a.val[1][__idx - 16];
        else if (__idx < 48)
            __ret[__i] = __a.val[2][__idx - 32];
        else if (__idx < 64)
            __ret[__i] = __a.val[3][__idx - 48];
        else
            __ret[__i] = __def[__i];
    }
    return __ret;
}

/* 64-bit table lookup (AArch32 compat) */

static __inline__ uint8x8_t __attribute__((__always_inline__))
vtbl2_u8(uint8x8x2_t __a, uint8x8_t __b)
{
    uint8x8_t __ret;
    for (int __i = 0; __i < 8; __i++) {
        unsigned char __idx = __b[__i];
        if (__idx < 8)
            __ret[__i] = __a.val[0][__idx];
        else if (__idx < 16)
            __ret[__i] = __a.val[1][__idx - 8];
        else
            __ret[__i] = 0;
    }
    return __ret;
}

static __inline__ uint8x8_t __attribute__((__always_inline__))
vtbx2_u8(uint8x8_t __def, uint8x8x2_t __a, uint8x8_t __b)
{
    uint8x8_t __ret;
    for (int __i = 0; __i < 8; __i++) {
        unsigned char __idx = __b[__i];
        if (__idx < 8)
            __ret[__i] = __a.val[0][__idx];
        else if (__idx < 16)
            __ret[__i] = __a.val[1][__idx - 8];
        else
            __ret[__i] = __def[__i];
    }
    return __ret;
}

/* ================================================================== */
/*                   PAIRWISE / ACROSS-LANE                            */
/* ================================================================== */

static __inline__ int8x8_t __attribute__((__always_inline__))
vpmin_s8(int8x8_t __a, int8x8_t __b)
{
    int8x8_t __ret;
    for (int __i = 0; __i < 4; __i++) {
        signed char __x = __a[2 * __i];
        signed char __y = __a[2 * __i + 1];
        __ret[__i] = (__x < __y) ? __x : __y;
    }
    for (int __i = 0; __i < 4; __i++) {
        signed char __x = __b[2 * __i];
        signed char __y = __b[2 * __i + 1];
        __ret[4 + __i] = (__x < __y) ? __x : __y;
    }
    return __ret;
}

static __inline__ signed char __attribute__((__always_inline__))
vminvq_s8(int8x16_t __a)
{
    signed char __min = __a[0];
    for (int __i = 1; __i < 16; __i++) {
        if (__a[__i] < __min)
            __min = __a[__i];
    }
    return __min;
}

/* vminvq_u32: horizontal minimum across uint32x4_t */
static __inline__ unsigned int __attribute__((__always_inline__))
vminvq_u32(uint32x4_t __a)
{
    unsigned int __min = __a[0];
    for (int __i = 1; __i < 4; __i++) {
        if (__a[__i] < __min)
            __min = __a[__i];
    }
    return __min;
}

/* ================================================================== */
/*                       AES INTRINSICS                                */
/* ================================================================== */
/* ARM Crypto Extension AES intrinsics using inline assembly.          */

static __inline__ uint8x16_t __attribute__((__always_inline__))
vaeseq_u8(uint8x16_t __a, uint8x16_t __b)
{
    uint8x16_t __ret;
    __asm__ __volatile__(
        "ldr q0, [%[a]]\n\t"
        "ldr q1, [%[b]]\n\t"
        "aese v0.16b, v1.16b\n\t"
        "str q0, [%[ret]]\n\t"
        :
        : [a] "r" (&__a), [b] "r" (&__b), [ret] "r" (&__ret)
        : "v0", "v1", "memory"
    );
    return __ret;
}

static __inline__ uint8x16_t __attribute__((__always_inline__))
vaesmcq_u8(uint8x16_t __a)
{
    uint8x16_t __ret;
    __asm__ __volatile__(
        "ldr q0, [%[a]]\n\t"
        "aesmc v0.16b, v0.16b\n\t"
        "str q0, [%[ret]]\n\t"
        :
        : [a] "r" (&__a), [ret] "r" (&__ret)
        : "v0", "memory"
    );
    return __ret;
}

static __inline__ uint8x16_t __attribute__((__always_inline__))
vaesdq_u8(uint8x16_t __a, uint8x16_t __b)
{
    uint8x16_t __ret;
    __asm__ __volatile__(
        "ldr q0, [%[a]]\n\t"
        "ldr q1, [%[b]]\n\t"
        "aesd v0.16b, v1.16b\n\t"
        "str q0, [%[ret]]\n\t"
        :
        : [a] "r" (&__a), [b] "r" (&__b), [ret] "r" (&__ret)
        : "v0", "v1", "memory"
    );
    return __ret;
}

static __inline__ uint8x16_t __attribute__((__always_inline__))
vaesimcq_u8(uint8x16_t __a)
{
    uint8x16_t __ret;
    __asm__ __volatile__(
        "ldr q0, [%[a]]\n\t"
        "aesimc v0.16b, v0.16b\n\t"
        "str q0, [%[ret]]\n\t"
        :
        : [a] "r" (&__a), [ret] "r" (&__ret)
        : "v0", "memory"
    );
    return __ret;
}

/* ================================================================== */
/*                  TYPE REINTERPRET CASTS                              */
/* ================================================================== */

static __inline__ uint8x16_t __attribute__((__always_inline__))
vreinterpretq_u8_p128(poly128_t __a)
{
    uint8x16_t __ret;
    __builtin_memcpy(&__ret, &__a, 16);
    return __ret;
}

/* ================================================================
 * Additional u32/u64 intrinsics for mbedtls, redis, libsodium
 * ================================================================ */

/* === Create / Duplicate === */

/* vcreate_u32: create uint32x2_t from a raw uint64 value */
static __inline__ uint32x2_t __attribute__((__always_inline__))
vcreate_u32(unsigned long long __a)
{
    uint32x2_t __ret;
    __builtin_memcpy(&__ret, &__a, 8);
    return __ret;
}

/* vcreate_u64: create uint64x1_t from a raw uint64 value */
static __inline__ uint64x1_t __attribute__((__always_inline__))
vcreate_u64(unsigned long long __a)
{
    uint64x1_t __ret;
    __ret[0] = __a;
    return __ret;
}

/* === Combine (two D-registers -> one Q-register) === */

/* vcombine_u32: combine two uint32x2_t into uint32x4_t */
static __inline__ uint32x4_t __attribute__((__always_inline__))
vcombine_u32(uint32x2_t __lo, uint32x2_t __hi)
{
    uint32x4_t __ret;
    __ret[0] = __lo[0];
    __ret[1] = __lo[1];
    __ret[2] = __hi[0];
    __ret[3] = __hi[1];
    return __ret;
}

/* === Get low/high halves === */

/* vget_low_u32: get low 64-bit half of uint32x4_t */
static __inline__ uint32x2_t __attribute__((__always_inline__))
vget_low_u32(uint32x4_t __a)
{
    uint32x2_t __ret;
    __ret[0] = __a[0];
    __ret[1] = __a[1];
    return __ret;
}

/* vget_high_u32: get high 64-bit half of uint32x4_t */
static __inline__ uint32x2_t __attribute__((__always_inline__))
vget_high_u32(uint32x4_t __a)
{
    uint32x2_t __ret;
    __ret[0] = __a[2];
    __ret[1] = __a[3];
    return __ret;
}

/* === Lane access === */

/* === Arithmetic: u32 === */

/* === Arithmetic: u64 === */

/* === Bitwise: u32 === */

/* === Shift: u32 === */

/* vsriq_n_u32: shift right and insert - for each lane,
 * shift __b right by __n, and insert into __a preserving the top __n bits of __a
 * result[i] = (a[i] & ~((1u<<(32-n))-1)) | (b[i] >> n) */
#define vsriq_n_u32(__a, __b, __n) __extension__ ({ \
    uint32x4_t __r; \
    unsigned int __mask = ~((1u << (32 - (__n))) - 1u); \
    __r[0] = ((__a)[0] & __mask) | ((__b)[0] >> (__n)); \
    __r[1] = ((__a)[1] & __mask) | ((__b)[1] >> (__n)); \
    __r[2] = ((__a)[2] & __mask) | ((__b)[2] >> (__n)); \
    __r[3] = ((__a)[3] & __mask) | ((__b)[3] >> (__n)); \
    __r; \
})

/* vsliq_n_u32: shift left and insert - for each lane,
 * shift __b left by __n, and insert into __a preserving the low __n bits of __a */
#define vsliq_n_u32(__a, __b, __n) __extension__ ({ \
    uint32x4_t __r; \
    unsigned int __mask = (1u << (__n)) - 1u; \
    __r[0] = ((__a)[0] & __mask) | ((__b)[0] << (__n)); \
    __r[1] = ((__a)[1] & __mask) | ((__b)[1] << (__n)); \
    __r[2] = ((__a)[2] & __mask) | ((__b)[2] << (__n)); \
    __r[3] = ((__a)[3] & __mask) | ((__b)[3] << (__n)); \
    __r; \
})

/* === Compare: u8 === */

/* vmaxq_u8: element-wise maximum of uint8x16_t */
static __inline__ uint8x16_t __attribute__((__always_inline__))
vmaxq_u8(uint8x16_t __a, uint8x16_t __b)
{
    uint8x16_t __ret;
    for (int __i = 0; __i < 16; __i++)
        __ret[__i] = __a[__i] > __b[__i] ? __a[__i] : __b[__i];
    return __ret;
}

/* === Population count === */

/* vcntq_u8: population count per byte */
static __inline__ uint8x16_t __attribute__((__always_inline__))
vcntq_u8(uint8x16_t __a)
{
    uint8x16_t __ret;
    for (int __i = 0; __i < 16; __i++) {
        unsigned char __v = __a[__i];
        unsigned char __c = 0;
        while (__v) { __c += __v & 1; __v >>= 1; }
        __ret[__i] = __c;
    }
    return __ret;
}

/* === Pairwise add long === */

/* vpaddlq_u8: pairwise add adjacent u8 pairs, result as u16 */
static __inline__ uint16x8_t __attribute__((__always_inline__))
vpaddlq_u8(uint8x16_t __a)
{
    uint16x8_t __ret;
    for (int __i = 0; __i < 8; __i++)
        __ret[__i] = (unsigned short)__a[__i * 2] + (unsigned short)__a[__i * 2 + 1];
    return __ret;
}

/* vpaddlq_u16: pairwise add adjacent u16 pairs, result as u32 */
static __inline__ uint32x4_t __attribute__((__always_inline__))
vpaddlq_u16(uint16x8_t __a)
{
    uint32x4_t __ret;
    for (int __i = 0; __i < 4; __i++)
        __ret[__i] = (unsigned int)__a[__i * 2] + (unsigned int)__a[__i * 2 + 1];
    return __ret;
}

/* vpaddlq_u32: pairwise add adjacent u32 pairs, result as u64 */
static __inline__ uint64x2_t __attribute__((__always_inline__))
vpaddlq_u32(uint32x4_t __a)
{
    uint64x2_t __ret;
    __ret[0] = (unsigned long long)__a[0] + (unsigned long long)__a[1];
    __ret[1] = (unsigned long long)__a[2] + (unsigned long long)__a[3];
    return __ret;
}

/* vpadalq_u8: pairwise add and accumulate long u8 -> u16 */
static __inline__ uint16x8_t __attribute__((__always_inline__))
vpadalq_u8(uint16x8_t __acc, uint8x16_t __a)
{
    uint16x8_t __ret;
    for (int __i = 0; __i < 8; __i++)
        __ret[__i] = __acc[__i] + (unsigned short)__a[__i * 2] + (unsigned short)__a[__i * 2 + 1];
    return __ret;
}

/* === Extract / Rotate === */

/* vextq_u64: extract from pair of uint64x2_t */
#define vextq_u64(__a, __b, __n) __extension__ ({ \
    uint64x2_t __r; \
    if ((__n) == 0) { \
        __r = (__a); \
    } else { \
        __r[0] = (__a)[1]; \
        __r[1] = (__b)[0]; \
    } \
    __r; \
})

/* vextq_u32: extract from pair of uint32x4_t */
#define vextq_u32(__a, __b, __n) __extension__ ({ \
    uint32x4_t __r; \
    unsigned int __tmp[8]; \
    __tmp[0] = (__a)[0]; __tmp[1] = (__a)[1]; \
    __tmp[2] = (__a)[2]; __tmp[3] = (__a)[3]; \
    __tmp[4] = (__b)[0]; __tmp[5] = (__b)[1]; \
    __tmp[6] = (__b)[2]; __tmp[7] = (__b)[3]; \
    __r[0] = __tmp[(__n)]; __r[1] = __tmp[(__n) + 1]; \
    __r[2] = __tmp[(__n) + 2]; __r[3] = __tmp[(__n) + 3]; \
    __r; \
})

/* === Narrowing === */

/* vmovn_u64: narrow uint64x2_t to uint32x2_t (take low 32 bits of each lane) */
static __inline__ uint32x2_t __attribute__((__always_inline__))
vmovn_u64(uint64x2_t __a)
{
    uint32x2_t __ret;
    __ret[0] = (unsigned int)__a[0];
    __ret[1] = (unsigned int)__a[1];
    return __ret;
}

/* vmovn_u32: narrow uint32x4_t to uint16x4_t (take low 16 bits of each lane) */
static __inline__ uint16x4_t __attribute__((__always_inline__))
vmovn_u32(uint32x4_t __a)
{
    uint16x4_t __ret;
    __ret[0] = (unsigned short)__a[0];
    __ret[1] = (unsigned short)__a[1];
    __ret[2] = (unsigned short)__a[2];
    __ret[3] = (unsigned short)__a[3];
    return __ret;
}

/* vshrn_n_u64: shift right and narrow uint64x2_t to uint32x2_t */
#define vshrn_n_u64(__a, __n) __extension__ ({ \
    uint32x2_t __r; \
    __r[0] = (unsigned int)((__a)[0] >> (__n)); \
    __r[1] = (unsigned int)((__a)[1] >> (__n)); \
    __r; \
})

/* vshrn_n_u32: shift right and narrow uint32x4_t to uint16x4_t */
#define vshrn_n_u32(__a, __n) __extension__ ({ \
    uint16x4_t __r; \
    __r[0] = (unsigned short)((__a)[0] >> (__n)); \
    __r[1] = (unsigned short)((__a)[1] >> (__n)); \
    __r[2] = (unsigned short)((__a)[2] >> (__n)); \
    __r[3] = (unsigned short)((__a)[3] >> (__n)); \
    __r; \
})

/* === Widening multiply-accumulate === */

/* vmlal_u32: widening multiply-accumulate u32 -> u64
 * result[i] = acc[i] + (u64)a[i] * (u64)b[i] */
static __inline__ uint64x2_t __attribute__((__always_inline__))
vmlal_u32(uint64x2_t __acc, uint32x2_t __a, uint32x2_t __b)
{
    uint64x2_t __ret;
    __ret[0] = __acc[0] + (unsigned long long)__a[0] * (unsigned long long)__b[0];
    __ret[1] = __acc[1] + (unsigned long long)__a[1] * (unsigned long long)__b[1];
    return __ret;
}

/* vmlal_high_u32: widening multiply-accumulate of high halves */
static __inline__ uint64x2_t __attribute__((__always_inline__))
vmlal_high_u32(uint64x2_t __acc, uint32x4_t __a, uint32x4_t __b)
{
    uint64x2_t __ret;
    __ret[0] = __acc[0] + (unsigned long long)__a[2] * (unsigned long long)__b[2];
    __ret[1] = __acc[1] + (unsigned long long)__a[3] * (unsigned long long)__b[3];
    return __ret;
}

/* vmlal_low_u32: widening multiply-accumulate of low halves */
static __inline__ uint64x2_t __attribute__((__always_inline__))
vmlal_low_u32(uint64x2_t __acc, uint32x4_t __a, uint32x4_t __b)
{
    uint64x2_t __ret;
    __ret[0] = __acc[0] + (unsigned long long)__a[0] * (unsigned long long)__b[0];
    __ret[1] = __acc[1] + (unsigned long long)__a[1] * (unsigned long long)__b[1];
    return __ret;
}

/* === Unzip / De-interleave === */

/* vuzpq_u32: unzip (de-interleave) two uint32x4_t vectors
 * result.val[0] = {a[0], a[2], b[0], b[2]}  (even elements)
 * result.val[1] = {a[1], a[3], b[1], b[3]}  (odd elements) */
static __inline__ uint32x4x2_t __attribute__((__always_inline__))
vuzpq_u32(uint32x4_t __a, uint32x4_t __b)
{
    uint32x4x2_t __ret;
    __ret.val[0][0] = __a[0];
    __ret.val[0][1] = __a[2];
    __ret.val[0][2] = __b[0];
    __ret.val[0][3] = __b[2];
    __ret.val[1][0] = __a[1];
    __ret.val[1][1] = __a[3];
    __ret.val[1][2] = __b[1];
    __ret.val[1][3] = __b[3];
    return __ret;
}

/* === Reinterpret casts: u32 <-> u64 === */

/* === SHA-256 crypto intrinsics (software implementation) === */
/* Used by mbedtls SHA-256 hardware acceleration. */

/* vsha256su0q_u32: SHA-256 schedule update 0 */
static __inline__ uint32x4_t __attribute__((__always_inline__))
vsha256su0q_u32(uint32x4_t __w0_3, uint32x4_t __w4_7)
{
    uint32x4_t __ret;
    for (int __i = 0; __i < 4; __i++) {
        unsigned int __w = (__i < 3) ? __w0_3[__i + 1] : __w4_7[0];
        /* sigma0: ROTR(7) ^ ROTR(18) ^ SHR(3) */
        unsigned int __s0 = ((__w >> 7) | (__w << 25)) ^ ((__w >> 18) | (__w << 14)) ^ (__w >> 3);
        __ret[__i] = __w0_3[__i] + __s0;
    }
    return __ret;
}

/* vsha256su1q_u32: SHA-256 schedule update 1. The upper two lanes use the
 * lower two results. */
static __inline__ uint32x4_t __attribute__((__always_inline__))
vsha256su1q_u32(uint32x4_t __tw0_3, uint32x4_t __w8_11, uint32x4_t __w12_15)
{
    uint32x4_t __ret;
    unsigned int __t0[4] = { __w8_11[1], __w8_11[2], __w8_11[3], __w12_15[0] };
    for (int __i = 0; __i < 4; __i++) {
        unsigned int __w = __i < 2 ? __w12_15[__i + 2] : __ret[__i - 2];
        /* sigma1: ROTR(17) ^ ROTR(19) ^ SHR(10) */
        unsigned int __s1 = ((__w >> 17) | (__w << 15)) ^ ((__w >> 19) | (__w << 13)) ^ (__w >> 10);
        __ret[__i] = __s1 + __tw0_3[__i] + __t0[__i];
    }
    return __ret;
}

/* vsha256hq_u32: SHA-256 hash update (part 1) */
static __inline__ uint32x4_t __attribute__((__always_inline__))
vsha256hq_u32(uint32x4_t __hash_abcd, uint32x4_t __hash_efgh, uint32x4_t __wk)
{
    unsigned int __a = __hash_abcd[0], __b = __hash_abcd[1];
    unsigned int __c = __hash_abcd[2], __d = __hash_abcd[3];
    unsigned int __e = __hash_efgh[0], __f = __hash_efgh[1];
    unsigned int __g = __hash_efgh[2], __h = __hash_efgh[3];
    for (int __i = 0; __i < 4; __i++) {
        unsigned int __S1 = ((__e >> 6) | (__e << 26)) ^ ((__e >> 11) | (__e << 21)) ^ ((__e >> 25) | (__e << 7));
        unsigned int __ch = (__e & __f) ^ (~__e & __g);
        unsigned int __temp1 = __h + __S1 + __ch + __wk[__i];
        unsigned int __S0 = ((__a >> 2) | (__a << 30)) ^ ((__a >> 13) | (__a << 19)) ^ ((__a >> 22) | (__a << 10));
        unsigned int __maj = (__a & __b) ^ (__a & __c) ^ (__b & __c);
        unsigned int __temp2 = __S0 + __maj;
        __h = __g; __g = __f; __f = __e; __e = __d + __temp1;
        __d = __c; __c = __b; __b = __a; __a = __temp1 + __temp2;
    }
    uint32x4_t __ret;
    __ret[0] = __a; __ret[1] = __b;
    __ret[2] = __c; __ret[3] = __d;
    return __ret;
}

/* vsha256h2q_u32: SHA-256 hash update (part 2) */
static __inline__ uint32x4_t __attribute__((__always_inline__))
vsha256h2q_u32(uint32x4_t __hash_efgh, uint32x4_t __hash_abcd, uint32x4_t __wk)
{
    unsigned int __a = __hash_abcd[0], __b = __hash_abcd[1];
    unsigned int __c = __hash_abcd[2], __d = __hash_abcd[3];
    unsigned int __e = __hash_efgh[0], __f = __hash_efgh[1];
    unsigned int __g = __hash_efgh[2], __h = __hash_efgh[3];
    for (int __i = 0; __i < 4; __i++) {
        unsigned int __S1 = ((__e >> 6) | (__e << 26)) ^ ((__e >> 11) | (__e << 21)) ^ ((__e >> 25) | (__e << 7));
        unsigned int __ch = (__e & __f) ^ (~__e & __g);
        unsigned int __temp1 = __h + __S1 + __ch + __wk[__i];
        unsigned int __S0 = ((__a >> 2) | (__a << 30)) ^ ((__a >> 13) | (__a << 19)) ^ ((__a >> 22) | (__a << 10));
        unsigned int __maj = (__a & __b) ^ (__a & __c) ^ (__b & __c);
        unsigned int __temp2 = __S0 + __maj;
        __h = __g; __g = __f; __f = __e; __e = __d + __temp1;
        __d = __c; __c = __b; __b = __a; __a = __temp1 + __temp2;
    }
    uint32x4_t __ret;
    __ret[0] = __e; __ret[1] = __f;
    __ret[2] = __g; __ret[3] = __h;
    return __ret;
}

/* === Load/Store u64 (64-bit / D-register) === */

/* === Load/Store u32 (64-bit / D-register) === */

/* === Store 8x8 (64-bit / D-register) === */

/* === Reinterpret casts === */

/* === Get lane === */

/* === Narrowing shifts === */

/* vshrn_n_u16: shift right narrow (u16 -> u8) */
static __inline__ uint8x8_t __attribute__((__always_inline__))
vshrn_n_u16(uint16x8_t __a, int __n)
{
    uint8x8_t __ret;
    for (int __i = 0; __i < 8; __i++) {
        __ret[__i] = (unsigned char)(__a[__i] >> __n);
    }
    return __ret;
}

/* === Shift and insert === */

/* vsli_n_u8: shift left and insert (64-bit, per-byte) */
static __inline__ uint8x8_t __attribute__((__always_inline__))
vsli_n_u8(uint8x8_t __a, uint8x8_t __b, int __n)
{
    uint8x8_t __ret;
    unsigned char __mask = (unsigned char)((0xFF << __n) & 0xFF);
    for (int __i = 0; __i < 8; __i++) {
        __ret[__i] = (__a[__i] & ~__mask) | ((unsigned char)(__b[__i] << __n) & __mask);
    }
    return __ret;
}

/* vsriq_n_u8: shift right and insert (128-bit, per-byte) */
static __inline__ uint8x16_t __attribute__((__always_inline__))
vsriq_n_u8(uint8x16_t __a, uint8x16_t __b, int __n)
{
    uint8x16_t __ret;
    unsigned char __mask = (unsigned char)(0xFF >> __n);
    for (int __i = 0; __i < 16; __i++) {
        __ret[__i] = (__a[__i] & ~__mask) | ((__b[__i] >> __n) & __mask);
    }
    return __ret;
}

/* === Multi-element structure loads === */

/* vld2q_u16: load 2-element interleaved u16 (128-bit x 2) */
static __inline__ uint16x8x2_t __attribute__((__always_inline__))
vld2q_u16(unsigned short const *__p)
{
    uint16x8x2_t __ret;
    for (int __i = 0; __i < 8; __i++) {
        __ret.val[0][__i] = __p[__i * 2];
        __ret.val[1][__i] = __p[__i * 2 + 1];
    }
    return __ret;
}

/* vld4q_u8: load 4-element interleaved u8 (128-bit x 4) */
static __inline__ uint8x16x4_t __attribute__((__always_inline__))
vld4q_u8(unsigned char const *__p)
{
    uint8x16x4_t __ret;
    for (int __i = 0; __i < 16; __i++) {
        __ret.val[0][__i] = __p[__i * 4];
        __ret.val[1][__i] = __p[__i * 4 + 1];
        __ret.val[2][__i] = __p[__i * 4 + 2];
        __ret.val[3][__i] = __p[__i * 4 + 3];
    }
    return __ret;
}

/* ================================================================== */
/*          REINTERPRET CASTS: signed <-> unsigned                     */
/* ================================================================== */

/* ================================================================== */
/*          DUPLICATE (BROADCAST): signed types                        */
/* ================================================================== */

/* ================================================================== */
/*           HORIZONTAL REDUCTION OPERATIONS                           */
/* ================================================================== */

/* vmaxvq_u8: horizontal max of all uint8x16_t lanes -> scalar uint8_t */
static __inline__ unsigned char __attribute__((__always_inline__))
vmaxvq_u8(uint8x16_t __a)
{
    unsigned char __ret = __a[0];
    for (int __i = 1; __i < 16; __i++)
        if (__a[__i] > __ret) __ret = __a[__i];
    return __ret;
}

/* vminvq_u8: horizontal min of all uint8x16_t lanes -> scalar uint8_t */
static __inline__ unsigned char __attribute__((__always_inline__))
vminvq_u8(uint8x16_t __a)
{
    unsigned char __ret = __a[0];
    for (int __i = 1; __i < 16; __i++)
        if (__a[__i] < __ret) __ret = __a[__i];
    return __ret;
}

/* vmaxvq_u32: horizontal max of all uint32x4_t lanes -> scalar uint32_t */
static __inline__ unsigned int __attribute__((__always_inline__))
vmaxvq_u32(uint32x4_t __a)
{
    unsigned int __ret = __a[0];
    for (int __i = 1; __i < 4; __i++)
        if (__a[__i] > __ret) __ret = __a[__i];
    return __ret;
}

/* vmaxvq_s16: horizontal max across int16x8_t */
static __inline__ short __attribute__((__always_inline__))
vmaxvq_s16(int16x8_t __a)
{
    short __max = __a[0];
    for (int __i = 1; __i < 8; __i++) {
        if (__a[__i] > __max)
            __max = __a[__i];
    }
    return __max;
}

/* vmaxvq_u16: horizontal max across uint16x8_t */
static __inline__ unsigned short __attribute__((__always_inline__))
vmaxvq_u16(uint16x8_t __a)
{
    unsigned short __max = __a[0];
    for (int __i = 1; __i < 8; __i++) {
        if (__a[__i] > __max)
            __max = __a[__i];
    }
    return __max;
}

/* ================================================================== */
/*           SATURATING ARITHMETIC                                     */
/* ================================================================== */

/* vqsubq_u8: saturating subtract uint8x16_t (clamp at 0) */
static __inline__ uint8x16_t __attribute__((__always_inline__))
vqsubq_u8(uint8x16_t __a, uint8x16_t __b)
{
    uint8x16_t __ret;
    for (int __i = 0; __i < 16; __i++)
        __ret[__i] = __a[__i] > __b[__i] ? __a[__i] - __b[__i] : 0;
    return __ret;
}

/* vqaddq_u8: saturating add uint8x16_t (clamp at 255) */
static __inline__ uint8x16_t __attribute__((__always_inline__))
vqaddq_u8(uint8x16_t __a, uint8x16_t __b)
{
    uint8x16_t __ret;
    for (int __i = 0; __i < 16; __i++) {
        unsigned int __sum = (unsigned int)__a[__i] + (unsigned int)__b[__i];
        __ret[__i] = __sum > 255 ? 255 : (unsigned char)__sum;
    }
    return __ret;
}

/* ================================================================== */
/*           ADDITIONAL COMPARISON OPERATIONS                          */
/* ================================================================== */

/* vqaddq_s16: saturating add int16x8_t */
static __inline__ int16x8_t __attribute__((__always_inline__))
vqaddq_s16(int16x8_t __a, int16x8_t __b)
{
    int16x8_t __ret;
    for (int __i = 0; __i < 8; __i++) {
        int __sum = (int)__a[__i] + (int)__b[__i];
        if (__sum > 32767) __sum = 32767;
        else if (__sum < -32768) __sum = -32768;
        __ret[__i] = (short)__sum;
    }
    return __ret;
}

/* vqsubq_u16: saturating subtract uint16x8_t */
static __inline__ uint16x8_t __attribute__((__always_inline__))
vqsubq_u16(uint16x8_t __a, uint16x8_t __b)
{
    uint16x8_t __ret;
    for (int __i = 0; __i < 8; __i++) {
        int __diff = (int)__a[__i] - (int)__b[__i];
        __ret[__i] = __diff < 0 ? 0 : (unsigned short)__diff;
    }
    return __ret;
}

/* vmaxq_s16: element-wise maximum of int16x8_t */
static __inline__ int16x8_t __attribute__((__always_inline__))
vmaxq_s16(int16x8_t __a, int16x8_t __b)
{
    int16x8_t __ret;
    for (int __i = 0; __i < 8; __i++)
        __ret[__i] = __a[__i] > __b[__i] ? __a[__i] : __b[__i];
    return __ret;
}

/* ===== Float32 NEON intrinsics ===== */

/* vget_low_f32: get low 64-bit half of float32x4_t as float32x2_t */
static __inline__ float32x2_t __attribute__((__always_inline__))
vget_low_f32(float32x4_t __a)
{
    float32x2_t __ret;
    __ret[0] = __a[0];
    __ret[1] = __a[1];
    return __ret;
}

/* vget_high_f32: get high 64-bit half of float32x4_t as float32x2_t */
static __inline__ float32x2_t __attribute__((__always_inline__))
vget_high_f32(float32x4_t __a)
{
    float32x2_t __ret;
    __ret[0] = __a[2];
    __ret[1] = __a[3];
    return __ret;
}

/* vcombine_f32: combine two float32x2_t into float32x4_t */
static __inline__ float32x4_t __attribute__((__always_inline__))
vcombine_f32(float32x2_t __lo, float32x2_t __hi)
{
    float32x4_t __ret;
    __ret[0] = __lo[0];
    __ret[1] = __lo[1];
    __ret[2] = __hi[0];
    __ret[3] = __hi[1];
    return __ret;
}

/* vrev64q_f32: reverse floats within each 64-bit lane */
static __inline__ float32x4_t __attribute__((__always_inline__))
vrev64q_f32(float32x4_t __a)
{
    float32x4_t __ret;
    __ret[0] = __a[1];
    __ret[1] = __a[0];
    __ret[2] = __a[3];
    __ret[3] = __a[2];
    return __ret;
}

/* vcvtq_s32_f32: convert float32x4_t to int32x4_t (round toward zero) */
static __inline__ int32x4_t __attribute__((__always_inline__))
vcvtq_s32_f32(float32x4_t __a)
{
    int32x4_t __ret;
    __ret[0] = (int)__a[0];
    __ret[1] = (int)__a[1];
    __ret[2] = (int)__a[2];
    __ret[3] = (int)__a[3];
    return __ret;
}

/* vqaddq_s32: saturating add int32x4_t */
static __inline__ int32x4_t __attribute__((__always_inline__))
vqaddq_s32(int32x4_t __a, int32x4_t __b)
{
    int32x4_t __ret;
    for (int __i = 0; __i < 4; __i++) {
        long long __sum = (long long)__a[__i] + (long long)__b[__i];
        if (__sum > 2147483647LL) __sum = 2147483647LL;
        if (__sum < -2147483648LL) __sum = -2147483648LL;
        __ret[__i] = (int)__sum;
    }
    return __ret;
}

/* vqmovn_s32: saturating narrow int32x4_t to int16x4_t */
static __inline__ int16x4_t __attribute__((__always_inline__))
vqmovn_s32(int32x4_t __a)
{
    int16x4_t __ret;
    for (int __i = 0; __i < 4; __i++) {
        int __v = __a[__i];
        if (__v > 32767) __v = 32767;
        if (__v < -32768) __v = -32768;
        __ret[__i] = (short)__v;
    }
    return __ret;
}

/* vst1_lane_s16: store one lane from int16x4_t */
static __inline__ void __attribute__((__always_inline__))
vst1_lane_s16(short *__p, int16x4_t __a, int __lane)
{
    *__p = __a[__lane];
}

/* ================================================================== */
/*              u16 ARITHMETIC / COMPARE / BITWISE                    */
/* ================================================================== */

/* vmaxq_u16: element-wise unsigned max uint16x8_t */
static __inline__ uint16x8_t __attribute__((__always_inline__))
vmaxq_u16(uint16x8_t __a, uint16x8_t __b)
{
    uint16x8_t __ret;
    for (int __i = 0; __i < 8; __i++)
        __ret[__i] = __a[__i] > __b[__i] ? __a[__i] : __b[__i];
    return __ret;
}

/* vminq_u16: element-wise unsigned min uint16x8_t */
static __inline__ uint16x8_t __attribute__((__always_inline__))
vminq_u16(uint16x8_t __a, uint16x8_t __b)
{
    uint16x8_t __ret;
    for (int __i = 0; __i < 8; __i++)
        __ret[__i] = __a[__i] < __b[__i] ? __a[__i] : __b[__i];
    return __ret;
}

/* vminq_s16: element-wise signed min int16x8_t */
static __inline__ int16x8_t __attribute__((__always_inline__))
vminq_s16(int16x8_t __a, int16x8_t __b)
{
    int16x8_t __ret;
    for (int __i = 0; __i < 8; __i++)
        __ret[__i] = __a[__i] < __b[__i] ? __a[__i] : __b[__i];
    return __ret;
}

/* ================================================================== */
/*              u16 COMPARE INTRINSICS                                */
/* ================================================================== */

/* vcgtzq_s16: compare greater than zero, int16x8_t -> uint16x8_t */
static __inline__ uint16x8_t __attribute__((__always_inline__))
vcgtzq_s16(int16x8_t __a)
{
    uint16x8_t __ret;
    for (int __i = 0; __i < 8; __i++)
        __ret[__i] = (__a[__i] > 0) ? 0xFFFF : 0x0000;
    return __ret;
}

/* vcgtzq_s32: compare greater than zero, int32x4_t -> uint32x4_t */
static __inline__ uint32x4_t __attribute__((__always_inline__))
vcgtzq_s32(int32x4_t __a)
{
    uint32x4_t __ret;
    for (int __i = 0; __i < 4; __i++)
        __ret[__i] = (__a[__i] > 0) ? 0xFFFFFFFF : 0x00000000;
    return __ret;
}

/* vcltzq_s16: compare less than zero, int16x8_t -> uint16x8_t */
static __inline__ uint16x8_t __attribute__((__always_inline__))
vcltzq_s16(int16x8_t __a)
{
    uint16x8_t __ret;
    for (int __i = 0; __i < 8; __i++)
        __ret[__i] = (__a[__i] < 0) ? 0xFFFF : 0x0000;
    return __ret;
}

/* ================================================================== */
/*              BITWISE SELECT (vbslq)                                */
/* ================================================================== */

/* ================================================================== */
/*              EXTRACT (vextq_u16)                                   */
/* ================================================================== */

/* vextq_u16: extract from pair of uint16x8_t */
#define vextq_u16(__a, __b, __n) __extension__ ({ \
    uint16x8_t __r; \
    unsigned short __tmp[16]; \
    __tmp[0] = (__a)[0]; __tmp[1] = (__a)[1]; \
    __tmp[2] = (__a)[2]; __tmp[3] = (__a)[3]; \
    __tmp[4] = (__a)[4]; __tmp[5] = (__a)[5]; \
    __tmp[6] = (__a)[6]; __tmp[7] = (__a)[7]; \
    __tmp[8] = (__b)[0]; __tmp[9] = (__b)[1]; \
    __tmp[10] = (__b)[2]; __tmp[11] = (__b)[3]; \
    __tmp[12] = (__b)[4]; __tmp[13] = (__b)[5]; \
    __tmp[14] = (__b)[6]; __tmp[15] = (__b)[7]; \
    __r[0] = __tmp[(__n)]; __r[1] = __tmp[(__n) + 1]; \
    __r[2] = __tmp[(__n) + 2]; __r[3] = __tmp[(__n) + 3]; \
    __r[4] = __tmp[(__n) + 4]; __r[5] = __tmp[(__n) + 5]; \
    __r[6] = __tmp[(__n) + 6]; __r[7] = __tmp[(__n) + 7]; \
    __r; \
})

/* vextq_s16: extract from pair of int16x8_t */
#define vextq_s16(__a, __b, __n) __extension__ ({ \
    int16x8_t __r; \
    short __tmp[16]; \
    __tmp[0] = (__a)[0]; __tmp[1] = (__a)[1]; \
    __tmp[2] = (__a)[2]; __tmp[3] = (__a)[3]; \
    __tmp[4] = (__a)[4]; __tmp[5] = (__a)[5]; \
    __tmp[6] = (__a)[6]; __tmp[7] = (__a)[7]; \
    __tmp[8] = (__b)[0]; __tmp[9] = (__b)[1]; \
    __tmp[10] = (__b)[2]; __tmp[11] = (__b)[3]; \
    __tmp[12] = (__b)[4]; __tmp[13] = (__b)[5]; \
    __tmp[14] = (__b)[6]; __tmp[15] = (__b)[7]; \
    __r[0] = __tmp[(__n)]; __r[1] = __tmp[(__n) + 1]; \
    __r[2] = __tmp[(__n) + 2]; __r[3] = __tmp[(__n) + 3]; \
    __r[4] = __tmp[(__n) + 4]; __r[5] = __tmp[(__n) + 5]; \
    __r[6] = __tmp[(__n) + 6]; __r[7] = __tmp[(__n) + 7]; \
    __r; \
})

/* ================================================================== */
/*              TABLE LOOKUP (vqtbl2q)                                */
/* ================================================================== */

/* vqtbl2q_u8: table lookup across 2 registers (32 bytes) */
static __inline__ uint8x16_t __attribute__((__always_inline__))
vqtbl2q_u8(uint8x16x2_t __a, uint8x16_t __b)
{
    uint8x16_t __ret;
    for (int __i = 0; __i < 16; __i++) {
        unsigned char __idx = __b[__i];
        if (__idx < 16)
            __ret[__i] = __a.val[0][__idx];
        else if (__idx < 32)
            __ret[__i] = __a.val[1][__idx - 16];
        else
            __ret[__i] = 0;
    }
    return __ret;
}

/* vqtbx2q_u8: table lookup extend across 2 registers */
static __inline__ uint8x16_t __attribute__((__always_inline__))
vqtbx2q_u8(uint8x16_t __def, uint8x16x2_t __a, uint8x16_t __b)
{
    uint8x16_t __ret;
    for (int __i = 0; __i < 16; __i++) {
        unsigned char __idx = __b[__i];
        if (__idx < 16)
            __ret[__i] = __a.val[0][__idx];
        else if (__idx < 32)
            __ret[__i] = __a.val[1][__idx - 16];
        else
            __ret[__i] = __def[__i];
    }
    return __ret;
}

/* vqtbl3q_u8: table lookup across 3 registers (48 bytes) */
static __inline__ uint8x16_t __attribute__((__always_inline__))
vqtbl3q_u8(uint8x16x3_t __a, uint8x16_t __b)
{
    uint8x16_t __ret;
    for (int __i = 0; __i < 16; __i++) {
        unsigned char __idx = __b[__i];
        if (__idx < 16)
            __ret[__i] = __a.val[0][__idx];
        else if (__idx < 32)
            __ret[__i] = __a.val[1][__idx - 16];
        else if (__idx < 48)
            __ret[__i] = __a.val[2][__idx - 32];
        else
            __ret[__i] = 0;
    }
    return __ret;
}

/* ================================================================== */
/*              UNZIP / DE-INTERLEAVE (AArch64 vuzp1/vuzp2)          */
/* ================================================================== */

/* vuzp1q_u16: unzip even elements from two uint16x8_t
 * result = {a[0], a[2], a[4], a[6], b[0], b[2], b[4], b[6]} */
static __inline__ uint16x8_t __attribute__((__always_inline__))
vuzp1q_u16(uint16x8_t __a, uint16x8_t __b)
{
    uint16x8_t __ret;
    __ret[0] = __a[0];
    __ret[1] = __a[2];
    __ret[2] = __a[4];
    __ret[3] = __a[6];
    __ret[4] = __b[0];
    __ret[5] = __b[2];
    __ret[6] = __b[4];
    __ret[7] = __b[6];
    return __ret;
}

/* vuzp2q_u16: unzip odd elements from two uint16x8_t
 * result = {a[1], a[3], a[5], a[7], b[1], b[3], b[5], b[7]} */
static __inline__ uint16x8_t __attribute__((__always_inline__))
vuzp2q_u16(uint16x8_t __a, uint16x8_t __b)
{
    uint16x8_t __ret;
    __ret[0] = __a[1];
    __ret[1] = __a[3];
    __ret[2] = __a[5];
    __ret[3] = __a[7];
    __ret[4] = __b[1];
    __ret[5] = __b[3];
    __ret[6] = __b[5];
    __ret[7] = __b[7];
    return __ret;
}

/* vuzp1q_u8: unzip even elements from two uint8x16_t */
static __inline__ uint8x16_t __attribute__((__always_inline__))
vuzp1q_u8(uint8x16_t __a, uint8x16_t __b)
{
    uint8x16_t __ret;
    for (int __i = 0; __i < 8; __i++) {
        __ret[__i] = __a[__i * 2];
        __ret[__i + 8] = __b[__i * 2];
    }
    return __ret;
}

/* vuzp2q_u8: unzip odd elements from two uint8x16_t */
static __inline__ uint8x16_t __attribute__((__always_inline__))
vuzp2q_u8(uint8x16_t __a, uint8x16_t __b)
{
    uint8x16_t __ret;
    for (int __i = 0; __i < 8; __i++) {
        __ret[__i] = __a[__i * 2 + 1];
        __ret[__i + 8] = __b[__i * 2 + 1];
    }
    return __ret;
}

/* vuzp1q_u32: unzip even elements from two uint32x4_t */
static __inline__ uint32x4_t __attribute__((__always_inline__))
vuzp1q_u32(uint32x4_t __a, uint32x4_t __b)
{
    uint32x4_t __ret;
    __ret[0] = __a[0];
    __ret[1] = __a[2];
    __ret[2] = __b[0];
    __ret[3] = __b[2];
    return __ret;
}

/* vuzp2q_u32: unzip odd elements from two uint32x4_t */
static __inline__ uint32x4_t __attribute__((__always_inline__))
vuzp2q_u32(uint32x4_t __a, uint32x4_t __b)
{
    uint32x4_t __ret;
    __ret[0] = __a[1];
    __ret[1] = __a[3];
    __ret[2] = __b[1];
    __ret[3] = __b[3];
    return __ret;
}

/* vzip1q_u16: zip/interleave low halves of two uint16x8_t
 * result = {a[0], b[0], a[1], b[1], a[2], b[2], a[3], b[3]} */
static __inline__ uint16x8_t __attribute__((__always_inline__))
vzip1q_u16(uint16x8_t __a, uint16x8_t __b)
{
    uint16x8_t __ret;
    __ret[0] = __a[0]; __ret[1] = __b[0];
    __ret[2] = __a[1]; __ret[3] = __b[1];
    __ret[4] = __a[2]; __ret[5] = __b[2];
    __ret[6] = __a[3]; __ret[7] = __b[3];
    return __ret;
}

/* vzip2q_u16: zip/interleave high halves of two uint16x8_t
 * result = {a[4], b[4], a[5], b[5], a[6], b[6], a[7], b[7]} */
static __inline__ uint16x8_t __attribute__((__always_inline__))
vzip2q_u16(uint16x8_t __a, uint16x8_t __b)
{
    uint16x8_t __ret;
    __ret[0] = __a[4]; __ret[1] = __b[4];
    __ret[2] = __a[5]; __ret[3] = __b[5];
    __ret[4] = __a[6]; __ret[5] = __b[6];
    __ret[6] = __a[7]; __ret[7] = __b[7];
    return __ret;
}

/* vzip1q_u32: zip/interleave low halves of two uint32x4_t */
static __inline__ uint32x4_t __attribute__((__always_inline__))
vzip1q_u32(uint32x4_t __a, uint32x4_t __b)
{
    uint32x4_t __ret;
    __ret[0] = __a[0]; __ret[1] = __b[0];
    __ret[2] = __a[1]; __ret[3] = __b[1];
    return __ret;
}

/* vzip2q_u32: zip/interleave high halves of two uint32x4_t */
static __inline__ uint32x4_t __attribute__((__always_inline__))
vzip2q_u32(uint32x4_t __a, uint32x4_t __b)
{
    uint32x4_t __ret;
    __ret[0] = __a[2]; __ret[1] = __b[2];
    __ret[2] = __a[3]; __ret[3] = __b[3];
    return __ret;
}

/* ================================================================== */
/*              ADDITIONAL REINTERPRET CASTS                          */
/* ================================================================== */

/* ================================================================== */
/*              ADDITIONAL SHIFT INTRINSICS                           */
/* ================================================================== */

/* ================================================================== */
/*         ADDITIONAL WIDENING / NARROWING INTRINSICS                 */
/* ================================================================== */

/* vmovl_u16: widen uint16x4_t to uint32x4_t */
static __inline__ uint32x4_t __attribute__((__always_inline__))
vmovl_u16(uint16x4_t __a)
{
    uint32x4_t __ret;
    __ret[0] = __a[0];
    __ret[1] = __a[1];
    __ret[2] = __a[2];
    __ret[3] = __a[3];
    return __ret;
}

/* vmovl_s16: widen int16x4_t to int32x4_t */
static __inline__ int32x4_t __attribute__((__always_inline__))
vmovl_s16(int16x4_t __a)
{
    int32x4_t __ret;
    __ret[0] = __a[0];
    __ret[1] = __a[1];
    __ret[2] = __a[2];
    __ret[3] = __a[3];
    return __ret;
}

/* vmovl_high_u16: widen high half of uint16x8_t to uint32x4_t */
static __inline__ uint32x4_t __attribute__((__always_inline__))
vmovl_high_u16(uint16x8_t __a)
{
    uint32x4_t __ret;
    __ret[0] = __a[4];
    __ret[1] = __a[5];
    __ret[2] = __a[6];
    __ret[3] = __a[7];
    return __ret;
}

/* vmovl_high_u8: widen high half of uint8x16_t to uint16x8_t */
static __inline__ uint16x8_t __attribute__((__always_inline__))
vmovl_high_u8(uint8x16_t __a)
{
    uint16x8_t __ret;
    for (int __i = 0; __i < 8; __i++)
        __ret[__i] = __a[__i + 8];
    return __ret;
}

/* ================================================================== */
/*              LANE / HALF / COMBINE INTRINSICS (s16)                */
/* ================================================================== */

/* vget_low_u16: get low half of uint16x8_t as uint16x4_t */
static __inline__ uint16x4_t __attribute__((__always_inline__))
vget_low_u16(uint16x8_t __a)
{
    uint16x4_t __ret;
    __ret[0] = __a[0];
    __ret[1] = __a[1];
    __ret[2] = __a[2];
    __ret[3] = __a[3];
    return __ret;
}

/* vget_high_u16: get high half of uint16x8_t as uint16x4_t */
static __inline__ uint16x4_t __attribute__((__always_inline__))
vget_high_u16(uint16x8_t __a)
{
    uint16x4_t __ret;
    __ret[0] = __a[4];
    __ret[1] = __a[5];
    __ret[2] = __a[6];
    __ret[3] = __a[7];
    return __ret;
}


/* ===== Transposes ===== */

static __inline__ uint8x8_t __attribute__((__always_inline__))
vtrn1_u8(uint8x8_t __a, uint8x8_t __b)
{
    uint8x8_t __ret;
    for (int __i = 0; __i < 8; __i += 2) {
        __ret[__i] = __a[__i + 0];
        __ret[__i + 1] = __b[__i + 0];
    }
    return __ret;
}

static __inline__ uint8x8_t __attribute__((__always_inline__))
vtrn2_u8(uint8x8_t __a, uint8x8_t __b)
{
    uint8x8_t __ret;
    for (int __i = 0; __i < 8; __i += 2) {
        __ret[__i] = __a[__i + 1];
        __ret[__i + 1] = __b[__i + 1];
    }
    return __ret;
}

static __inline__ uint8x8x2_t __attribute__((__always_inline__))
vtrn_u8(uint8x8_t __a, uint8x8_t __b)
{
    uint8x8x2_t __ret;
    __ret.val[0] = vtrn1_u8(__a, __b);
    __ret.val[1] = vtrn2_u8(__a, __b);
    return __ret;
}

static __inline__ int8x8_t __attribute__((__always_inline__))
vtrn1_s8(int8x8_t __a, int8x8_t __b)
{
    int8x8_t __ret;
    for (int __i = 0; __i < 8; __i += 2) {
        __ret[__i] = __a[__i + 0];
        __ret[__i + 1] = __b[__i + 0];
    }
    return __ret;
}

static __inline__ int8x8_t __attribute__((__always_inline__))
vtrn2_s8(int8x8_t __a, int8x8_t __b)
{
    int8x8_t __ret;
    for (int __i = 0; __i < 8; __i += 2) {
        __ret[__i] = __a[__i + 1];
        __ret[__i + 1] = __b[__i + 1];
    }
    return __ret;
}

static __inline__ int8x8x2_t __attribute__((__always_inline__))
vtrn_s8(int8x8_t __a, int8x8_t __b)
{
    int8x8x2_t __ret;
    __ret.val[0] = vtrn1_s8(__a, __b);
    __ret.val[1] = vtrn2_s8(__a, __b);
    return __ret;
}

static __inline__ uint16x4_t __attribute__((__always_inline__))
vtrn1_u16(uint16x4_t __a, uint16x4_t __b)
{
    uint16x4_t __ret;
    for (int __i = 0; __i < 4; __i += 2) {
        __ret[__i] = __a[__i + 0];
        __ret[__i + 1] = __b[__i + 0];
    }
    return __ret;
}

static __inline__ uint16x4_t __attribute__((__always_inline__))
vtrn2_u16(uint16x4_t __a, uint16x4_t __b)
{
    uint16x4_t __ret;
    for (int __i = 0; __i < 4; __i += 2) {
        __ret[__i] = __a[__i + 1];
        __ret[__i + 1] = __b[__i + 1];
    }
    return __ret;
}

static __inline__ uint16x4x2_t __attribute__((__always_inline__))
vtrn_u16(uint16x4_t __a, uint16x4_t __b)
{
    uint16x4x2_t __ret;
    __ret.val[0] = vtrn1_u16(__a, __b);
    __ret.val[1] = vtrn2_u16(__a, __b);
    return __ret;
}

static __inline__ int16x4_t __attribute__((__always_inline__))
vtrn1_s16(int16x4_t __a, int16x4_t __b)
{
    int16x4_t __ret;
    for (int __i = 0; __i < 4; __i += 2) {
        __ret[__i] = __a[__i + 0];
        __ret[__i + 1] = __b[__i + 0];
    }
    return __ret;
}

static __inline__ int16x4_t __attribute__((__always_inline__))
vtrn2_s16(int16x4_t __a, int16x4_t __b)
{
    int16x4_t __ret;
    for (int __i = 0; __i < 4; __i += 2) {
        __ret[__i] = __a[__i + 1];
        __ret[__i + 1] = __b[__i + 1];
    }
    return __ret;
}

static __inline__ int16x4x2_t __attribute__((__always_inline__))
vtrn_s16(int16x4_t __a, int16x4_t __b)
{
    int16x4x2_t __ret;
    __ret.val[0] = vtrn1_s16(__a, __b);
    __ret.val[1] = vtrn2_s16(__a, __b);
    return __ret;
}

static __inline__ uint32x2_t __attribute__((__always_inline__))
vtrn1_u32(uint32x2_t __a, uint32x2_t __b)
{
    uint32x2_t __ret;
    for (int __i = 0; __i < 2; __i += 2) {
        __ret[__i] = __a[__i + 0];
        __ret[__i + 1] = __b[__i + 0];
    }
    return __ret;
}

static __inline__ uint32x2_t __attribute__((__always_inline__))
vtrn2_u32(uint32x2_t __a, uint32x2_t __b)
{
    uint32x2_t __ret;
    for (int __i = 0; __i < 2; __i += 2) {
        __ret[__i] = __a[__i + 1];
        __ret[__i + 1] = __b[__i + 1];
    }
    return __ret;
}

static __inline__ uint32x2x2_t __attribute__((__always_inline__))
vtrn_u32(uint32x2_t __a, uint32x2_t __b)
{
    uint32x2x2_t __ret;
    __ret.val[0] = vtrn1_u32(__a, __b);
    __ret.val[1] = vtrn2_u32(__a, __b);
    return __ret;
}

static __inline__ int32x2_t __attribute__((__always_inline__))
vtrn1_s32(int32x2_t __a, int32x2_t __b)
{
    int32x2_t __ret;
    for (int __i = 0; __i < 2; __i += 2) {
        __ret[__i] = __a[__i + 0];
        __ret[__i + 1] = __b[__i + 0];
    }
    return __ret;
}

static __inline__ int32x2_t __attribute__((__always_inline__))
vtrn2_s32(int32x2_t __a, int32x2_t __b)
{
    int32x2_t __ret;
    for (int __i = 0; __i < 2; __i += 2) {
        __ret[__i] = __a[__i + 1];
        __ret[__i + 1] = __b[__i + 1];
    }
    return __ret;
}

static __inline__ int32x2x2_t __attribute__((__always_inline__))
vtrn_s32(int32x2_t __a, int32x2_t __b)
{
    int32x2x2_t __ret;
    __ret.val[0] = vtrn1_s32(__a, __b);
    __ret.val[1] = vtrn2_s32(__a, __b);
    return __ret;
}

static __inline__ float32x2_t __attribute__((__always_inline__))
vtrn1_f32(float32x2_t __a, float32x2_t __b)
{
    float32x2_t __ret;
    for (int __i = 0; __i < 2; __i += 2) {
        __ret[__i] = __a[__i + 0];
        __ret[__i + 1] = __b[__i + 0];
    }
    return __ret;
}

static __inline__ float32x2_t __attribute__((__always_inline__))
vtrn2_f32(float32x2_t __a, float32x2_t __b)
{
    float32x2_t __ret;
    for (int __i = 0; __i < 2; __i += 2) {
        __ret[__i] = __a[__i + 1];
        __ret[__i + 1] = __b[__i + 1];
    }
    return __ret;
}

static __inline__ float32x2x2_t __attribute__((__always_inline__))
vtrn_f32(float32x2_t __a, float32x2_t __b)
{
    float32x2x2_t __ret;
    __ret.val[0] = vtrn1_f32(__a, __b);
    __ret.val[1] = vtrn2_f32(__a, __b);
    return __ret;
}

static __inline__ uint8x16_t __attribute__((__always_inline__))
vtrn1q_u8(uint8x16_t __a, uint8x16_t __b)
{
    uint8x16_t __ret;
    for (int __i = 0; __i < 16; __i += 2) {
        __ret[__i] = __a[__i + 0];
        __ret[__i + 1] = __b[__i + 0];
    }
    return __ret;
}

static __inline__ uint8x16_t __attribute__((__always_inline__))
vtrn2q_u8(uint8x16_t __a, uint8x16_t __b)
{
    uint8x16_t __ret;
    for (int __i = 0; __i < 16; __i += 2) {
        __ret[__i] = __a[__i + 1];
        __ret[__i + 1] = __b[__i + 1];
    }
    return __ret;
}

static __inline__ uint8x16x2_t __attribute__((__always_inline__))
vtrnq_u8(uint8x16_t __a, uint8x16_t __b)
{
    uint8x16x2_t __ret;
    __ret.val[0] = vtrn1q_u8(__a, __b);
    __ret.val[1] = vtrn2q_u8(__a, __b);
    return __ret;
}

static __inline__ int8x16_t __attribute__((__always_inline__))
vtrn1q_s8(int8x16_t __a, int8x16_t __b)
{
    int8x16_t __ret;
    for (int __i = 0; __i < 16; __i += 2) {
        __ret[__i] = __a[__i + 0];
        __ret[__i + 1] = __b[__i + 0];
    }
    return __ret;
}

static __inline__ int8x16_t __attribute__((__always_inline__))
vtrn2q_s8(int8x16_t __a, int8x16_t __b)
{
    int8x16_t __ret;
    for (int __i = 0; __i < 16; __i += 2) {
        __ret[__i] = __a[__i + 1];
        __ret[__i + 1] = __b[__i + 1];
    }
    return __ret;
}

static __inline__ int8x16x2_t __attribute__((__always_inline__))
vtrnq_s8(int8x16_t __a, int8x16_t __b)
{
    int8x16x2_t __ret;
    __ret.val[0] = vtrn1q_s8(__a, __b);
    __ret.val[1] = vtrn2q_s8(__a, __b);
    return __ret;
}

static __inline__ uint16x8_t __attribute__((__always_inline__))
vtrn1q_u16(uint16x8_t __a, uint16x8_t __b)
{
    uint16x8_t __ret;
    for (int __i = 0; __i < 8; __i += 2) {
        __ret[__i] = __a[__i + 0];
        __ret[__i + 1] = __b[__i + 0];
    }
    return __ret;
}

static __inline__ uint16x8_t __attribute__((__always_inline__))
vtrn2q_u16(uint16x8_t __a, uint16x8_t __b)
{
    uint16x8_t __ret;
    for (int __i = 0; __i < 8; __i += 2) {
        __ret[__i] = __a[__i + 1];
        __ret[__i + 1] = __b[__i + 1];
    }
    return __ret;
}

static __inline__ uint16x8x2_t __attribute__((__always_inline__))
vtrnq_u16(uint16x8_t __a, uint16x8_t __b)
{
    uint16x8x2_t __ret;
    __ret.val[0] = vtrn1q_u16(__a, __b);
    __ret.val[1] = vtrn2q_u16(__a, __b);
    return __ret;
}

static __inline__ int16x8_t __attribute__((__always_inline__))
vtrn1q_s16(int16x8_t __a, int16x8_t __b)
{
    int16x8_t __ret;
    for (int __i = 0; __i < 8; __i += 2) {
        __ret[__i] = __a[__i + 0];
        __ret[__i + 1] = __b[__i + 0];
    }
    return __ret;
}

static __inline__ int16x8_t __attribute__((__always_inline__))
vtrn2q_s16(int16x8_t __a, int16x8_t __b)
{
    int16x8_t __ret;
    for (int __i = 0; __i < 8; __i += 2) {
        __ret[__i] = __a[__i + 1];
        __ret[__i + 1] = __b[__i + 1];
    }
    return __ret;
}

static __inline__ int16x8x2_t __attribute__((__always_inline__))
vtrnq_s16(int16x8_t __a, int16x8_t __b)
{
    int16x8x2_t __ret;
    __ret.val[0] = vtrn1q_s16(__a, __b);
    __ret.val[1] = vtrn2q_s16(__a, __b);
    return __ret;
}

static __inline__ uint32x4_t __attribute__((__always_inline__))
vtrn1q_u32(uint32x4_t __a, uint32x4_t __b)
{
    uint32x4_t __ret;
    for (int __i = 0; __i < 4; __i += 2) {
        __ret[__i] = __a[__i + 0];
        __ret[__i + 1] = __b[__i + 0];
    }
    return __ret;
}

static __inline__ uint32x4_t __attribute__((__always_inline__))
vtrn2q_u32(uint32x4_t __a, uint32x4_t __b)
{
    uint32x4_t __ret;
    for (int __i = 0; __i < 4; __i += 2) {
        __ret[__i] = __a[__i + 1];
        __ret[__i + 1] = __b[__i + 1];
    }
    return __ret;
}

static __inline__ uint32x4x2_t __attribute__((__always_inline__))
vtrnq_u32(uint32x4_t __a, uint32x4_t __b)
{
    uint32x4x2_t __ret;
    __ret.val[0] = vtrn1q_u32(__a, __b);
    __ret.val[1] = vtrn2q_u32(__a, __b);
    return __ret;
}

static __inline__ int32x4_t __attribute__((__always_inline__))
vtrn1q_s32(int32x4_t __a, int32x4_t __b)
{
    int32x4_t __ret;
    for (int __i = 0; __i < 4; __i += 2) {
        __ret[__i] = __a[__i + 0];
        __ret[__i + 1] = __b[__i + 0];
    }
    return __ret;
}

static __inline__ int32x4_t __attribute__((__always_inline__))
vtrn2q_s32(int32x4_t __a, int32x4_t __b)
{
    int32x4_t __ret;
    for (int __i = 0; __i < 4; __i += 2) {
        __ret[__i] = __a[__i + 1];
        __ret[__i + 1] = __b[__i + 1];
    }
    return __ret;
}

static __inline__ int32x4x2_t __attribute__((__always_inline__))
vtrnq_s32(int32x4_t __a, int32x4_t __b)
{
    int32x4x2_t __ret;
    __ret.val[0] = vtrn1q_s32(__a, __b);
    __ret.val[1] = vtrn2q_s32(__a, __b);
    return __ret;
}

static __inline__ float32x4_t __attribute__((__always_inline__))
vtrn1q_f32(float32x4_t __a, float32x4_t __b)
{
    float32x4_t __ret;
    for (int __i = 0; __i < 4; __i += 2) {
        __ret[__i] = __a[__i + 0];
        __ret[__i + 1] = __b[__i + 0];
    }
    return __ret;
}

static __inline__ float32x4_t __attribute__((__always_inline__))
vtrn2q_f32(float32x4_t __a, float32x4_t __b)
{
    float32x4_t __ret;
    for (int __i = 0; __i < 4; __i += 2) {
        __ret[__i] = __a[__i + 1];
        __ret[__i + 1] = __b[__i + 1];
    }
    return __ret;
}

static __inline__ float32x4x2_t __attribute__((__always_inline__))
vtrnq_f32(float32x4_t __a, float32x4_t __b)
{
    float32x4x2_t __ret;
    __ret.val[0] = vtrn1q_f32(__a, __b);
    __ret.val[1] = vtrn2q_f32(__a, __b);
    return __ret;
}

#endif /* _ARM_NEON_H_INCLUDED */
