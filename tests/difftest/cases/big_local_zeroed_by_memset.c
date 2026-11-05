/* Locals over 256 bytes are cleared by a call to memset, smaller ones by
 * stores. The clearing must cover exactly the part the initializer leaves out,
 * come before the stores of what it does give, and happen every time the
 * declaration is reached. The stack is dirtied first, so that a missed byte
 * shows. Sizes sit on either side of the limit. */
#include <stdio.h>
#include <string.h>

typedef unsigned long ul;

static ul sum(const void *p, unsigned n)
{
    const unsigned char *b = p;
    ul h = n;
    for (unsigned i = 0; i < n; i++)
        h = h * 131 + b[i];
    return h;
}

static void __attribute__((noinline)) dirty(void)
{
    volatile unsigned char junk[70000];
    for (unsigned i = 0; i < sizeof junk; i++)
        junk[i] = (unsigned char)(i * 3 + 1);
}

static int __attribute__((noinline)) next(int *n)
{
    return ++*n;
}

#define WHOLE(name, size) \
    static ul __attribute__((noinline)) name(void) \
    { \
        struct { unsigned char b[size]; } s = { { 0 } }; \
        return sum(&s, sizeof s); \
    }

WHOLE(whole_255, 255)
WHOLE(whole_256, 256)
WHOLE(whole_257, 257)
WHOLE(whole_264, 264)
WHOLE(whole_60001, 60001)

/* Stores after the clearing: first, last and in between. */
static ul __attribute__((noinline)) some_given(int a)
{
    int v[300] = { a, a + 1, [150] = a + 2, [299] = a + 3 };
    return sum(v, sizeof v);
}

/* Their values come from calls, which run after the clearing too. */
static ul __attribute__((noinline)) from_calls(void)
{
    int n = 0;
    long v[100] = { next(&n), next(&n), [98] = next(&n) };
    return sum(v, sizeof v) + (ul)n;
}

/* Regions inside a struct, at offsets that are not multiples of 8. */
struct inner { char c[300]; };
struct outer {
    char tag;
    struct inner in;
    short mid;
    char big[1000];
    char end;
};

static ul __attribute__((noinline)) inside_a_struct(char t)
{
    struct outer o = { .tag = t, .mid = 7, .end = (char)(t + 1) };
    return sum(&o.tag, 1) + sum(&o.in, sizeof o.in) * 3 + sum(&o.mid, 2) * 5
         + sum(o.big, sizeof o.big) * 7 + sum(&o.end, 1);
}

/* An array of structs where only one element is given. */
static ul __attribute__((noinline)) one_element(int x)
{
    struct { int a; char b[61]; } v[20] = { [7] = { x, "seven" } };
    return sum(v, sizeof v);
}

/* A compound literal, assigned over a value it reads. */
struct big { int first; int rest[200]; };

static ul __attribute__((noinline)) literal_over_itself(void)
{
    struct big b;
    memset(&b, 0x5a, sizeof b);
    b.first = 9;
    b = (struct big){ .rest = { b.first, 3 } };
    return sum(&b, sizeof b);
}

/* Cleared again each time round, after being written. */
static ul __attribute__((noinline)) every_time(int n)
{
    ul h = 0;
    for (int i = 0; i < n; i++) {
        char v[500] = { (char)i };
        h = h * 31 + sum(v, sizeof v);
        memset(v + 1, i + 1, sizeof v - 1);
        h += (ul)v[499];
    }
    return h;
}

/* The clearing must not disturb arguments still to be used. */
static ul __attribute__((noinline)) keeps_arguments(ul a, ul b, ul c, double d, ul e, ul f, ul g, ul h, ul i)
{
    char v[400] = { 1 };
    return sum(v, sizeof v) + a + 2 * b + 3 * c + (ul)(d * 4) + 5 * e + 6 * f + 7 * g + 8 * h + 9 * i;
}

int main(void)
{
    dirty(); printf("%lu\n", whole_255());
    dirty(); printf("%lu\n", whole_256());
    dirty(); printf("%lu\n", whole_257());
    dirty(); printf("%lu\n", whole_264());
    dirty(); printf("%lu\n", whole_60001());
    dirty(); printf("%lu\n", some_given(11));
    dirty(); printf("%lu\n", from_calls());
    dirty(); printf("%lu\n", inside_a_struct('q'));
    dirty(); printf("%lu\n", one_element(1234));
    dirty(); printf("%lu\n", literal_over_itself());
    dirty(); printf("%lu\n", every_time(5));
    dirty(); printf("%lu\n", keeps_arguments(1, 2, 3, 1.5, 5, 6, 7, 8, 9));
    return 0;
}
