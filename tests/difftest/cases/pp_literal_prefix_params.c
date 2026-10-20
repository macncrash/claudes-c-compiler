/* Macro parameters named like string/char encoding prefixes (L, u, U, u8)
 * must not be substituted into the prefix of a literal in the body:
 * L'1' and u8"x" are single tokens. */
#include <stdio.h>
#include <wchar.h>

#define WCH(L) (L'1' + (L))
#define WSTR(L) (L"ab"[L])
#define U16(u) (u'x' + (u))
#define U32(U) (U'y' + (U))
#define U8S(u8) (u8"hi"[u8])
#define BOTH(L, u) (L'a' + u'b' + (L) + (u))
#define NUM(L) (10L + (L))

int main(void) {
    printf("%d\n", (int)WCH(0));
    printf("%d\n", (int)WCH(2));
    printf("%d\n", (int)WSTR(1));
    printf("%d\n", (int)U16(3));
    printf("%d\n", (int)U32(4));
    printf("%d\n", (int)U8S(1));
    printf("%d\n", (int)BOTH(5, 6));
    printf("%ld\n", NUM(7));
    printf("%d\n", WCH(0) != L'1');
    return 0;
}
