/* Arrays of vectors, split into one local per vector or left whole, and vector
 * locals in the register pool: what must survive between the passes of a loop,
 * across calls, and past the registers there are. */
#include <arm_neon.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define NI __attribute__((noinline))
#define FORCE static inline __attribute__((always_inline))

typedef uint32_t v4 __attribute__((vector_size(16)));
typedef uint8_t b16 __attribute__((vector_size(16)));

static uint32_t sum(uint32x4_t v)
{
    return vgetq_lane_u32(v, 0) + 3 * vgetq_lane_u32(v, 1) + 5 * vgetq_lane_u32(v, 2) + 7 * vgetq_lane_u32(v, 3);
}

static uint32x4_t seed(uint32_t x)
{
    uint32_t a[4] = { x, x * 3 + 1, x ^ 0x55, x + 1000 };
    return vld1q_u32(a);
}

FORCE void mix(uint32x4_t v[8], int r)
{
    v[0] = vaddq_u32(v[0], v[4]);
    v[1] = veorq_u32(v[1], v[5]);
    v[2] = vaddq_u32(v[2], v[6 + (r & 1) * 0]);
    v[3] = vorrq_u32(vshlq_n_u32(v[3], 3), vshrq_n_u32(v[7], 29));
}

/* Split: constant indices, through an always_inline function. */
static NI uint32_t split_array(uint32_t x)
{
    uint32x4_t v[8];
    for (int i = 0; i < 8; i++)
        v[i] = seed(x + (uint32_t)i);
    mix(v, 0);
    mix(v, 1);
    uint32_t t = 0;
    for (int i = 0; i < 8; i++)
        t += sum(v[i]) * (uint32_t)(i + 1);
    return t;
}

static NI uint32_t split_unrolled(uint32_t x)
{
    uint32x4_t v[4] = { seed(x), seed(x + 1), seed(x + 2), seed(x + 3) };
    v[1] = vaddq_u32(v[0], v[3]);
    v[2] = veorq_u32(v[1], v[0]);
    v[0] = vsubq_u32(v[2], v[3]);
    return sum(v[0]) + 2 * sum(v[1]) + 3 * sum(v[2]) + 4 * sum(v[3]);
}

/* Left whole: a variable index. */
static NI uint32_t variable_index(uint32_t x, int k)
{
    uint32x4_t v[4] = { seed(x), seed(x + 1), seed(x + 2), seed(x + 3) };
    v[k & 3] = vaddq_u32(v[(k + 1) & 3], v[2]);
    return sum(v[k & 3]) + sum(v[0]);
}

/* A zeroed initializer, then part of it written. */
static NI uint32_t zeroed(uint32_t x)
{
    uint32x4_t v[3] = { { 0, 0, 0, 0 } };
    v[1] = seed(x);
    v[2] = vaddq_u32(v[0], v[1]);
    return sum(v[0]) + sum(v[1]) * 2 + sum(v[2]) * 3;
}

/* One lane stored alone, and read alone. */
static NI uint32_t lanes(uint32_t x)
{
    v4 v[3];
    v[0] = (v4){ x, x + 1, x + 2, x + 3 };
    v[1] = v[0] + v[0];
    v[2] = v[1] * v[0];
    v[1][2] = 77;
    uint32_t *p = (uint32_t *)&v[2];
    p[1] = v[1][2] + 1;
    return v[0][3] + v[1][2] + v[1][0] + v[2][1] + v[2][3];
}

/* Across two: a read straddling the vectors must keep the array whole. */
static NI uint64_t straddle(uint32_t x)
{
    uint32x4_t v[2] = { seed(x), seed(x + 1) };
    v[1] = vaddq_u32(v[1], v[0]);
    uint64_t r;
    memcpy(&r, (char *)v + 12, 8);
    return r;
}

/* Its address goes to a function. */
static NI void bump(uint32x4_t *p) { p[1] = vaddq_u32(p[0], p[1]); }

static NI uint32_t escaping(uint32_t x)
{
    uint32x4_t v[2] = { seed(x), seed(x + 1) };
    v[0] = vaddq_u32(v[0], v[1]);
    bump(v);
    return sum(v[0]) + sum(v[1]);
}

/* A value carried from one pass of a loop to the next, written last in it. */
static NI uint32_t carried(int n)
{
    uint32x4_t acc = seed(1), prev = seed(2);
    for (int i = 0; i < n; i++) {
        uint32x4_t t = vaddq_u32(acc, prev);
        uint32x4_t u = veorq_u32(t, seed((uint32_t)i));
        acc = vaddq_u32(t, u);
        prev = u;
    }
    return sum(acc) + 2 * sum(prev);
}

/* Written in part in each pass: the other lanes are last pass's. */
static NI uint32_t half_written(int n)
{
    v4 v = { 1, 2, 3, 4 };
    uint32_t t = 0;
    for (int i = 0; i < n; i++) {
        v[i & 3] = (uint32_t)i * 7 + 1;
        v4 w = v + v;
        t += w[0] + w[1] * 3 + w[2] * 5 + w[3] * 7;
    }
    return t;
}

/* A local with no value on the first pass, set late in the loop, used early. */
static NI uint32_t set_late(int n)
{
    v4 keep = { 0, 0, 0, 0 };
    uint32_t t = 0;
    for (int i = 0; i < n; i++) {
        v4 a = keep + (v4){ 1, 1, 1, 1 };
        t += a[2];
        v4 b = a * a;
        keep = b - a;
    }
    return t;
}

/* Calls between a vector's write and its read. */
static NI uint32_t clobber(uint32_t x) { uint32x4_t a = seed(x), b = seed(x + 9); return sum(vaddq_u32(a, b)); }

static NI uint32_t across_calls(uint32_t x)
{
    uint32x4_t a = seed(x);
    uint32x4_t b = vaddq_u32(a, a);
    uint32_t t = clobber(x);
    uint32x4_t c = vaddq_u32(b, a);
    t += clobber(x + 1);
    uint32x4_t d = veorq_u32(c, b);
    t += clobber(x + 2);
    return t + sum(a) + sum(b) + sum(c) + sum(d);
}

/* More alive at once than there are registers. */
#define ALL(F) F(0) F(1) F(2) F(3) F(4) F(5) F(6) F(7) F(8) F(9) F(10) F(11) F(12) F(13) F(14) F(15) \
    F(16) F(17) F(18) F(19) F(20) F(21) F(22) F(23) F(24) F(25) F(26) F(27) F(28) F(29) F(30) F(31) \
    F(32) F(33) F(34) F(35)
#define DEF(i) uint32x4_t a##i = seed(x + i);
#define USE(i) t = vaddq_u32(t, veorq_u32(a##i, vshlq_n_u32(t, 1)));
#define NEW(i) a##i = vaddq_u32(a##i, t);

static NI uint32_t pressure(uint32_t x)
{
    ALL(DEF)
    uint32x4_t t = vdupq_n_u32(x);
    ALL(USE)
    ALL(NEW)
    ALL(USE)
    return sum(t) + sum(a0) + sum(a17) + sum(a35);
}

/* Struct members, whole-struct copies, and an array within a struct. */
typedef struct { uint32x4_t a, b; } pair_t;
typedef struct { uint32x4_t v[3]; uint32_t n; } holder_t;

static NI uint32_t in_structs(uint32_t x)
{
    pair_t p = { seed(x), seed(x + 1) }, q;
    holder_t h;
    q = p;
    q.a = vaddq_u32(q.a, p.b);
    h.v[0] = q.a;
    h.v[1] = vaddq_u32(h.v[0], q.b);
    h.v[2] = veorq_u32(h.v[1], p.a);
    h.n = 3;
    holder_t c = h;
    c.v[1] = vaddq_u32(c.v[1], c.v[1]);
    return sum(c.v[0]) + sum(c.v[1]) * 2 + sum(c.v[2]) * 3 + sum(h.v[1]) * 5 + c.n;
}

/* Two arrays copied element by element, and one element onto itself. */
static NI uint32_t element_copies(uint32_t x)
{
    uint32x4_t a[4], b[4];
    for (int i = 0; i < 4; i++)
        a[i] = seed(x + (uint32_t)i);
    b[0] = a[3]; b[1] = a[2]; b[2] = a[1]; b[3] = a[0];
    a[0] = b[0];
    a[1] = a[1];
    a[2] = vaddq_u32(b[2], b[3]);
    return sum(a[0]) + 2 * sum(a[1]) + 3 * sum(a[2]) + 4 * sum(a[3]) + sum(b[1]);
}

/* Bytes, through a shuffle, and a branch choosing what is written. */
static NI uint32_t branches(uint32_t x, int f)
{
    b16 v[2] = { { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 } };
    if (f)
        v[1] = v[0] + (b16){ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
    else
        v[1] = v[0] * v[0];
    v4 w;
    memcpy(&w, &v[1], 16);
    return w[0] + w[3] + x;
}

int main(void)
{
    printf("%u %u\n", split_array(3), split_unrolled(4));
    printf("%u %u\n", variable_index(5, 1), variable_index(5, 6));
    printf("%u %u\n", zeroed(7), lanes(8));
    printf("%llu %u\n", (unsigned long long)straddle(9), escaping(10));
    printf("%u %u\n", carried(0), carried(9));
    printf("%u %u %u\n", half_written(0), half_written(3), half_written(11));
    printf("%u %u\n", set_late(1), set_late(6));
    printf("%u\n", across_calls(11));
    printf("%u\n", pressure(12));
    printf("%u %u\n", in_structs(13), element_copies(14));
    printf("%u %u\n", branches(1, 1), branches(1, 0));
    return 0;
}
