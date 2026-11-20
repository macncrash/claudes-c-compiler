/* Designators inside a nested brace list of a multi-dimensional array index
 * the sub-array that list initializes (C11 6.7.9p17): in
 * { { "a" }, { [1] = "c" } } for char[2][3][2], [1] is a[1][1]. For a
 * local array ccc took it from the start of the whole array (a[0][1]), for
 * int as for strings; for a static one a lone designated string in braces,
 * { [1] = "c" }, was read as the brace-wrapped string { "c" } (a[1][0]).
 * A designated string that comes back to a row an earlier string filled
 * initializes the whole row ({ "abcd", [0] = "x" } leaves "x" and zeros);
 * a local char array kept the earlier string's tail.
 * Narrow, wide, char16_t and char32_t rows, static and automatic. */
#include <stdio.h>
#include <wchar.h>
typedef __CHAR16_TYPE__ c16;
typedef __CHAR32_TYPE__ c32;

static void dump(const char *n, const void *p, size_t sz, size_t unit)
{
    printf("%s:", n);
    for (size_t i = 0; i < sz; i += unit) {
        unsigned v = 0;
        for (size_t k = 0; k < unit; k++)
            v |= (unsigned)((const unsigned char *)p)[i + k] << (8 * k);
        printf(" %x", v);
    }
    printf("\n");
}
#define D(a) dump(#a, a, sizeof a, sizeof(a[0][0][0]))
#define D2(a) dump(#a, a, sizeof a, sizeof(a[0][0]))

static char g1[2][3][2] = { { "a" }, { [1] = "c" } };
static char g2[2][3][2] = { { "a" }, { "b", [2] = "c" } };
static char g3[2][3][2] = { [1] = { [1] = "c" } };
static char g4[2][3][2] = { { "a" }, { [1] = "c", "d" } };
static int g5[2][3][2] = { { { 1 } }, { [1] = { 3 } } };
static c16 g6[2][3][4] = { { u"a", u"bb" }, { [1] = u"ccc" } };
static wchar_t g7[2][3][4] = { { L"a" }, { [2] = L"zz" } };
static c32 g8[2][2][3] = { [1] = { [1] = U"qq" }, [0] = { U"r" } };
static char g9[3][2] = { "a", [2] = "c" };
static int g10[2][2][2][2] = { { { { 1 } } }, { [1] = { [1] = { 7, 8 } } } };

int main(void)
{
    D(g1); D(g2); D(g3); D(g4); D(g5); D(g6); D(g7); D(g8); D2(g9);
    dump("g10", g10, sizeof g10, sizeof(int));

    char l1[2][3][2] = { { "a" }, { [1] = "c" } };
    char l2[2][3][2] = { { "a" }, { "b", [2] = "c" } };
    char l3[2][3][2] = { [1] = { [1] = "c" } };
    char l4[2][3][2] = { { "a" }, { [1] = "c", "d" } };
    int l5[2][3][2] = { { { 1 } }, { [1] = { 3 } } };
    c16 l6[2][3][4] = { { u"a", u"bb" }, { [1] = u"ccc" } };
    wchar_t l7[2][3][4] = { { L"a" }, { [2] = L"zz" } };
    c32 l8[2][2][3] = { [1] = { [1] = U"qq" }, [0] = { U"r" } };
    char l9[3][2] = { "a", [2] = "c" };
    int l10[2][2][2][2] = { { { { 1 } } }, { [1] = { [1] = { 7, 8 } } } };
    int l11[2][3] = { 1, 2, [1] = { 5 }, 6 };
    D(l1); D(l2); D(l3); D(l4); D(l5); D(l6); D(l7); D(l8); D2(l9);
    dump("l10", l10, sizeof l10, sizeof(int));
    D2(l11);
    char o1[][5] = { "abcd", [0] = "x" };
    char o2[2][2][4] = { { "abc", "def" }, [0][1] = "z" };
    char o3[][7] = { [1] = u8"\x80", { u8"\u20ac\0" u8"\u00e9" u8"" }, [2] = u8"Z" };
    char o4[3][4] = { [1] = "abc", [0] = "d", "e" };
    c16 o5[][4] = { u"abc", [0] = u"q" };
    static char so1[][5] = { "abcd", [0] = "x" };
    D2(o1); D(o2); D2(o3); D2(o4); D2(o5); D2(so1);
    return 0;
}
