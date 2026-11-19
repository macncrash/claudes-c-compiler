/* Unions in AAPCS64 homogeneous float aggregates (HFAs): a union whose
 * members share one float base type is an HFA with as many members as its
 * largest, and so is a struct holding one (union { float a[2]; float b; },
 * a packed struct around union { double; long double } on Darwin). ccc
 * classified no union as an HFA and passed them in x registers where clang
 * uses s/d registers. U4, U7, U8 and W must stay non-HFA. */
/* ccc builds this callee side; union_hfa_callee.helper.c (the caller) is built by the reference compiler. */
#include <stdio.h>
union U1 { float a; float b; };
union U2 { float a[2]; float b; };
union U3 { double d; long double ld; };
union U4 { double d; float f; };            /* mixed base: not HFA */
union U5 { float a[3]; struct { float x, y; } s; };
union U6 { _Complex float c; float a[2]; };
union U7 { double d; long l; };             /* int member: not HFA */
union U8 { float a; char c[8]; };           /* not HFA */
struct P { union U3 u; } __attribute__((packed));
struct Q { union U2 u; float z; };
struct R { union U1 u[3]; };
struct W { union U2 u; union U2 v; float z; };  /* 5 members: not HFA */
union U9 { struct Q q; float a[3]; };
#define PROTOS \
X(union U1, f1) X(union U2, f2) X(union U3, f3) X(union U4, f4) X(union U5, f5) \
X(union U6, f6) X(union U7, f7) X(union U8, f8) X(struct P, f9) X(struct Q, f10) \
X(struct R, f11) X(struct W, f12) X(union U9, f13)
#include <string.h>
static void dump(const char *n, const void *p, size_t sz)
{
    const unsigned char *b = p;
    printf("%s", n);
    for (size_t i = 0; i < sz; i++)
        printf(" %02x", b[i]);
    printf("\n");
}
/* Each takes (int, T, double, T) and returns its second T with every byte
 * incremented, so register assignment and return both show. */
#define X(T, f) \
    T f(int i, T a, double d, T b) { \
        dump(#f " a", &a, sizeof a); dump(#f " b", &b, sizeof b); \
        printf("%s i=%d d=%g\n", #f, i, d); \
        unsigned char *p = (unsigned char *)&b; \
        for (size_t k = 0; k < sizeof b; k++) p[k]++; \
        return b; \
    }
PROTOS
#undef X
