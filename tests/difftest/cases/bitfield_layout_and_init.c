/* Bit-field placement, struct size and alignment, and static initializers:
 * a field that would straddle moves to its own type's next boundary; zero-width
 * and unnamed bit-fields align what follows without aligning the struct; the
 * size is the bits used; a packed field may span 9 bytes; and a _Bool field
 * holds 0 or 1, also in structs that hold a pointer. */
#include <stdio.h>
#include <string.h>

static int anchor;

struct straddle { long a : 14; short b : 16; };
struct widths { _Bool a : 1; int b : 9; unsigned short c : 16; };
struct zero_char { short a : 4; char : 0; long b : 14; int c : 17; };
struct zero_first { long long : 0; int x; };
struct zero_mid { signed char a : 6; unsigned : 0; signed char b : 4; };
struct __attribute__((packed)) zero_packed { long long a : 21; long long : 0; unsigned short b : 16; };
struct unnamed_tail { unsigned a : 10; long long : 10; };
struct unnamed_long { char c; long long : 60; char d; };
struct unnamed_only { int : 4; };
#pragma pack(2)
struct pack2 { int a : 4; };
struct pack2_mix { char c; long long : 30; int x : 3; };
#pragma pack()
struct __attribute__((packed)) span9 { char c : 3; unsigned long long x : 64; };
struct with_ptr { _Bool f : 1; char : 0; const void *p; long g : 14; _Bool h : 1; };

static struct straddle g_straddle = { -5, -1 };
static struct widths g_widths = { 7, -3, 0x1234 };
static struct zero_char g_zero_char = { -1, 0x1fff, -65536 };
static struct zero_packed g_zero_packed = { -1, 0xfedc };
static struct span9 g_span9 = { 1, 0xfedcba9876543210ul };
static struct with_ptr g_with_ptr = { 2147483648ll, &anchor, 19353, 32768 };

static void dump(const char *tag, const void *p, unsigned long n)
{
    const unsigned char *b = p;
    printf("%-12s %2lu:", tag, n);
    for (unsigned long i = 0; i < n; i++)
        printf(" %02x", b[i]);
    printf("\n");
}

#define LAYOUT(T) printf("%-12s %zu %zu\n", #T, sizeof(struct T), _Alignof(struct T))

int main(void)
{
    LAYOUT(straddle); LAYOUT(widths); LAYOUT(zero_char); LAYOUT(zero_first);
    LAYOUT(zero_mid); LAYOUT(zero_packed); LAYOUT(unnamed_tail); LAYOUT(unnamed_long);
    LAYOUT(unnamed_only); LAYOUT(pack2); LAYOUT(pack2_mix); LAYOUT(span9); LAYOUT(with_ptr);

    dump("straddle", &g_straddle, sizeof g_straddle);
    dump("widths", &g_widths, sizeof g_widths);
    dump("zero_char", &g_zero_char, sizeof g_zero_char);
    dump("zero_packed", &g_zero_packed, sizeof g_zero_packed);
    dump("span9", &g_span9, sizeof g_span9);
    struct with_ptr w;
    memcpy(&w, &g_with_ptr, sizeof w);
    w.p = 0;
    dump("with_ptr", &w, sizeof w);
    printf("%d %d %ld %d\n", g_with_ptr.p == &anchor, g_with_ptr.f, (long)g_with_ptr.g, g_with_ptr.h);

    /* The same fields written and read at run time. */
    volatile long long v = -2;
    volatile unsigned long long u = 0xfedcba9876543210ul;
    struct straddle s; memset(&s, 0, sizeof s); s.b = v; s.a = v * 3;
    struct zero_char z; memset(&z, 0, sizeof z); z.b = v; z.c = v - 7; z.a = 5;
    struct span9 p; memset(&p, 0, sizeof p); p.x = u; p.c = 2;
    dump("run straddle", &s, sizeof s);
    dump("run zero", &z, sizeof z);
    dump("run span9", &p, sizeof p);
    printf("%ld %d %llx\n", (long)s.a, s.b, (unsigned long long)p.x);
    return 0;
}
