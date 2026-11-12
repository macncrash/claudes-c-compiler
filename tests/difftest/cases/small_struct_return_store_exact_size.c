/* A struct of 3, 5, 6 or 7 bytes returned in a register was stored to its
 * destination with a single 4- or 8-byte store, overwriting the bytes after
 * the object: the next array element, the next member or a neighbouring
 * variable. Found by a struct copy fuzzer (ga[0] = mk(x) inside a comma
 * expression clobbered ga[1]). */
#include <stdio.h>
#include <string.h>

struct S3 { char a, b, c; };
struct S5 { char a; char b[4]; };
struct S6 { char c[2]; short s[2]; };
struct S7 { char a[7]; };
struct S12 { int a, b, c; };

struct S3 g3[3];
struct S5 g5[3];
struct S6 g6[3];
struct S7 g7[3];
struct S12 g12[3];
struct Outer { struct S6 in; char tail[2]; } go;

__attribute__((noinline)) struct S3 mk3(int x) { return (struct S3){ (char)x, (char)(x + 1), (char)(x + 2) }; }
__attribute__((noinline)) struct S5 mk5(int x) { return (struct S5){ (char)x, { 1, 2, 3, (char)x } }; }
__attribute__((noinline)) struct S6 mk6(int x) { return (struct S6){ { (char)x, 7 }, { (short)(x * 3), (short)(x * 5) } }; }
__attribute__((noinline)) struct S7 mk7(int x) { struct S7 v; for (int i = 0; i < 7; i++) v.a[i] = (char)(x + i); return v; }
__attribute__((noinline)) struct S12 mk12(int x) { return (struct S12){ x, x * 2, x * 3 }; }

static unsigned sum(const void *p, unsigned n)
{
    const unsigned char *b = p;
    unsigned s = 0;
    for (unsigned i = 0; i < n; i++)
        s = s * 31 + b[i];
    return s;
}

__attribute__((noinline)) void fill(int x)
{
    int t = 0;
    memset(g3, 0x55, sizeof g3);
    memset(g5, 0x55, sizeof g5);
    memset(g6, 0x55, sizeof g6);
    memset(g7, 0x55, sizeof g7);
    memset(g12, 0x55, sizeof g12);
    memset(&go, 0x55, sizeof go);
    g3[0] = mk3(x);
    g5[0] = (t++, mk5(x));
    g6[2] = g6[0] = (t++, (g6[1] = mk6(x)));
    g6[1] = mk6(x + 1);
    g7[0] = x > 0 ? mk7(x) : mk7(-x);
    g12[0] = mk12(x);
    go.in = mk6(x + 2);
    printf("t %d\n", t);
}

__attribute__((noinline)) unsigned locals(int x)
{
    struct { struct S7 v; char guard[9]; } l;
    memset(&l, 0x33, sizeof l);
    l.v = mk7(x);
    struct S5 a = mk5(x);
    struct S3 b = mk3(x);
    return sum(&l, sizeof l) ^ sum(&a, sizeof a) ^ sum(&b, sizeof b);
}

int main(void)
{
    fill(9);
    printf("%u %u %u\n", sum(g3, sizeof g3), sum(g5, sizeof g5), sum(g6, sizeof g6));
    printf("%u %u %u\n", sum(g7, sizeof g7), sum(g12, sizeof g12), sum(&go, sizeof go));
    printf("%u\n", locals(4));
    return 0;
}
