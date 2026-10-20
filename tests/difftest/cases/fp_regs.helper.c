/* Built by the reference compiler. Checks that a callback preserves the
 * callee-saved FP registers d8-d15, as AAPCS64 requires. */
#include <stdint.h>

#if defined(__aarch64__)
int h_fp_callee_saved_ok(void (*fn)(void))
{
    uint64_t out[8];
    register void (*f)(void) __asm__("x19") = fn;
    register uint64_t *o __asm__("x20") = out;
    __asm__ volatile(
        "fmov d8, #1.0\n\t"  "fmov d9, #2.0\n\t"  "fmov d10, #3.0\n\t" "fmov d11, #4.0\n\t"
        "fmov d12, #5.0\n\t" "fmov d13, #6.0\n\t" "fmov d14, #7.0\n\t" "fmov d15, #8.0\n\t"
        "blr %[f]\n\t"
        "stp d8, d9, [%[o]]\n\t"        "stp d10, d11, [%[o], #16]\n\t"
        "stp d12, d13, [%[o], #32]\n\t" "stp d14, d15, [%[o], #48]\n\t"
        : : [f] "r"(f), [o] "r"(o)
        : "memory", "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7", "x8", "x9", "x10",
          "x11", "x12", "x13", "x14", "x15", "x16", "x17", "x30",
          "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7",
          "d8", "d9", "d10", "d11", "d12", "d13", "d14", "d15",
          "v16", "v17", "v18", "v19", "v20", "v21", "v22", "v23",
          "v24", "v25", "v26", "v27", "v28", "v29", "v30", "v31");
    int ok = 0;
    for (int i = 0; i < 8; i++) {
        union { uint64_t u; double d; } v = { out[i] };
        ok += v.d == (double)(i + 1);
    }
    return ok;
}
#else
int h_fp_callee_saved_ok(void (*fn)(void)) { fn(); return 8; }
#endif

/* Clobbers every caller-saved FP register a ccc caller might hold values in. */
double h_clobber_fp(double x)
{
    volatile double t[24];
    for (int i = 0; i < 24; i++)
        t[i] = x * i;
    double s = 0;
    for (int i = 0; i < 24; i++)
        s += t[i] * t[(i * 7) % 24];
    return s;
}
