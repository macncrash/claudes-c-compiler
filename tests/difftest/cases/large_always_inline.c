/* always_inline bodies too large for the ordinary limits: flags constant at each
 * call, wrappers around them, chains of them, and the ones that cannot be
 * expanded. Each body is well over 500 instructions. */
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>

#define FORCE static inline __attribute__((always_inline))
#define NI __attribute__((noinline))

#define MIX1(h, x) h = (h ^ (uint32_t)(x)) * 16777619u; h ^= h >> 13
#define MIX4(h, x) MIX1(h, x); MIX1(h, (x) + 1); MIX1(h, (x) + 2); MIX1(h, (x) + 3)
#define MIX16(h, x) MIX4(h, x); MIX4(h, (x) + 4); MIX4(h, (x) + 8); MIX4(h, (x) + 12)
#define MIX64(h, x) MIX16(h, x); MIX16(h, (x) + 16); MIX16(h, (x) + 32); MIX16(h, (x) + 48)
#define MIX256(h, x) MIX64(h, x); MIX64(h, (x) + 64); MIX64(h, (x) + 128); MIX64(h, (x) + 192)

typedef struct { int off; int len; } match_t;

/* Flags tested in the loop, a struct returned, a local array. */
FORCE match_t finder(const uint8_t *p, int n, int pattern, int swap, int reverse)
{
    match_t m = { 0, 0 };
    uint32_t h = 2166136261u;
    uint8_t seen[8] = { 0 };
    for (int i = 0; i < n; i++) {
        int k = reverse ? n - 1 - i : i;
        if (pattern && p[k] == 'x')
            m.len++;
        if (swap && k > 0 && p[k] < p[k - 1])
            m.off += k;
        seen[p[k] & 7]++;
        MIX1(h, p[k]);
    }
    MIX64(h, m.len);
    MIX64(h, seen[3]);
    m.off ^= (int)(h & 0xff);
    return m;
}

/* A chain: this one calls the one above from three places. */
FORCE int chain(const uint8_t *p, int n, int level)
{
    match_t a = finder(p, n, 1, 0, 0);
    match_t b = finder(p, n / 2, level > 1, 1, 0);
    match_t c = finder(p + 1, n - 1, 0, level > 2, 1);
    uint32_t h = (uint32_t)(a.off + b.len * 3 + c.off * 7);
    MIX64(h, a.len);
    return (int)(h >> 4);
}

/* A small one between: it has to carry the large ones along. */
FORCE int small_step(const uint8_t *p, int n, int level)
{
    return chain(p, n, level) + 1;
}

/* An ordinary wrapper, and wrappers of that, called from many places. */
static int wrapper(const uint8_t *p, int n, int level)
{
    return small_step(p, n, level);
}

int entry_a(const uint8_t *p, int n) { return wrapper(p, n, 1); }
int entry_b(const uint8_t *p, int n) { return wrapper(p, n, 2); }
static int entry_c(const uint8_t *p, int n) { return wrapper(p, n, 3) ^ wrapper(p, n, 1); }

/* The same static in every copy. */
FORCE int counted(int x)
{
    static int calls;
    uint32_t h = (uint32_t)x;
    MIX64(h, x);
    MIX64(h, x + 1);
    calls++;
    return calls * 1000 + (int)(h & 7);
}

/* Each copy its own locals, whose addresses are taken. */
FORCE int *keep_local(int *out, int x)
{
    int local[4] = { x, x + 1, x + 2, x + 3 };
    uint32_t h = (uint32_t)x;
    int *q = local + (x & 3);
    MIX64(h, *q);
    MIX64(h, local[1]);
    *out = *q + (int)(h & 3);
    return out;
}

/* Left as a function: its address taken as well as being called. */
FORCE int by_address(int x)
{
    uint32_t h = (uint32_t)x;
    MIX64(h, x);
    MIX64(h, x * 3);
    return (int)(h % 1000);
}

/* Those that cannot be expanded. Recursive: */
FORCE int recursive(int n)
{
    uint32_t h = (uint32_t)n;
    MIX64(h, n);
    MIX64(h, n + 7);
    return n <= 0 ? (int)(h & 15) : (int)(h & 15) + recursive(n - 1);
}

/* Calling each other: */
static int mutual_b(int n);
FORCE int mutual_a(int n)
{
    uint32_t h = (uint32_t)n;
    MIX64(h, n);
    MIX64(h, n + 2);
    return n <= 0 ? 1 : (int)(h & 3) + mutual_b(n - 1);
}
static int mutual_b(int n) { return mutual_a(n) + 1; }

/* Variable arguments: */
FORCE int varargs(int n, ...)
{
    va_list ap;
    uint32_t h = (uint32_t)n;
    va_start(ap, n);
    for (int i = 0; i < n; i++)
        h += (uint32_t)va_arg(ap, int);
    va_end(ap);
    MIX64(h, n);
    MIX64(h, n + 1);
    return (int)(h & 255);
}

/* A variable-length array: */
FORCE int vla(int n)
{
    int a[n];
    uint32_t h = 1;
    for (int i = 0; i < n; i++)
        a[i] = i * i;
    MIX64(h, a[n - 1]);
    MIX64(h, a[n / 2]);
    return (int)(h & 1023);
}

/* Past what one caller may take, so a call after all. */
FORCE int over_budget(int x)
{
    uint32_t h = (uint32_t)x;
    MIX256(h, x);
    MIX256(h, x + 1);
    MIX256(h, x + 2);
    MIX256(h, x + 3);
    MIX256(h, x + 4);
    MIX256(h, x + 5);
    MIX256(h, x + 6);
    MIX256(h, x + 7);
    return (int)(h & 0xffff);
}

/* Inside a loop, with the loop's variable as the flag. */
static NI int in_a_loop(const uint8_t *p, int n)
{
    int t = 0;
    for (int f = 0; f < 3; f++) {
        match_t m = finder(p, n, f & 1, f >> 1, 0);
        t = t * 31 + m.off + m.len;
    }
    return t;
}

int main(void)
{
    const uint8_t *s = (const uint8_t *)"xaxbxcxdzyxwvutsrqponmlkjihgfedcba0123456789";
    int n = (int)strlen((const char *)s);
    int (*fp)(int) = by_address;
    int a, b;

    printf("%d %d %d\n", entry_a(s, n), entry_b(s, n), entry_c(s, n));
    printf("%d\n", in_a_loop(s, n));
    printf("%d %d %d\n", counted(1), counted(2), counted(3));
    printf("%d %d %d\n", *keep_local(&a, 5), *keep_local(&b, 6), a + b);
    printf("%d %d\n", fp(4), by_address(4));
    printf("%d %d\n", recursive(3), mutual_a(4));
    printf("%d %d %d\n", varargs(3, 1, 2, 3), vla(5), vla(9));
    printf("%d %d\n", over_budget(1), over_budget(2));
    return 0;
}
