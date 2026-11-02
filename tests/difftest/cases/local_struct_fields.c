/* Local structs and arrays split into one variable per field: counters of
 * a result struct updated in loops around calls, a zeroed initializer
 * with padding, a whole copy in and out, constant array indices, a struct
 * returned in registers with a negative field (folded to one 128-bit
 * constant), and the shapes that must stay in memory: a union read
 * through another member, an address passed on, a variable index. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

typedef struct {
    uint32_t inserted, hits, misses, after_delete;
    uint64_t sum;
} Result;

typedef struct { char tag; /* 7 bytes padding */ long value; short s; } Padded;
typedef struct { long a, b; } Pair;
typedef union { double d; uint64_t bits; uint32_t half[2]; } Pun;

static int __attribute__((noinline)) odd(int i) { return i & 1; }
static uint32_t __attribute__((noinline)) twice(int i) { return (uint32_t)i * 2; }
static void __attribute__((noinline)) touch(Padded *p) { p->value += 100; }

static Result __attribute__((noinline)) count(int n) {
    Result r = { 0, 0, 0, 0, 0 };
    for (int i = 0; i < n; i++)
        r.inserted += odd(i);
    for (int i = 0; i < n; i++) {
        uint32_t v = twice(i);
        if (v % 3 == 0) {
            r.hits++;
            r.sum += v;
        } else {
            r.misses += !odd(i);
        }
    }
    for (int i = 0; i < n / 2; i++) {
        r.after_delete++;
        r.sum = r.sum * 31 + (uint32_t)i;
    }
    return r;
}

static Padded __attribute__((noinline)) padded(int k) {
    Padded p = { 0 };
    p.tag = (char)('a' + k);
    p.value = -k * 1000L;
    p.s = (short)(k - 40000);
    return p;
}

static Pair __attribute__((noinline)) mkpair(long a) { Pair p = { a, -a }; return p; }
static Pair __attribute__((noinline)) pass_pair(long a) { return mkpair(a + 1); }

static Pair __attribute__((noinline)) swap(Pair in) {
    Pair t = in;
    Pair out;
    out.a = t.b;
    out.b = t.a;
    return out;
}

static long __attribute__((noinline)) fixed_indices(long x) {
    long v[4] = { x, x + 1, x + 2, x + 3 };
    for (int i = 0; i < 5; i++) {
        v[0] += v[3];
        v[3] = v[1] ^ v[2];
        v[1] = v[0] - 7;
    }
    return v[0] + v[1] * 3 + v[2] * 5 + v[3] * 7;
}

static uint64_t __attribute__((noinline)) pun(double d) {
    Pun u;
    u.d = d;
    return u.bits ^ u.half[1];
}

static long __attribute__((noinline)) escaped(int k) {
    Padded p = padded(k);
    touch(&p);
    return p.value + p.tag;
}

static long __attribute__((noinline)) variable_index(int k) {
    long v[4] = { 10, 20, 30, 40 };
    v[1] = k;
    return v[k & 3] + v[1];
}

static int __attribute__((noinline)) bytes(uint32_t x) {
    union { uint32_t w; unsigned char b[4]; } u = { x };
    return u.b[0] + 2 * u.b[1] + 3 * u.b[2] + 4 * u.b[3];
}

int main(void) {
    Result r = count(1000);
    printf("count %u %u %u %u %llu\n", r.inserted, r.hits, r.misses, r.after_delete, (unsigned long long)r.sum);
    for (int k = 0; k < 3; k++) {
        Padded p = padded(k);
        printf("padded %c %ld %d\n", p.tag, p.value, p.s);
    }
    Pair p = pass_pair(9);
    Pair q = swap(p);
    printf("pair %ld %ld swapped %ld %ld\n", p.a, p.b, q.a, q.b);
    printf("fixed %ld %ld\n", fixed_indices(3), fixed_indices(-11));
    printf("pun %llx %llx\n", (unsigned long long)pun(1.5), (unsigned long long)pun(-0.1));
    printf("escaped %ld %ld\n", escaped(1), escaped(5));
    printf("variable %ld %ld %ld\n", variable_index(0), variable_index(2), variable_index(7));
    printf("bytes %d %d\n", bytes(0x01020304u), bytes(0xff00ff00u));
    Result z;
    memset(&z, 0, sizeof z);
    z.hits = 3;
    printf("zeroed %u %llu\n", z.hits + z.misses, (unsigned long long)z.sum);
    return 0;
}
