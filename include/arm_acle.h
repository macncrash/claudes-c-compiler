/* ccc's arm_acle.h: the scalar ACLE intrinsics for AArch64 (ACLE 2.0
 * section 8 and 9): CRC32, bit and byte manipulation, barriers, hints and
 * system register access. Each one is the single instruction ACLE names,
 * in inline asm, so it costs what it costs under clang. */
#ifndef __ARM_ACLE_H
#define __ARM_ACLE_H

#if !defined(__aarch64__)
#error "arm_acle.h: ccc supports these intrinsics on AArch64 only"
#endif

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define __CCC_ACLE static __inline__ __attribute__((__always_inline__, __unused__))

/* Barriers (8.3). The argument is the 4-bit option, a constant
 * expression: 15 is SY, 11 ISH, 10 ISHST, 9 ISHLD. */
#define __dmb(__opt) __asm__ __volatile__("dmb #%0" : : "i"(__opt) : "memory")
#define __dsb(__opt) __asm__ __volatile__("dsb #%0" : : "i"(__opt) : "memory")
#define __isb(__opt) __asm__ __volatile__("isb #%0" : : "i"(__opt) : "memory")

/* Hints (8.4). */
__CCC_ACLE void __wfi(void) { __asm__ __volatile__("wfi"); }
__CCC_ACLE void __wfe(void) { __asm__ __volatile__("wfe"); }
__CCC_ACLE void __sev(void) { __asm__ __volatile__("sev"); }
__CCC_ACLE void __sevl(void) { __asm__ __volatile__("sevl"); }
__CCC_ACLE void __yield(void) { __asm__ __volatile__("yield"); }
__CCC_ACLE void __nop(void) { __asm__ __volatile__("nop"); }

/* Data processing (9.2). */
__CCC_ACLE uint32_t __ror(uint32_t __x, uint32_t __y) {
    __y %= 32;
    return __y == 0 ? __x : (__x >> __y) | (__x << (32 - __y));
}
__CCC_ACLE uint64_t __rorll(uint64_t __x, uint32_t __y) {
    __y %= 64;
    return __y == 0 ? __x : (__x >> __y) | (__x << (64 - __y));
}
__CCC_ACLE unsigned long __rorl(unsigned long __x, uint32_t __y) { return __rorll(__x, __y); }

__CCC_ACLE unsigned int __clz(uint32_t __x) {
    unsigned int __r; __asm__("clz %w0, %w1" : "=r"(__r) : "r"(__x)); return __r;
}
__CCC_ACLE unsigned int __clzll(uint64_t __x) {
    uint64_t __r; __asm__("clz %x0, %x1" : "=r"(__r) : "r"(__x)); return (unsigned int)__r;
}
__CCC_ACLE unsigned int __clzl(unsigned long __x) { return __clzll(__x); }

__CCC_ACLE unsigned int __cls(uint32_t __x) {
    unsigned int __r; __asm__("cls %w0, %w1" : "=r"(__r) : "r"(__x)); return __r;
}
__CCC_ACLE unsigned int __clsll(uint64_t __x) {
    uint64_t __r; __asm__("cls %x0, %x1" : "=r"(__r) : "r"(__x)); return (unsigned int)__r;
}
__CCC_ACLE unsigned int __clsl(unsigned long __x) { return __clsll(__x); }

__CCC_ACLE uint32_t __rev(uint32_t __x) { return __builtin_bswap32(__x); }
__CCC_ACLE uint64_t __revll(uint64_t __x) { return __builtin_bswap64(__x); }
__CCC_ACLE unsigned long __revl(unsigned long __x) { return __builtin_bswap64(__x); }

__CCC_ACLE uint32_t __rev16(uint32_t __x) {
    uint32_t __r; __asm__("rev16 %w0, %w1" : "=r"(__r) : "r"(__x)); return __r;
}
__CCC_ACLE uint64_t __rev16ll(uint64_t __x) {
    uint64_t __r; __asm__("rev16 %x0, %x1" : "=r"(__r) : "r"(__x)); return __r;
}
__CCC_ACLE unsigned long __rev16l(unsigned long __x) { return __rev16ll(__x); }
__CCC_ACLE int16_t __revsh(int16_t __x) { return (int16_t)__builtin_bswap16((uint16_t)__x); }

__CCC_ACLE uint32_t __rbit(uint32_t __x) {
    uint32_t __r; __asm__("rbit %w0, %w1" : "=r"(__r) : "r"(__x)); return __r;
}
__CCC_ACLE uint64_t __rbitll(uint64_t __x) {
    uint64_t __r; __asm__("rbit %x0, %x1" : "=r"(__r) : "r"(__x)); return __r;
}
__CCC_ACLE unsigned long __rbitl(unsigned long __x) { return __rbitll(__x); }

/* CRC32 (9.7): CRC-32 (polynomial 0x04C11DB7) and CRC-32C (0x1EDC6F41),
 * bit-reflected, without the initial or final inversion. */
#define __CCC_CRC32(__name, __insn, __ty, __m)                                 \
    __CCC_ACLE uint32_t __name(uint32_t __a, __ty __b) {                       \
        uint32_t __r;                                                          \
        __asm__(__insn " %w0, %w1, %" __m "2" : "=r"(__r) : "r"(__a), "r"(__b)); \
        return __r;                                                            \
    }
__CCC_CRC32(__crc32b, "crc32b", uint8_t, "w")
__CCC_CRC32(__crc32h, "crc32h", uint16_t, "w")
__CCC_CRC32(__crc32w, "crc32w", uint32_t, "w")
__CCC_CRC32(__crc32d, "crc32x", uint64_t, "x")
__CCC_CRC32(__crc32cb, "crc32cb", uint8_t, "w")
__CCC_CRC32(__crc32ch, "crc32ch", uint16_t, "w")
__CCC_CRC32(__crc32cw, "crc32cw", uint32_t, "w")
__CCC_CRC32(__crc32cd, "crc32cx", uint64_t, "x")
#undef __CCC_CRC32

/* System registers (10.1). The name is a string literal such as
 * "cntvct_el0" or "tpidr_el0". */
#define __arm_rsr64(__reg) (__extension__({ uint64_t __v; __asm__ __volatile__("mrs %0, " __reg : "=r"(__v)); __v; }))
#define __arm_rsr(__reg) ((uint32_t)__arm_rsr64(__reg))
#define __arm_rsrp(__reg) ((void *)__arm_rsr64(__reg))
#define __arm_wsr64(__reg, __v) __asm__ __volatile__("msr " __reg ", %0" : : "r"((uint64_t)(__v)))
#define __arm_wsr(__reg, __v) __arm_wsr64(__reg, (uint32_t)(__v))
#define __arm_wsrp(__reg, __v) __arm_wsr64(__reg, (uintptr_t)(__v))

#undef __CCC_ACLE

#ifdef __cplusplus
}
#endif

#endif /* __ARM_ACLE_H */
