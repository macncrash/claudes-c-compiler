/* Results of calls that return an 8-byte vector, or a struct of one
 * double or one float, in a vector register: stored to an array element,
 * a struct field and a local, passed straight to the next call, and read
 * by lane. The copy out of each call's result is made from the register;
 * the values must come out the same. */
#include <stdio.h>
#include <stdint.h>

typedef uint32_t u2 __attribute__((vector_size(8)));
typedef float f2 __attribute__((vector_size(8)));
typedef int16_t h4 __attribute__((vector_size(8)));
struct one_double { double d; };
struct one_float { float f; };

#define N 32

static u2 acc[N];
static f2 facc[N];
static struct { h4 h; struct one_double od; struct one_float of; int tag; } rec[N];

__attribute__((noinline)) static u2 step(u2 a, u2 v, uint32_t k) { return a * 3u + (v ^ k); }
__attribute__((noinline)) static f2 fstep(f2 a, float k) { return a * 0.5f + k; }
__attribute__((noinline)) static h4 hstep(h4 a, int16_t k) { return (a << 1) - k; }
__attribute__((noinline)) static struct one_double dstep(struct one_double a, int i) {
    return (struct one_double){ a.d * 1.25 + i };
}
__attribute__((noinline)) static struct one_float fstep1(struct one_float a, int i) {
    return (struct one_float){ a.f - (float)i / 8 };
}

int main(void) {
    for (int i = 0; i < N; i++) {
        acc[i] = (u2){ (uint32_t)i * 7u, (uint32_t)i ^ 0x5au };
        facc[i] = (f2){ (float)i, (float)-i };
        rec[i].h = (h4){ (int16_t)i, (int16_t)(i * 3), (int16_t)-i, 9 };
        rec[i].od.d = i / 3.0;
        rec[i].of.f = (float)i * 1.5f;
    }
    for (int r = 0; r < 3; r++) {
        for (int i = 0; i < N; i++) {
            acc[i] = step(acc[i], (u2){ (uint32_t)i, -(uint32_t)i }, i & 7);
            facc[i] = fstep(facc[i], (float)r);
            rec[i].h = hstep(rec[i].h, (int16_t)r);
            rec[i].od = dstep(rec[i].od, i);
            rec[i].of = fstep1(rec[i].of, i);
            rec[i].tag = rec[i].h[1] + (int)acc[i][0];
        }
    }
    u2 chain = step(step(acc[1], acc[2], 3), acc[3], 5);
    f2 fl = fstep(fstep(facc[4], 1.0f), 2.0f);
    struct one_double dd = dstep(dstep(rec[5].od, 2), 3);
    uint64_t h = 0;
    for (int i = 0; i < N; i++)
        h = h * 31 + acc[i][0] + acc[i][1] * 7 + (uint64_t)(int64_t)rec[i].tag
            + (uint64_t)(rec[i].od.d * 16) + (uint64_t)(rec[i].of.f * 16 + 4096)
            + (uint64_t)(int64_t)(facc[i][0] * 8) + (uint64_t)(uint16_t)rec[i].h[3];
    printf("hash %016llx\n", (unsigned long long)h);
    printf("chain %u %u\n", chain[0], chain[1]);
    printf("fl %.4f %.4f\n", fl[0], fl[1]);
    printf("dd %.6f\n", dd.d);
    printf("rec7 %d %d %d %d %.5f %.5f\n", rec[7].h[0], rec[7].h[1], rec[7].h[2], rec[7].h[3],
           rec[7].od.d, rec[7].of.f);
    return 0;
}
