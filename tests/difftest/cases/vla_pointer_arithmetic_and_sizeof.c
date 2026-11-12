/* Pointers to VLA types, found by a VLA fuzzer: int (*p)[m], Row *p with
 * Row a VLA typedef, and VLA parameters stepped with v++. Subscripts,
 * p + k, p++, p -= k, p - q and sizeof *p used the static element size
 * (the VLA dimension counted as nothing), so they read the wrong rows. */
#include <stdio.h>

struct S { char c; int i; short s; };

__attribute__((noinline)) long rowidx(int n, int m)
{
    int a[n][m];
    for (int i = 0; i < n; i++)
        for (int j = 0; j < m; j++)
            a[i][j] = i * 10 + j;
    int (*p)[m] = a;
    long r = p[2][1];
    p += 1;
    r = r * 100 + p[1][m - 1];
    r = r * 100 + (*(p + 1))[0];
    p++;
    r = r * 100 + **p;
    --p;
    r = r * 100 + p[0][1] + 1[p][0];
    return r * 10000 + (long)sizeof *p * 100 + (long)sizeof p[1];
}

__attribute__((noinline)) long walk(int n, int m)
{
    double a[n][m];
    for (int i = 0; i < n; i++)
        for (int j = 0; j < m; j++)
            a[i][j] = i + j * 0.5;
    double s = 0;
    for (double (*p)[m] = a; p < a + n; p++)
        s = s * 3 + (*p)[m - 1] + p[0][0];
    double (*q)[m] = &a[n - 1];
    double (*b)[m] = a;
    return (long)s * 1000 + (q - b) * 10 + (long)(a + 1 - a);
}

__attribute__((noinline)) long cube(int n, int m, int k)
{
    short a[n][m][k];
    for (int i = 0; i < n; i++)
        for (int j = 0; j < m; j++)
            for (int l = 0; l < k; l++)
                a[i][j][l] = (short)(i * 100 + j * 10 + l);
    short (*p)[m][k] = a;
    p = p + (n - 1);
    long r = p[0][m - 1][k - 1];
    p -= 1;
    r = r * 1000 + p[0][1][2];
    return r * 100000 + (long)sizeof *p * 100 + (long)sizeof (*p)[0];
}

__attribute__((noinline)) long td_rows(int n, int m)
{
    typedef int Row[m];
    Row a[n];
    for (int i = 0; i < n; i++)
        for (int j = 0; j < m; j++)
            a[i][j] = i * 10 + j;
    Row *p = a;
    long r = p[n - 1][m - 1];
    p++;
    r = r * 100 + (*p)[1];
    return r * 10000 + (long)sizeof *p * 10 + (long)sizeof p[0][0];
}

__attribute__((noinline)) long td_struct(int n)
{
    typedef struct S T[n];
    T a[3];
    for (int k = 0; k < 3; k++)
        for (int i = 0; i < n; i++)
            a[k][i] = (struct S){ (char)k, k * 100 + i, (short)i };
    T *p = &a[1];
    T *e = a + 3;
    return (long)(e - p) * 100000 + p[1][n - 1].i * 10 + (long)sizeof *p;
}

__attribute__((noinline)) long whole(int n)
{
    long a[n];
    for (int i = 0; i < n; i++)
        a[i] = i * 7;
    long (*p)[n] = &a;
    char *end = (char *)(&a + 1);
    return (*p)[n - 1] * 1000 + (long)sizeof *p * 10 + (end - (char *)a == (long)sizeof a);
}

__attribute__((noinline)) long param_step(int n, int m, int v[n][m])
{
    long r = 0;
    for (int i = 0; i < n; i++, v++)
        r = r * 10 + (*v)[m - 1] % 10;
    v -= n;
    return r * 1000 + (v + 1)[1][0] + (long)sizeof *v;
}

__attribute__((noinline)) long pick(int n, int m, int c)
{
    int a[n][m], b[n][m];
    for (int i = 0; i < n; i++)
        for (int j = 0; j < m; j++)
            a[i][j] = i * 10 + j, b[i][j] = 500 + i * 10 + j;
    int (*p)[m] = c ? a : b;
    return (c ? p : b)[n - 1][m - 1] + (p = b, p + 1)[0][0] * 1000;
}

int main(void)
{
    int g[4][5];
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 5; j++)
            g[i][j] = i * 10 + j;
    printf("%ld %ld\n", rowidx(4, 3), rowidx(3, 5));
    printf("%ld %ld\n", walk(3, 4), walk(5, 2));
    printf("%ld %ld\n", cube(3, 4, 5), cube(2, 3, 4));
    printf("%ld %ld\n", td_rows(4, 3), td_rows(3, 5));
    printf("%ld %ld\n", td_struct(2), td_struct(5));
    printf("%ld %ld\n", whole(3), whole(9));
    printf("%ld\n", param_step(4, 5, g));
    printf("%ld %ld\n", pick(3, 4, 1), pick(4, 2, 0));
    return 0;
}
