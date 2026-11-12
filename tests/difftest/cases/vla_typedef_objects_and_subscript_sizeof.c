/* VLA typedefs and sizeof of VLA subscripts, found by a VLA fuzzer:
 * - an object declared with a VLA typedef (typedef int T[n]; T a;) got a
 *   fixed slot of one element, so filling it overran the stack (crash);
 *   it now takes the size saved when the typedef was declared, also when
 *   the size variable changes afterwards, and arrays of such typedefs and
 *   2-D typedefs index with the typedef's strides;
 * - sizeof a[0] of a multi-dimensional VLA was folded to a constant 8. */
#include <stdio.h>

struct S { char c; int i; short s; };

__attribute__((noinline)) int td_lit(int n)
{
    typedef struct S T[n];
    T a;
    for (int i = 0; i < n; i++)
        a[i] = (struct S){ (char)i, i * 3, (short)i };
    return a[n - 1].i + (int)sizeof a;
}

__attribute__((noinline)) int td_int(int n)
{
    typedef int T[n];
    T a;
    for (int i = 0; i < n; i++)
        a[i] = i * 3;
    return a[n - 1] + (int)sizeof a;
}

__attribute__((noinline)) long td2(int n, int m)
{
    typedef int T[n][m];
    T a;
    for (int i = 0; i < n; i++)
        for (int j = 0; j < m; j++)
            a[i][j] = i * 10 + j;
    long s = 0;
    for (int i = 0; i < n; i++)
        for (int j = 0; j < m; j++)
            s = s * 7 + a[i][j];
    return s * 1000 + (long)sizeof a + (long)sizeof a[0];
}

__attribute__((noinline)) long td_later(int n)
{
    typedef short T[n];
    n += 10;
    T a;
    for (int i = 0; i < n - 10; i++)
        a[i] = (short)(i + 1);
    return (long)sizeof a * 100 + (long)sizeof(T) * 10 + a[n - 11];
}

__attribute__((noinline)) long td_arr(int n)
{
    typedef char T[n];
    T a[3];
    for (int k = 0; k < 3; k++)
        for (int i = 0; i < n; i++)
            a[k][i] = (char)(k * 16 + i);
    return (long)sizeof a * 1000 + (long)sizeof a[0] * 10 + a[2][n - 1];
}

__attribute__((noinline)) long td_rows(int n, int m)
{
    typedef int Row[m];
    Row a[n];
    for (int i = 0; i < n; i++)
        for (int j = 0; j < m; j++)
            a[i][j] = i * 10 + j;
    return a[n - 1][m - 1] * 1000 + (long)sizeof a[0] * 10 + (long)sizeof a;
}

__attribute__((noinline)) long td_struct2(int n, int m)
{
    typedef struct S R[m];
    R a[n];
    for (int i = 0; i < n; i++)
        for (int j = 0; j < m; j++)
            a[i][j] = (struct S){ (char)i, i * m + j, (short)j };
    return (long)sizeof a * 1000 + a[n - 1][m - 1].i;
}

__attribute__((noinline)) long plain2(int n, int m)
{
    int a[n][m];
    return (long)sizeof a * 1000000 + (long)sizeof a[0] * 1000 + (long)sizeof a[1][0];
}

__attribute__((noinline)) long plain3(int n, int m, int k)
{
    long a[n][m][k];
    return (long)sizeof a[0] * 1000 + (long)sizeof a[0][0];
}

int main(void)
{
    printf("%d %d\n", td_lit(6), td_int(6));
    printf("%ld %ld\n", td2(3, 4), td2(2, 5));
    printf("%ld %ld\n", td_later(4), td_later(7));
    printf("%ld %ld\n", td_arr(5), td_arr(2));
    printf("%ld %ld\n", td_rows(4, 3), td_rows(3, 5));
    printf("%ld %ld\n", td_struct2(3, 2), td_struct2(2, 4));
    printf("%ld %ld %ld\n", plain2(3, 4), plain2(2, 7), plain3(2, 3, 5));
    return 0;
}
