/* Wide string literals (L, u, U) in initializers, found by a literal fuzzer:
 * - outside a whole one-dimensional array (rows of a two-dimensional array,
 *   struct and union members, flexible array members, compound literals) a
 *   wide literal was stored as its address, or not at all;
 * - u"" did not encode a character above U+FFFF as a surrogate pair, and
 *   u"\xd800" became U+FFFD;
 * - a \u escape in a plain string was the byte of its value, not UTF-8;
 * - for plain strings too: [i].field = "s" in an array of structs, a braced
 *   row {"cd"} of a local char[][n], a braced row after a designator that
 *   moved back, and (char[6]){"a"}, whose tail was left uninitialized. */
#include <stdio.h>
#include <wchar.h>
typedef __CHAR16_TYPE__ char16_t;
typedef __CHAR32_TYPE__ char32_t;

#define D(x) do { const unsigned char *p_ = (const unsigned char *)&(x); printf("%s", #x); \
  for (unsigned i_ = 0; i_ < sizeof(x); i_++) printf(" %x", p_[i_]); printf("\n"); } while (0)

struct W { short a; wchar_t w[3]; char16_t c[3]; char32_t u[2]; char n[3]; };
struct N { int k; struct W in; wchar_t t[2][3]; };

static char16_t g1[2][3] = {u"ab", u"c"};
static char16_t g2[2][3] = {{u"ab"}, {u"c"}};
static wchar_t g3[2][3] = {[1] = L"xy"};
static wchar_t g4[][4] = {L"abc", L"d", {L"ef"}};
static struct W g5 = {1, L"ab", u"cd", U"e", "fg"};
static struct W g6 = {.c = u"xy", .w = L"z", .n = "q"};
static struct W g7[2] = {{1, L"a", u"b", U"c", "d"}, [1].w = L"zz", [1].c = u"\U0001F600"};
static struct N g8 = {7, {2, L"ab", u"c"}, {L"de", L"f"}};
static struct N g9 = {.in.w = L"q", .t[1] = L"rs"};
static struct W g10 = {1, {L"ab"}, {u"cd"}};
static char32_t g11[2][2] = {U"a", U"bc"};
static const wchar_t *g12[] = {L"p", L"qr"};
static struct { const char16_t *p; wchar_t w[2]; } g13 = {u"pq", L"r"};
static union { wchar_t w[2]; int i; } g14 = {L"a"};
static struct { int a; wchar_t w[]; } g15 = {1, L"flex"};

__attribute__((noinline)) static void locals(void) {
  char16_t l1[2][3] = {u"ab", u"c"};
  char16_t l2[2][3] = {{u"ab"}, {u"c"}};
  wchar_t l3[2][3] = {[1] = L"xy"};
  wchar_t l4[][4] = {L"abc", L"d", {L"ef"}};
  struct W l5 = {1, L"ab", u"cd", U"e", "fg"};
  struct W l6 = {.c = u"xy", .w = L"z", .n = "q"};
  struct W l7[2] = {{1, L"a", u"b", U"c", "d"}, [1].w = L"zz", [1].c = u"\U0001F600"};
  struct N l8 = {7, {2, L"ab", u"c"}, {L"de", L"f"}};
  struct N l9 = {.in.w = L"q", .t[1] = L"rs"};
  struct W l10 = {1, {L"ab"}, {u"cd"}};
  char32_t l11[2][2] = {U"a", U"bc"};
  union { wchar_t w[2]; int i; } l14 = {L"a"};
  struct W l16 = (struct W){3, L"cl", u"cm"};
  wchar_t *l17 = (wchar_t[]){L"lit"};
  D(l1); D(l2); D(l3); D(l4); D(l5); D(l6); D(l7); D(l8); D(l9); D(l10); D(l11); D(l14); D(l16);
  printf("l17 %x %x %x\n", l17[0], l17[2], l17[3]);
}

static char16_t u1[] = u"a\U0001F600\xd800€";
static char n1[] = "é\xe9\U0001F600";
static char n2[][6] = {[3] = "x", [0] = "ab", {"cd"}};
static struct W n3[2] = {[1].n = "q", [1].a = 5};

__attribute__((noinline)) static void more(void) {
  char16_t lu[] = u"\U0001F600\xdc00";
  char ln[2][3] = {{"ab"}, {"c"}};
  unsigned char *lc = (unsigned char[6]){"\7"};
  wchar_t lw[][3] = {[2] = L"x", [0] = L"yz", {L"w"}};
  D(lu); D(ln); D(lw);
  printf("lc %x %x %x\n", lc[0], lc[1], lc[5]);
}

int main(void) {
  D(u1); D(n1); D(n2); D(n3);
  more();
  D(g1); D(g2); D(g3); D(g4); D(g5); D(g6); D(g7); D(g8); D(g9); D(g10); D(g11); D(g13.w); D(g14);
  printf("g12 %x %x\n", g12[1][1], g13.p[1]);
  printf("g15 %x %x\n", g15.w[0], g15.w[3]);
  locals();
  return 0;
}
