/* Structs over 256 bytes passed by value are copied in a loop by the callee,
 * into its own frame. The copy must leave everything else alone: the other
 * arguments, in and out of registers, and the values around the call. Sizes
 * sit on either side of the limit, one is not a multiple of 8, and the largest
 * puts the copy beyond every immediate offset. */
#include <stdarg.h>
#include <stdio.h>

typedef unsigned long ul;

struct s256 { unsigned char b[256]; };
struct s264 { unsigned char b[264]; };
struct s1001 { unsigned char b[1001]; };
struct s40000 { unsigned char b[40000]; };

static ul mix(const unsigned char *p, unsigned n)
{
    ul h = n;
    for (unsigned i = 0; i < n; i++)
        h = h * 131 + p[i];
    return h;
}

#define FILL(s, seed) \
    for (unsigned i = 0; i < sizeof (s).b; i++) \
        (s).b[i] = (unsigned char)(i * 7 + (seed) + (i >> 8))

/* In a register, changed by the callee: the caller's copy must not be. */
static ul __attribute__((noinline)) in_reg_256(struct s256 s)
{
    s.b[0]++;
    s.b[255]++;
    return mix(s.b, sizeof s.b);
}

static ul __attribute__((noinline)) in_reg_264(struct s264 s)
{
    s.b[0]++;
    s.b[263]++;
    return mix(s.b, sizeof s.b);
}

static ul __attribute__((noinline)) in_reg_1001(struct s1001 s)
{
    s.b[1000]++;
    return mix(s.b, sizeof s.b);
}

static ul __attribute__((noinline)) in_reg_40000(struct s40000 s)
{
    s.b[39999]++;
    return mix(s.b, sizeof s.b);
}

/* Beside other arguments in integer and floating-point registers. */
static ul __attribute__((noinline)) among_args(int a, struct s1001 s, long b, double d, struct s264 t, char c, float f)
{
    return mix(s.b, sizeof s.b) * 3 + mix(t.b, sizeof t.b) + (ul)a * 5 + (ul)b * 7 + (ul)(d * 2) + (ul)c + (ul)(f * 4);
}

/* With the integer registers used up, its pointer comes on the stack. */
static ul __attribute__((noinline)) on_stack(long a, long b, long c, long d, long e, long f, long g, long h,
                                             struct s1001 s, long i, struct s264 t)
{
    return mix(s.b, sizeof s.b) + mix(t.b, sizeof t.b) * 3
         + (ul)(a + 2 * b + 3 * c + 4 * d + 5 * e + 6 * f + 7 * g + 8 * h + 9 * i);
}

/* Variadic: the struct goes where the variadic ABI puts it. */
static ul __attribute__((noinline)) variadic(int n, ...)
{
    va_list ap;
    ul h = 0;
    va_start(ap, n);
    for (int i = 0; i < n; i++) {
        struct s1001 s = va_arg(ap, struct s1001);
        h = h * 31 + mix(s.b, sizeof s.b) + (ul)va_arg(ap, int);
    }
    va_end(ap);
    return h;
}

/* Through a pointer, whose target must survive the copy. */
typedef ul (*fn_t)(int, struct s1001, long, double, struct s264, char, float);

static ul __attribute__((noinline)) indirect(fn_t f, struct s1001 *s, struct s264 *t, ul x, ul y)
{
    /* x and y stay live around the call, and are arguments of it too. */
    ul r = f((int)x, *s, (long)y, 1.5, *t, 'c', 0.25f);
    return r + x * 11 + y * 13;
}

int main(void)
{
    static struct s256 a;
    static struct s264 b;
    static struct s1001 c;
    static struct s40000 d;
    FILL(a, 1);
    FILL(b, 2);
    FILL(c, 3);
    FILL(d, 4);
    fn_t f = among_args;

    printf("%lu\n", in_reg_256(a));
    printf("%lu\n", in_reg_264(b));
    printf("%lu\n", in_reg_1001(c));
    printf("%lu\n", in_reg_40000(d));
    printf("%lu %lu %lu %lu\n", mix(a.b, sizeof a.b), mix(b.b, sizeof b.b), mix(c.b, sizeof c.b), mix(d.b, sizeof d.b));
    printf("%lu\n", among_args(-3, c, 1L << 40, 2.5, b, 'x', 1.5f));
    printf("%lu\n", on_stack(1, 2, 3, 4, 5, 6, 7, 8, c, 9, b));
    printf("%lu\n", variadic(2, c, 5, c, 6));
    printf("%lu\n", indirect(f, &c, &b, 12345, 67890));
    return 0;
}
