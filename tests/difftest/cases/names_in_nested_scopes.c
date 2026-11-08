/* Names meaning different things in nested scopes: variables, typedefs, enum
 * constants, tags and labels hiding one another, and enums declared where only
 * a type name is written (sizeof, a cast, a parameter, a for statement). */
#include <stdio.h>
#include <stddef.h>

#define NI __attribute__((noinline))

typedef short T;
enum { E = 7 };
static long G = 100;
struct S { char c; long l; };

static NI long typedef_shadowed(int x)
{
    long r = sizeof(T) * 1000;
    {
        int T = x + 1;               /* a variable named like the typedef */
        r += T * 10 + sizeof(T);
        {
            typedef long long T;     /* and a typedef again further in */
            T v = 1LL << 40;
            r += (long)(sizeof(T) + (v >> 39));
        }
        r += T;
    }
    T s = (T)70000;                  /* short again */
    return r * 100000 + s;
}

static NI long variable_shadowed_by_enum(int x)
{
    int F = x * 5;
    long r = F;
    {
        enum { F = 3 };              /* the constant hides the variable */
        r = r * 100 + F + F + sizeof(F);
        int a[F];
        r = r * 10 + (long)(sizeof a / sizeof a[0]);
    }
    r = r * 100 + F;
    return r;
}

static NI long enum_in_parameter_scope(int E)
{
    long r = E + E;                  /* the parameter, not the constant */
    {
        long E = 1L << 33;
        r += (E >> 30) + sizeof(E);
    }
    return r;
}

static NI long global_shadowed(int x)
{
    long r = G;
    {
        char G = (char)x;
        r = r * 1000 + G + sizeof(G);
        {
            extern long G;           /* the file's G again */
            r = r * 1000 + G / 10 + sizeof(G);
        }
    }
    return r;
}

static NI long tags_shadowed(void)
{
    long r = sizeof(struct S);
    {
        struct S { int a[5]; };      /* a new tag hiding the outer one */
        struct S s = { { 1, 2, 3, 4, 5 } };
        r = r * 100 + sizeof(struct S) + s.a[4];
        {
            enum S2 { A = 1, B };
            r = r * 10 + B;
        }
    }
    return r * 100 + offsetof(struct S, l);
}

static NI long in_for_scopes(int n)
{
    long r = 0;
    int i = 1000;
    for (int i = 0; i < n; i++) {
        int j = i * 2;
        for (long i = 5; i < 7; i++)
            r += i + j;
    }
    return r * 10000 + i;
}

static NI long member_and_variable(int x)
{
    struct { int x; int y; } p = { x + 1, x + 2 };
    int y = 50;
    return p.x * 1000 + p.y * 10 + y;
}

static NI long labels_and_names(int x)
{
    long r = 0;
    int L = x;
    goto L;
L:
    r += L;
    {
        int L = 9;
        r = r * 10 + L;
    }
    return r;
}

static NI long constant_expressions(int x)
{
    long r = 0;
    {
        const int E = x;             /* hides the constant, not itself constant */
        switch (3) {
        case 7: r = 1; break;        /* the enum is not visible as E here... */
        default: r = 2; break;
        }
        r = r * 100 + E + E;
    }
    switch (x) {
    case E: r = r * 10 + 7; break;   /* ...but is here */
    default: r = r * 10 + 1; break;
    }
    return r;
}

/* ---- */

typedef int U;
enum { Q = 4 };

/* An enum declared inside sizeof, a cast and a struct member. */
static NI long enums_in_odd_places(int x)
{
    long r = sizeof(enum { Z = 5 });
    r = r * 10 + Z;
    r = r * 10 + (int)(enum { Y = 6 })x;
    r = r * 10 + Y;
    struct { enum { W = 2 } w; int v; } s = { W, x };
    r = r * 10 + W + s.v;
    return r;
}

/* In a for statement's first clause, until the loop ends. */
static NI long enum_in_for(int x)
{
    int P = x * 10;
    long r = 0;
    for (enum { P = 2 } e = P; e < 4; e++)
        r = r * 10 + P + e;
    return r * 1000 + P;
}

/* A parameter named like a typedef. */
static NI long parameter_named_like_a_type(int U)
{
    return U * 2 + sizeof(U);
}

static NI long typedef_after(void)
{
    U v = 3;
    return v + sizeof(U);
}

/* Enum declared in a statement expression, then hidden by a variable. */
static NI long in_statement_expressions(int x)
{
    int R = x;
    long r = ({ enum { R = 9 }; R + R; });
    r = r * 100 + R;
    r = r * 100 + ({ int Q = x + 1; Q + Q; }) + Q;
    return r;
}

/* A static local hidden by an enum, and back. */
static NI long static_hidden(int x)
{
    static int M = 40;
    long r = M;
    {
        enum { M = 1 };
        r = r * 100 + M + sizeof(M);
    }
    M += x;
    return r * 100 + M;
}

/* An enum inside a function's parameter list affects only it. */
static NI long enum_in_parameters(enum { V0, V1, V2 } e)
{
    return e + V2;
}

/* Labels share no namespace with variables or constants. */
static NI long label_named_like_constant(int x)
{
    long r = 0;
    goto Q;
Q:
    r = Q + x;
    {
        int Q = 100;
        r += Q;
    }
    return r + Q;
}

static NI void second(void)
{
    printf("%ld\n", enums_in_odd_places(3));
    printf("%ld\n", enum_in_for(5));
    printf("%ld %ld\n", parameter_named_like_a_type(7), typedef_after());
    printf("%ld\n", in_statement_expressions(5));
    printf("%ld %ld\n", static_hidden(2), static_hidden(3));
    printf("%ld %ld\n", enum_in_parameters(1), label_named_like_constant(1));
}

int main(void)
{
    printf("%ld\n", typedef_shadowed(4));
    printf("%ld\n", variable_shadowed_by_enum(6));
    printf("%ld\n", enum_in_parameter_scope(2));
    printf("%ld\n", global_shadowed(9));
    printf("%ld\n", tags_shadowed());
    printf("%ld %ld\n", in_for_scopes(3), member_and_variable(4));
    printf("%ld %ld %ld\n", labels_and_names(3), constant_expressions(7), constant_expressions(5));
    second();
    return 0;
}
