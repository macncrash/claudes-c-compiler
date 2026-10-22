/* Scalar code shapes from tuning a bitboard program: struct copies of many
 * sizes (16-byte steps, a 32-byte-chunk tail, a copy loop past 512 bytes),
 * the highest set bit as 63 - clzll and 31 - clz, multiply-add and
 * multiply-subtract (index * 48 + base, x % 6), sign-extended byte and
 * halfword loads, bit tests against zero, functions with large frames
 * (over 504 bytes and over a page) keeping values in callee-saved registers
 * across calls, and small helpers inlined into a recursive function. */
#include <stdio.h>
#include <string.h>

#define COPY_STRUCT(n) struct s##n { unsigned char b[n]; };
COPY_STRUCT(1) COPY_STRUCT(7) COPY_STRUCT(16) COPY_STRUCT(17) COPY_STRUCT(31)
COPY_STRUCT(32) COPY_STRUCT(48) COPY_STRUCT(63) COPY_STRUCT(200) COPY_STRUCT(513)
COPY_STRUCT(1000)

static unsigned long long h = 1469598103934665603ULL;

static void mix(const void *p, unsigned long n) {
    const unsigned char *b = p;
    for (unsigned long i = 0; i < n; i++)
        h = (h ^ b[i]) * 1099511628211ULL;
}

#define COPY_TEST(n)                                                   \
    __attribute__((noinline)) static void copy##n(struct s##n *dst, const struct s##n *src) { \
        *dst = *src;                                                   \
    }
COPY_TEST(1) COPY_TEST(7) COPY_TEST(16) COPY_TEST(17) COPY_TEST(31)
COPY_TEST(32) COPY_TEST(48) COPY_TEST(63) COPY_TEST(200) COPY_TEST(513)
COPY_TEST(1000)

#define RUN_COPY(n) do {                                               \
        struct s##n a, b;                                              \
        for (int i = 0; i < n; i++) { a.b[i] = (unsigned char)(i * 7 + n); b.b[i] = 0xee; } \
        copy##n(&b, &a);                                               \
        mix(&b, sizeof b);                                             \
        if (memcmp(&a, &b, sizeof a)) printf("copy %d differs\n", n);  \
    } while (0)

static int msb64(unsigned long long b) { return 63 - __builtin_clzll(b); }
static int msb32(unsigned b) { return 31 - __builtin_clz(b); }

typedef struct { unsigned long long pieces[2][6]; signed char board[64]; short hs[8]; int side; } Pos;

__attribute__((noinline)) static unsigned long long piece_at(const Pos *p, int color, int type) {
    return p->pieces[color][type];          /* index * 48 + base */
}

__attribute__((noinline)) static int split(const Pos *p, int sq) {
    int pc = p->board[sq];                  /* sign-extended byte */
    return (pc / 6) * 100 + pc % 6 + p->hs[sq & 7];
}

__attribute__((noinline)) static int tests(const Pos *p, unsigned long long mask) {
    int n = 0;
    for (int c = 0; c < 2; c++)
        for (int t = 0; t < 6; t++) {
            if (p->pieces[c][t] & mask)
                n += 1 + t;
            if ((p->pieces[c][t] & 0xff) == 0)
                n += 100;
        }
    return n;
}

__attribute__((noinline)) static long sink(long *a, int n) {
    long s = 0;
    for (int i = 0; i < n; i++)
        s = s * 3 + a[i];
    return s;
}

/* Large frames with values live across calls. */
__attribute__((noinline)) static long big_frame(int k) {
    long a[80], b = k, c = k * 3, d = k ^ 5, e = k - 9, f = k * k;
    for (int i = 0; i < 80; i++)
        a[i] = i * b - c;
    long s = sink(a, 80);
    s += sink(a + 10, 30) * d;
    return s + b + c + d + e + f;
}

__attribute__((noinline)) static long page_frame(int k) {
    long a[700], x = k + 1, y = k * 7, z = k ^ 0x55;
    for (int i = 0; i < 700; i++)
        a[i] = (i ^ k) + x;
    long s = sink(a, 700) + sink(a + 600, 100) * y;
    return s ^ z ^ x;
}

static int add_bits(unsigned long long b) { return __builtin_popcountll(b) + msb64(b | 1); }

__attribute__((noinline)) static long walk(unsigned long long b, int depth) {
    if (depth == 0)
        return add_bits(b);
    long n = 0;
    for (unsigned long long c = b; c; c &= c - 1)
        n += walk(b ^ (c & -c), depth - 1) + msb64(c);
    return n;
}

int main(void) {
    RUN_COPY(1); RUN_COPY(7); RUN_COPY(16); RUN_COPY(17); RUN_COPY(31); RUN_COPY(32);
    RUN_COPY(48); RUN_COPY(63); RUN_COPY(200); RUN_COPY(513); RUN_COPY(1000);
    printf("copies %016llx\n", h);

    unsigned long long vals[] = { 1, 2, 3, 0x80, 0xffff, 1ULL << 40, 0x8000000000000000ULL, 0x123456789aULL };
    for (int i = 0; i < 8; i++)
        printf("msb %d %d\n", msb64(vals[i]), msb32((unsigned)(vals[i] | 1)));

    Pos p;
    memset(&p, 0, sizeof p);
    for (int c = 0; c < 2; c++)
        for (int t = 0; t < 6; t++)
            p.pieces[c][t] = 0x0101010101010101ULL << (c * 6 + t) ^ (unsigned long long)(t * 977);
    for (int i = 0; i < 64; i++)
        p.board[i] = (signed char)((i * 5) % 13 - 1);
    for (int i = 0; i < 8; i++)
        p.hs[i] = (short)(i * -1234);
    long acc = 0;
    for (int c = 0; c < 2; c++)
        for (int t = 0; t < 6; t++)
            acc += (long)(piece_at(&p, c, t) % 1000003);
    for (int sq = 0; sq < 64; sq++)
        acc = acc * 31 + split(&p, sq);
    printf("pos %ld %d %d\n", acc, tests(&p, 0x40), tests(&p, 0));
    printf("frames %ld %ld\n", big_frame(7), page_frame(3));
    printf("walk %ld\n", walk(0x1234, 3));
    return 0;
}
