/* Modifiable lvalues ccc's lvalue check must keep accepting: pointers to
 * const, const pointees through typedefs, members, __real__/__imag__,
 * compound literals, shadowed enum constants, array parameters, and & of
 * functions, arrays, string literals and members. */
#include <stdio.h>

typedef const char *CP;
typedef const int CI;
typedef int *IP;
struct S { int m; const char *name; int arr[2]; };
struct C { const int k; int n; };
enum { E = 7 };

static int twice(int v) { return 2 * v; }

static int walk(char *const argv[], const char s[], CP p, const int a[2])
{
    int n = 0;
    while (*argv) { argv++; n++; }      /* the parameter is char *const *: modifiable */
    s++; p++; a++;
    return n * 100 + (s[0] - 'a') * 10 + (p[0] - 'a') + a[0];
}

int main(void)
{
    const char *s = "abc";
    s++;
    CP cp = "xyz";
    cp += 2;
    CI ci = 3;
    CI *cip = &ci;
    cip = 0;
    const IP *ipp = 0;
    ipp = 0;
    int *const *pp = 0;
    pp = 0;
    int x = 1, y = 2;
    int *const fixed = &x;
    *fixed = 5;                          /* the pointee is not const */
    struct S st = { 1, "n", { 3, 4 } };
    st.m += 10;
    st.name = "name";
    st.arr[1]++;
    struct S *sp = &st;
    sp->m *= 2;
    struct C c = { 1, 2 };
    c.n = 9;
    _Complex double z = 1.0;
    __real__ z = 4.0;
    __imag__ z += 2.5;
    int *lit = &(struct S){ 6, "c", { 0, 0 } }.m;
    *lit += 1;
    {
        int E = 0;                       /* shadows the enum constant */
        E = 3;
        y += E;
    }
    int (*fp)(int) = &twice;
    int (*ap)[2] = &st.arr;
    const char (*strp)[4] = &"abc";
    char *argv[] = { "a", "b", 0 };
    int arr2[2] = { 7, 8 };
    int b2[2][2] = { { 1, 2 }, { 3, 4 } };
    b2[1][0] = (*ap)[0] + fp(y);
    printf("%s %s %d %d %d\n", s, cp, ci, cip == 0, ipp == 0 && pp == 0);
    printf("%d %d %s %d %d %d\n", x, st.m, st.name, st.arr[1], c.k, c.n);
    printf("%g %g %d %d %c\n", __real__ z, __imag__ z, *lit, y, (*strp)[1]);
    printf("%d %d %d\n", b2[1][0], walk(argv, "abc", "abc", arr2), E);
    return 0;
}
