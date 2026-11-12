/* String and character literal bugs found by a literal fuzzer:
 * - hex and octal escapes in L, u and U literals and character constants
 *   were truncated to a byte (L"\x123" gave 0x23, U"\x1f600" gave 0);
 * - an unprefixed piece concatenated to a wide literal kept its escapes as
 *   bytes and its non-ASCII characters as UTF-8 bytes (L"" "é" was two
 *   wchar_t), though the whole literal takes the prefix (C11 6.4.5p5);
 * - char a[][6] = { "ab" } got one row per character of the string;
 * - a local wchar_t array longer than its literal kept stack garbage after
 *   the terminator, and an exact-fit one got a terminator written past it;
 * - sizeof u'x' was 4 (u'x' is a char16_t). */
#include <stdio.h>
#include <wchar.h>

/* #if evaluates character constants as the compiler does. */
#if L'\400' == 256 && u'\xffff' == 65535 && '\377' < 0 && 'ab' == 0x6162 && U'\x1f600' == 0x1f600 && '\n' == 10
#define PP 1
#else
#define PP 0
#endif

typedef unsigned short c16;
typedef unsigned int c32;

wchar_t ga[] = L"\x123\x10ffff\777\1";
c16 gb[] = u"\xabcd\x1" "\x12";
c32 gc[] = U"\x1f600" "\U0001F601";
wchar_t gd[] = L"é" "é" "\xe9" "\x100";
wchar_t ge[] = L"" "\xc3\xa9";
c16 gf[] = u"" "€";
char g2[][6] = { "ab" };
char g3[][4] = { "xyz", "" };

static void dw(const char *n, const wchar_t *p, unsigned k)
{
    printf("%s %u:", n, k);
    for (unsigned i = 0; i < k; i++)
        printf(" %x", (unsigned)p[i]);
    printf("\n");
}

static void d16(const char *n, const c16 *p, unsigned k)
{
    printf("%s %u:", n, k);
    for (unsigned i = 0; i < k; i++)
        printf(" %x", p[i]);
    printf("\n");
}

__attribute__((noinline)) void locals(void)
{
    struct { wchar_t w[6]; int guard; } s = { { 0 }, 0x5a5a };
    wchar_t lw[6] = L"z\r";
    wchar_t exact[2] = L"ab";
    struct { c16 u[2]; short guard; } t;
    t.guard = 0x77;
    c16 lu[5] = u"xy" "\x1234";
    char l2[][5] = { "ab" };
    wchar_t lcat[] = L"a" "\x2603";
    dw("lw", lw, 6);
    dw("exact", exact, 2);
    d16("lu", lu, 5);
    dw("lcat", lcat, sizeof lcat / sizeof lcat[0]);
    printf("l2 %zu %d %d\n", sizeof l2, l2[0][1], l2[0][4]);
    printf("guards %x %x %x\n", s.guard, (unsigned)s.w[5], t.guard);
}

int main(void)
{
    dw("ga", ga, sizeof ga / sizeof ga[0]);
    d16("gb", gb, sizeof gb / sizeof gb[0]);
    printf("gc %zu: %x %x\n", sizeof gc, gc[0], gc[1]);
    dw("gd", gd, sizeof gd / sizeof gd[0]);
    dw("ge", ge, sizeof ge / sizeof ge[0]);
    d16("gf", gf, sizeof gf / sizeof gf[0]);
    printf("g2 %zu g3 %zu %s %d\n", sizeof g2, sizeof g3, g3[0], g3[1][0]);
    wchar_t wc = L'\x123';
    c16 uc = u'\xabcd';
    c32 Uc = U'\x1f600';
    printf("%x %x %x %zu %zu %zu\n", (unsigned)wc, uc, Uc, sizeof u'x', sizeof U'x', sizeof L'x');
    printf("pp %d\n", PP);
    locals();
    return 0;
}
