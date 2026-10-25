/* memcpy, memmove and memset as the system headers expand them
 * (__builtin___memcpy_chk with __builtin_object_size), with constant
 * lengths done in place: every length up to 130 at unaligned offsets, fill
 * bytes constant and variable, lengths that become constant only after
 * inlining, overlapping memmove (which must not become a plain copy),
 * explicit object sizes the length fits in, and the returned pointers.
 * Also a recursive helper called from several places (no longer inlined
 * into itself). */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static unsigned char buf[512], src[512];

static uint32_t sum(const unsigned char *p, int n) {
    uint32_t h = 2166136261u;
    for (int i = 0; i < n; i++)
        h = (h ^ p[i]) * 16777619u;
    return h;
}

static void reset(void) {
    for (int i = 0; i < 512; i++) {
        buf[i] = (unsigned char)(i * 7 + 3);
        src[i] = (unsigned char)(i * 13 + 1);
    }
}

/* Each constant length, through the header macro. */
#define COPY(n) do { reset(); void *r = memcpy(buf + 3, src + 5, n); h = h * 31 + sum(buf, 160) + (r == buf + 3); } while (0)
#define SET(n, c) do { reset(); void *r = memset(buf + 1, c, n); h = h * 31 + sum(buf, 160) + (r == buf + 1); } while (0)

__attribute__((noinline)) static uint32_t constant_lengths(int fill) {
    uint32_t h = 0;
    COPY(0); COPY(1); COPY(2); COPY(3); COPY(4); COPY(5); COPY(7); COPY(8); COPY(9); COPY(13);
    COPY(15); COPY(16); COPY(17); COPY(24); COPY(31); COPY(32); COPY(33); COPY(56); COPY(63);
    COPY(64); COPY(65); COPY(100); COPY(127); COPY(128); COPY(129); COPY(130);
    SET(0, 0); SET(1, 0); SET(3, 0xab); SET(7, -1); SET(8, 0); SET(9, 0x5a); SET(15, fill);
    SET(16, fill); SET(24, 0); SET(33, fill); SET(56, 0); SET(100, 0x33); SET(127, fill);
    SET(128, 0); SET(129, fill); SET(130, 0x77);
    return h;
}

/* Lengths constant only once inlined. */
static void clear_n(void *p, int n) { memset(p, 0, (size_t)n * 4); }
static void copy_n(void *d, const void *s, int n) { memcpy(d, s, (size_t)n * 8); }

__attribute__((noinline)) static uint32_t folded_lengths(void) {
    uint32_t h = 0;
    reset();
    clear_n(buf + 2, 5);
    copy_n(buf + 40, src + 7, 9);
    clear_n(buf + 200, 33);
    copy_n(buf + 300, src, 17);
    h = sum(buf, 512);
    return h;
}

/* Overlapping moves, both directions, constant and variable lengths. */
__attribute__((noinline)) static uint32_t overlaps(int k) {
    uint32_t h = 0;
    reset();
    memmove(buf + 4, buf, 64);
    h = h * 31 + sum(buf, 128);
    memmove(buf, buf + 9, 50);
    h = h * 31 + sum(buf, 128);
    memmove(buf + k, buf, 100);
    h = h * 31 + sum(buf, 256);
    return h;
}

/* An explicit object size the constant length fits in, and one where only
 * the runtime check can tell. */
__attribute__((noinline)) static uint32_t sized(int n) {
    char small[16];
    __builtin___memset_chk(small, 'x', 16, sizeof small);
    __builtin___memcpy_chk(small, "abcdefgh", 8, sizeof small);
    __builtin___memcpy_chk(small + 8, "ABCDEFGH", (size_t)n, sizeof small - 8);
    return sum((unsigned char *)small, 16);
}

/* Variable lengths and fill. */
__attribute__((noinline)) static uint32_t variable(int n, int c) {
    reset();
    memset(buf + 3, c, (size_t)n);
    memcpy(buf + 300, buf, (size_t)n + 1);
    return sum(buf, 512);
}

typedef struct Tree { struct Tree *l, *r; int v; } Tree;

static int depth(const Tree *t) {
    if (!t)
        return 0;
    int a = depth(t->l), b = depth(t->r);
    return 1 + (a > b ? a : b);
}

__attribute__((noinline)) static int depths(const Tree *a, const Tree *b) {
    return depth(a) * 100 + depth(b) * 10 + depth(a ? a->l : 0);
}

int main(void) {
    printf("constant %u %u\n", constant_lengths(0x11), constant_lengths(0x2ee));
    printf("folded %u\n", folded_lengths());
    printf("overlap %u %u\n", overlaps(3), overlaps(17));
    printf("sized %u %u\n", sized(8), sized(3));
    printf("variable %u %u %u\n", variable(0, 1), variable(37, 0x1ff), variable(200, -2));
    Tree n[6] = { { &n[1], &n[2], 1 }, { &n[3], 0, 2 }, { 0, &n[4], 3 }, { 0, 0, 4 }, { &n[5], 0, 5 }, { 0, 0, 6 } };
    printf("depth %d %d\n", depths(&n[0], &n[2]), depths(0, &n[5]));
    return 0;
}
