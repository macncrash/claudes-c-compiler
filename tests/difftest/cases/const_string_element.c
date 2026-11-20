/* A character of a string literal at a constant index is folded in static
 * initializers, as gcc and clang do: "abc"[1], 1["pq"], *"zz",
 * *("xyz" + 3 - 1), L"wide"[3], u"h16"[0], U"first"[0]. ccc evaluated none
 * of them and silently stored 0. Plain char keeps the target's signedness
 * ("\xff"[0] is -1 where char is signed, 255 where it is not). */
#include <stdio.h>
typedef __CHAR16_TYPE__ c16;
typedef __CHAR32_TYPE__ c32;

static char a = "abc"[1];
static int b = "abc"[2] + 1;
static int c = L"wide"[3];
static unsigned d = U"x32"[1];
static unsigned short e = u"h16"[0];
static int f[] = { "xy"[0], 1["pq"], *"zz" };
static char g = *("abc" + 2);
static int h = sizeof("abc"[0]);
static int hi = "\xff"[0];
static unsigned hu = (unsigned char)"\xfe"[0];
static int nul = "ab"[2];
static int u16max = u"\xffff"[0];
static int off = *("xyz" + 3 - 1);
static int neg = -"5"[0];
static char arr[] = { "ab"[1], "cd"[0], 0 };
static int cmp = "abc"[1] == 98;
static c32 rows[][5] = { [3] = U"last", [0] = U"first"[0] };
static const char *p = "pointer" + ("ab"[1] - 'a');

int main(void)
{
    int k = 2;
    printf("%d %d %d %u %u\n", a, b, c, d, e);
    printf("%d %d %d %d %d\n", f[0], f[1], f[2], g, h);
    printf("%d %u %d %d %d %d %s %d\n", hi == (char)0xff, hu, nul, u16max, off, neg, arr, cmp);
    for (int i = 0; i < 4; i++)
        printf("%x %x ", (unsigned)rows[i][0], (unsigned)rows[i][3]);
    printf("\n%s %c\n", p, "hello"[k]);
    return 0;
}
