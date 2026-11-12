/* Struct layout rules under #pragma pack and packed/aligned attributes,
 * found by a layout fuzzer (clang and GCC agree on all of them):
 * - under any #pragma pack(N), not only pack(1), bit-fields are laid out
 *   back to back with no padding to a storage unit boundary;
 * - under a pragma a named bit-field raises the struct's alignment to
 *   min(alignof(type), N), even in a packed struct;
 * - aligned(k) on a member of a packed struct raises its alignment from
 *   1, not from its natural alignment; and #pragma pack caps even an
 *   explicit aligned attribute;
 * - a bit-field with its own packed attribute is placed at the next bit. */
#include <stdio.h>
#include <string.h>
#include <stddef.h>

#pragma pack(push, 4)
struct A { char c; int f : 29; } __attribute__((packed));
struct B { char c; int f : 20; char d; } __attribute__((packed));
struct C { char c; int f : 29 __attribute__((packed)); };
#pragma pack(pop)
#pragma pack(push, 8)
struct D { char f2 : 4; long long f3 : 62; };
struct E { int f4 __attribute__((aligned(16))); char g; };
#pragma pack(pop)
#pragma pack(2)
struct F { unsigned long f5 : 59; unsigned long long f6 : 57; };
struct G { long f2 __attribute__((aligned(1))); char c; } __attribute__((aligned(2)));
#pragma pack()
struct I { char c; long long f2 __attribute__((aligned(2))); } __attribute__((packed));
struct J { char c; unsigned f : 20 __attribute__((packed)); unsigned g : 20; };

#define L(T) printf(#T " %zu %zu\n", sizeof(struct T), _Alignof(struct T))

static void bits(const char *n, const void *p, unsigned size)
{
    const unsigned char *b = p;
    printf("%s:", n);
    for (unsigned i = 0; i < size; i++)
        printf(" %02x", b[i]);
    printf("\n");
}

int main(void)
{
    L(A); L(B); L(C); L(D); L(E); L(F); L(G); L(I); L(J);
    printf("%zu %zu %zu\n", offsetof(struct B, d), offsetof(struct E, g), offsetof(struct I, f2));
    struct D d; memset(&d, 0, sizeof d); d.f2 = -3; d.f3 = -0x123456789abcdll;
    bits("D", &d, sizeof d);
    printf("%d %lld\n", d.f2, (long long)d.f3);
    struct F f; memset(&f, 0, sizeof f); f.f5 = 0x7123456789abcdeul; f.f6 = 0x1fedcba987654ull;
    bits("F", &f, sizeof f);
    struct B b; memset(&b, 0, sizeof b); b.c = 1; b.f = -77777; b.d = 9;
    bits("B", &b, sizeof b);
    struct J j; memset(&j, 0, sizeof j); j.c = 2; j.f = 0xabcde; j.g = 0x12345;
    bits("J", &j, sizeof j);
    static struct A sa[2] = { { 1, 2 }, { 3, -4 } };
    bits("sa", sa, sizeof sa);
    printf("%d %d\n", sa[1].c, sa[1].f);
    return 0;
}
