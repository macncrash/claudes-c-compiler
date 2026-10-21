/* GCC vector-extension benchmark that checks itself.
 *
 * Each kernel runs twice over the same data: once written with vector types
 * (__attribute__((vector_size))) and once as plain scalar loops. The results
 * must agree (exactly for integers, within a relative 1e-5 for floating
 * point, since compilers may fuse multiply-adds differently in the two
 * versions), so a miscompiled vector operation shows up as FAIL without a
 * reference compiler. Then the vector version is timed.
 *
 *   ./vecbench          quick run (checks plus short timings)
 *   ./vecbench --long   five times as many timing repetitions (make bench)
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifndef COMPILER_NAME
#define COMPILER_NAME "unknown"
#endif

typedef float f4 __attribute__((vector_size(16)));
typedef double d2 __attribute__((vector_size(16)));
typedef int32_t i4 __attribute__((vector_size(16)));
typedef uint32_t u4 __attribute__((vector_size(16)));
typedef uint16_t u16x8 __attribute__((vector_size(16)));
typedef uint8_t u8x8 __attribute__((vector_size(8)));
typedef uint32_t u2 __attribute__((vector_size(8)));

#define N 4096 /* vectors per array */

static f4 fx[N], fy[N], fy_ref[N];
static u4 hs[N], hs_ref[N];
static f4 sel[N], sel_ref[N];
static d2 dv[N], dv_ref[N];
static u8x8 px_a[N], px_b[N], px_out[N], px_ref[N];
static u2 acc2[N], acc2_ref[N];

static int failures;

static double now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}

static int close_enough(double a, double b) {
    double scale = fabs(a) > fabs(b) ? fabs(a) : fabs(b);
    return fabs(a - b) <= 1e-5 * (scale > 1 ? scale : 1);
}

static void report(const char *name, int ok, uint64_t sum) {
    printf("check  %-10s %s  %016llx\n", name, ok ? "PASS" : "FAIL", (unsigned long long)sum);
    if (!ok)
        failures++;
}

static uint64_t hash_bytes(const void *p, size_t n) {
    const unsigned char *b = p;
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; i++)
        h = (h ^ b[i]) * 1099511628211ull;
    return h;
}

/* --- kernels: vector versions ------------------------------------------ */

static void saxpy(f4 a) {
    for (int i = 0; i < N; i++)
        fy[i] = a * fx[i] + fy[i] * 0.5f;
}

static void xorshift(void) {
    for (int i = 0; i < N; i++) {
        u4 h = hs[i];
        h ^= h << 13;
        h ^= h >> 17;
        h ^= h << 5;
        hs[i] = h * 2654435761u + (u4){ 1, 2, 3, 4 };
    }
}

/* max(x, y) per lane, then clamp to [-limit, limit], through lane masks. */
static void select_clamp(float limit) {
    for (int i = 0; i < N; i++) {
        f4 x = fx[i], y = fy[i];
        i4 m = x > y;
        f4 v = (f4)(((i4)x & m) | ((i4)y & ~m));
        i4 hi = v > limit, lo = v < -limit;
        f4 lim = (f4){ limit, limit, limit, limit };
        v = (f4)(((i4)v & ~(hi | lo)) | ((i4)lim & hi) | ((i4)(-lim) & lo));
        sel[i] = v;
    }
}

static void dmix(void) {
    for (int i = 0; i < N; i++)
        dv[i] = dv[i] * (d2){ 0.5, 1.5 } - dv[i] / 3.0 + 1.0;
}

/* Blend two rows of 8-bit pixels: (a * 3 + b) / 4, with 8-byte vectors. */
static void blend(void) {
    for (int i = 0; i < N; i++)
        px_out[i] = (px_a[i] >> 1) + (px_a[i] >> 2) + (px_b[i] >> 2);
}

/* Vector parameters and return values through a function that is not
 * inlined. */
__attribute__((noinline)) static u2 step2(u2 acc, u2 v, uint32_t k) {
    return acc * 3u + (v ^ k);
}

static void call_kernel(void) {
    for (int i = 0; i < N; i++)
        acc2[i] = step2(acc2[i], (u2){ (uint32_t)i, -(uint32_t)i }, i & 7);
}

/* --- kernels: scalar references ---------------------------------------- */

static void saxpy_ref(float a[4]) {
    for (int i = 0; i < N; i++)
        for (int k = 0; k < 4; k++)
            fy_ref[i][k] = a[k] * fx[i][k] + fy_ref[i][k] * 0.5f;
}

static void xorshift_ref(void) {
    for (int i = 0; i < N; i++)
        for (int k = 0; k < 4; k++) {
            uint32_t h = hs_ref[i][k];
            h ^= h << 13;
            h ^= h >> 17;
            h ^= h << 5;
            hs_ref[i][k] = h * 2654435761u + (uint32_t)(k + 1);
        }
}

static void select_clamp_ref(float limit) {
    for (int i = 0; i < N; i++)
        for (int k = 0; k < 4; k++) {
            float v = fx[i][k] > fy[i][k] ? fx[i][k] : fy[i][k];
            if (v > limit) v = limit;
            if (v < -limit) v = -limit;
            sel_ref[i][k] = v;
        }
}

static void dmix_ref(void) {
    static const double mul[2] = { 0.5, 1.5 };
    for (int i = 0; i < N; i++)
        for (int k = 0; k < 2; k++)
            dv_ref[i][k] = dv_ref[i][k] * mul[k] - dv_ref[i][k] / 3.0 + 1.0;
}

static void blend_ref(void) {
    for (int i = 0; i < N; i++)
        for (int k = 0; k < 8; k++) {
            uint8_t a = px_a[i][k], b = px_b[i][k];
            px_ref[i][k] = (uint8_t)((a >> 1) + (a >> 2) + (b >> 2));
        }
}

static void call_kernel_ref(void) {
    for (int i = 0; i < N; i++) {
        uint32_t k = i & 7;
        acc2_ref[i][0] = acc2_ref[i][0] * 3u + ((uint32_t)i ^ k);
        acc2_ref[i][1] = acc2_ref[i][1] * 3u + (-(uint32_t)i ^ k);
    }
}

/* --- driver -------------------------------------------------------------- */

static void init(void) {
    for (int i = 0; i < N; i++) {
        fx[i] = (f4){ i * 0.5f, i * 0.25f - 300, 1.0f, -i * 0.125f };
        fy[i] = fy_ref[i] = (f4){ 1.0f, 2.0f, (float)(i % 17), 4.0f - i };
        hs[i] = hs_ref[i] = (u4){ (uint32_t)i, i * 3u, i ^ 0x55u, ~(uint32_t)i };
        dv[i] = dv_ref[i] = (d2){ i * 0.5, 1.0 / (i + 1) };
        for (int k = 0; k < 8; k++) {
            px_a[i][k] = (uint8_t)(i * 7 + k * 31);
            px_b[i][k] = (uint8_t)(i * 13 + k * 5);
        }
        acc2[i] = acc2_ref[i] = (u2){ (uint32_t)i, 1 };
    }
}

static int float_arrays_match(const float *a, const float *b, size_t n) {
    for (size_t i = 0; i < n; i++)
        if (!close_enough(a[i], b[i]))
            return 0;
    return 1;
}

static int double_arrays_match(const double *a, const double *b, size_t n) {
    for (size_t i = 0; i < n; i++)
        if (!close_enough(a[i], b[i]))
            return 0;
    return 1;
}

/* Run each kernel `passes` times against its reference and report. */
static void check_all(void) {
    const int passes = 3;
    f4 a = { 0.999f, 0.998f, 0.997f, 0.996f };
    float a_s[4] = { 0.999f, 0.998f, 0.997f, 0.996f };
    for (int p = 0; p < passes; p++) {
        saxpy(a); saxpy_ref(a_s);
        xorshift(); xorshift_ref();
        dmix(); dmix_ref();
        blend(); blend_ref();
        call_kernel(); call_kernel_ref();
    }
    select_clamp(100.0f); select_clamp_ref(100.0f);

    report("saxpy", float_arrays_match((float *)fy, (float *)fy_ref, 4 * N), 0);
    report("xorshift", !memcmp(hs, hs_ref, sizeof hs), hash_bytes(hs, sizeof hs));
    report("select", float_arrays_match((float *)sel, (float *)sel_ref, 4 * N), 0);
    report("dmix", double_arrays_match((double *)dv, (double *)dv_ref, 2 * N), 0);
    report("blend", !memcmp(px_out, px_ref, sizeof px_out), hash_bytes(px_out, sizeof px_out));
    report("call", !memcmp(acc2, acc2_ref, sizeof acc2), hash_bytes(acc2, sizeof acc2));
}

#define TIME(name, reps, body)                                                  \
    do {                                                                        \
        double t0_ = now();                                                     \
        for (int r_ = 0; r_ < (reps); r_++) { body; }                          \
        double t_ = now() - t0_;                                                \
        total += t_;                                                            \
        printf("time   %-10s %8.2f M vectors/s  %.3f s\n", name,                \
               (double)(reps) * N / t_ / 1e6, t_);                              \
    } while (0)

int main(int argc, char **argv) {
    int reps = argc > 1 && !strcmp(argv[1], "--long") ? 50000 : 10000;
    printf("vecbench (%s), %d vectors per array\n", COMPILER_NAME, N);
    init();
    check_all();

    double total = 0;
    f4 a = { 0.999f, 0.998f, 0.997f, 0.996f };
    TIME("saxpy", reps, saxpy(a));
    TIME("xorshift", reps, xorshift());
    TIME("select", reps, select_clamp(100.0f));
    TIME("dmix", reps, dmix());
    TIME("blend", reps, blend());
    TIME("call", reps, call_kernel());
    printf("total time: %.3f s\n", total);
    printf(failures ? "%d check(s) FAILED\n" : "all checks passed\n", failures);
    return failures != 0;
}
