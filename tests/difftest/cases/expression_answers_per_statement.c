/* What an expression's type and constant value are, asked again and again while
 * one statement is lowered: the answers kept for that statement must not leak
 * into the next, where a declaration may have changed them. */
#include <stdio.h>
#include <stdint.h>
#include <complex.h>

#define NI __attribute__((noinline))

typedef int v4 __attribute__((vector_size(16)));

static NI long mixed_widths(int a, long b, unsigned c, unsigned char d, short e)
{
    long r = a + b + c + d + e;
    r += a * c + d * e - b;
    r ^= (a | d) + (c & e) + (b ^ a);
    return r + a % 7 + c / 3;
}

static NI unsigned long long wide_last(int a, unsigned long long b)
{
    return a + a + a + a + a + a + a + b;
}

static NI double reals_late(int a, float f, double g)
{
    return a + a + a + f + a + g + a;
}

/* A name meaning a constant in one statement and a variable in the next. */
enum { K = 10 };

static NI int shadowed(int x)
{
    int r = K + K + x;
    {
        int K = x * 3;
        r += K + K + 1;
        {
            const int K = 5;
            r += K + K + x;
        }
        r += K + 1;
    }
    return r + K + K;
}

/* A variable of another type shadowing an enum constant: its size and type,
 * and a test of it, are the variable's. */
enum { WIDE = 3, FLAG = 1 };

static NI long shadowed_types(int x)
{
    long r = sizeof(WIDE) + WIDE;
    {
        char WIDE = (char)(x + 1);
        r = r * 100 + sizeof(WIDE) * 10 + WIDE + WIDE;
        {
            long long WIDE = 1LL << 40;
            r += (long)(sizeof(WIDE) + (WIDE >> 38));
        }
    }
    {
        int FLAG = x - x;
        if (FLAG)
            r += 1000000;
        r += FLAG ? 7 : 9;
    }
    if (FLAG)
        r += 2000000;
    return r;
}

/* Declarations inside an expression, between questions about it. */
static NI long statement_expressions(int x)
{
    long r = ({ const int c = 5; int d = c + c + 1; d + x; });
    r += ({ long w = x; w + w + 1; }) + ({ int w = 2; w + w; });
    r += ({ const int c = 7; enum { E = 3 }; c + E + c; }) * 2;
    r += ({ int y = ({ const int c = 4; c + c; }); y + y + x; });
    return r;
}

/* Short-circuit folding, from a constant on either side. */
static int calls;
static NI int side(int v) { calls++; return v; }

static NI int short_circuits(int x)
{
    int r = 0;
    r += (1 + 1 - 2) && side(x);
    r += (2 * 3 - 5) || side(x);
    r += side(x) && (1 + 2 - 3);
    r += side(x) || (4 - 4 + 1);
    r += (K + K - 20) && side(x + 1);
    return r * 100 + calls;
}

/* Complex and vector operands in the middle of a chain. */
static NI double complex_chain(double a)
{
    double complex z = 1.0 + 2.0 * I;
    double complex s = a + z + a + z * a + 1;
    return creal(s) * 10 + cimag(s);
}

static NI int vector_chain(int a)
{
    v4 v = { 1, 2, 3, 4 };
    v4 w = v + v + a + v * 2 + 1;
    return w[0] + w[1] * 10 + w[2] * 100 + w[3] * 1000;
}

/* Pointer differences and pointer sums in chains. */
static NI long pointers(int *p, int *q, int n)
{
    long d = q - p + n + (q - p);
    int *e = p + n + 1 + n;
    return d * 1000 + (e - p);
}

/* The same expression text, constant once and not the next time. */
static NI int same_text(int x)
{
    int r;
    {
        const int a = 3;
        r = a + a + a;
    }
    {
        int a = x;
        r += a + a + a;
    }
    return r;
}

int main(void)
{
    int arr[64];
    printf("%ld %llu %.3f\n", mixed_widths(-3, 1L << 40, 7u, 200, -9), wide_last(-1, 1ull << 63), reals_late(3, 0.5f, 0.25));
    printf("%d %ld\n", shadowed(4), statement_expressions(6));
    printf("%ld\n", shadowed_types(5));
    printf("%d\n", short_circuits(1));
    printf("%.3f %d\n", complex_chain(1.5), vector_chain(2));
    printf("%ld %d\n", pointers(arr, arr + 9, 5), same_text(10));
    return 0;
}
