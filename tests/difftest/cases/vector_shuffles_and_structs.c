/* GCC vectors where they meet the rest of the language: shuffles, with a
 * constant or a run-time mask, one source or two; __auto_type and typeof;
 * statement expressions; and structs and arrays holding vectors, passed and
 * returned by value. The sizes of such structs were once worked out from the
 * vector's element, so a struct of two vectors was four bytes to a caller. */
#include <stdio.h>
#include <string.h>

typedef unsigned char b16 __attribute__((vector_size(16)));
typedef unsigned short h8 __attribute__((vector_size(16)));
typedef unsigned int u4 __attribute__((vector_size(16)));
typedef int i4 __attribute__((vector_size(16)));
typedef unsigned long long d2 __attribute__((vector_size(16)));
typedef float f4 __attribute__((vector_size(16)));
typedef unsigned int u2 __attribute__((vector_size(8)));

typedef struct { h8 val[2]; } arr2;
typedef struct { h8 a, b; } two;
typedef struct { h8 v; } one;
typedef struct { h8 v; long l; } mixed;
typedef struct { h8 v[4]; } arr4;
typedef struct { h8 v[5]; } arr5;
typedef struct { char tag; u2 small; u4 big[2]; short tail; } padded;

/* Struct sizes come from the vector, in any order of definition. */
typedef struct { u4 first; } before;
typedef union { u4 v; unsigned w[4]; } overlay;

/* clang has no __builtin_shuffle: there, this is what it means. */
#ifdef __clang__
#define SHUF(a, m) __extension__ ({ __typeof__(a) a_ = (a), r_; __typeof__(m) m_ = (m); \
    unsigned n_ = sizeof a_ / sizeof a_[0]; \
    for (unsigned i_ = 0; i_ < n_; i_++) r_[i_] = a_[(unsigned long)m_[i_] & (n_ - 1)]; r_; })
#define SHUF2(a, b, m) __extension__ ({ __typeof__(a) a_ = (a), b_ = (b), r_; __typeof__(m) m_ = (m); \
    unsigned n_ = sizeof a_ / sizeof a_[0]; \
    for (unsigned i_ = 0; i_ < n_; i_++) { unsigned k_ = (unsigned long)m_[i_] & (2 * n_ - 1); r_[i_] = k_ < n_ ? a_[k_] : b_[k_ - n_]; } r_; })
#else
#define SHUF(a, m) __builtin_shuffle(a, m)
#define SHUF2(a, b, m) __builtin_shuffle(a, b, m)
#endif

static void show(const char *name, const void *p, unsigned n)
{
    const unsigned char *b = p;
    printf("%s", name);
    for (unsigned i = 0; i < n; i++)
        printf(" %02x", b[i]);
    printf("\n");
}

#define T(type, set, get) \
    static __attribute__((noinline)) type make_##type(int n) { type r; memset(&r, 0, sizeof r); set; return r; } \
    static __attribute__((noinline)) int read_##type(type r) { return get; }

T(arr2, r.val[1][3] = (unsigned short)n, r.val[1][3] + 1)
T(two, r.b[3] = (unsigned short)n, r.b[3] + 1)
T(one, r.v[3] = (unsigned short)n, r.v[3] + 1)
T(mixed, (r.v[3] = (unsigned short)n, r.l = 8), r.v[3] + (int)r.l)
T(arr4, r.v[3][1] = (unsigned short)n, r.v[3][1] + 1)
T(arr5, r.v[4][1] = (unsigned short)n, r.v[4][1] + 1)
T(padded, (r.tag = 'x', r.small[1] = (unsigned)n, r.big[1][2] = (unsigned)n * 2, r.tail = 9), (int)r.small[1] + (int)r.big[1][2] + r.tail)

/* A variable index into a vector inside an array inside a struct. */
static __attribute__((noinline)) arr2 interleaved(const unsigned short *p)
{
    arr2 r;
    for (int i = 0; i < 8; i++) {
        r.val[0][i] = p[i * 2];
        r.val[1][i] = p[i * 2 + 1];
    }
    return r;
}

#define SHIFT(x, n) __extension__ ({ u4 r_; for (int i_ = 0; i_ < 4; i_++) r_[i_] = (x)[i_] >> (n); r_; })

static __attribute__((noinline)) u4 identity(u4 x) { return x; }

static __attribute__((noinline)) b16 run_time_mask(b16 a, b16 m) { return SHUF(a, m); }
static __attribute__((noinline)) b16 run_time_two(b16 a, b16 b, b16 m) { return SHUF2(a, b, m); }

int main(void)
{
    static const unsigned short data[16] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
    u4 a = { 0x100, 0x200, 0x300, 0x400 }, b = { 1, 2, 3, 4 };
    b16 bytes, more;
    for (int i = 0; i < 16; i++) {
        bytes[i] = (unsigned char)(i * 3 + 1);
        more[i] = (unsigned char)(200 - i);
    }

    printf("%zu %zu %zu %zu %zu %zu %zu %zu %zu\n", sizeof(arr2), sizeof(two), sizeof(one), sizeof(mixed), sizeof(arr4), sizeof(arr5),
           sizeof(padded), sizeof(before), sizeof(overlay));
    printf("%d %d\n", make_arr2(7).val[1][3], read_arr2(make_arr2(7)));
    printf("%d %d\n", make_two(7).b[3], read_two(make_two(7)));
    printf("%d %d\n", make_one(7).v[3], read_one(make_one(7)));
    printf("%d %d\n", make_mixed(7).v[3], read_mixed(make_mixed(7)));
    printf("%d %d\n", make_arr4(7).v[3][1], read_arr4(make_arr4(7)));
    printf("%d %d\n", make_arr5(7).v[4][1], read_arr5(make_arr5(7)));
    printf("%d\n", read_padded(make_padded(7)));

    arr2 d = interleaved(data);
    show("interleaved", &d, sizeof d);

    /* __auto_type and typeof keep the whole vector. */
    __auto_type c = identity(a);
    show("auto call", &c, sizeof c);
    __auto_type s = SHIFT(a, 4);
    show("auto statement", &s, sizeof s);
    __typeof__(a) t = a + b;
    show("typeof", &t, sizeof t);
    printf("%zu %zu\n", sizeof(__typeof__(SHIFT(a, 1))), sizeof c);

    /* Shuffles. */
    b16 rev = { 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0 };
    b16 wrap = { 16, 17, 33, 255, 0, 1, 2, 3, 240, 241, 242, 243, 4, 5, 6, 7 };
    b16 across = { 0, 16, 1, 17, 2, 18, 3, 19, 31, 15, 30, 14, 29, 13, 28, 12 };
    u4 mask4 = { 3, 2, 1, 0 };
    i4 sm = { 1, 0, -1, 2 };
    show("reverse", (b16[]){ SHUF(bytes, rev) }, 16);
    show("wrap", (b16[]){ SHUF(bytes, wrap) }, 16);
    show("two", (b16[]){ SHUF2(bytes, more, across) }, 16);
    show("run time", (b16[]){ run_time_mask(bytes, rev) }, 16);
    show("run time wrap", (b16[]){ run_time_mask(bytes, wrap) }, 16);
    show("run time two", (b16[]){ run_time_two(bytes, more, across) }, 16);
    show("words", (u4[]){ SHUF(a, mask4) }, 16);
    show("signed mask", (i4[]){ SHUF((i4)a, sm) }, 16);
    f4 fl = { 1.5f, 2.5f, 3.5f, 4.5f };
    show("floats", (f4[]){ SHUF(fl, ((i4){ 2, 2, 0, 3 })) }, 16);
    typedef unsigned short h4 __attribute__((vector_size(8)));
    show("list", (b16[]){ __builtin_shufflevector(bytes, more, 0, 16, 1, 17, 2, 18, 3, 19, 31, 15, 30, 14, 29, 13, 28, 12) }, 16);
    show("list one", (u4[]){ __builtin_shufflevector(a, a, 3, 3, 0, 1) }, 16);
    show("list narrow", (u2[]){ __builtin_shufflevector(b, b, 2, 7) }, 8);
    h8 wide = { 1, 2, 3, 4, 5, 6, 7, 8 };
    show("list half", (h4[]){ __builtin_shufflevector(wide, wide, 6, 4, 2, 0) }, 8);
    return 0;
}
