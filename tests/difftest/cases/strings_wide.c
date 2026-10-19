/* String literals, escapes, wide/UTF strings, adjacent concatenation. */
#include <stdio.h>
#include <string.h>
#include <wchar.h>

int main(void) {
    const char *s = "tab\there\nnew \"quoted\" \x41\102 " "concat";
    printf("%s|%zu\n", s, strlen(s));
    const wchar_t *w = L"wide é";
    printf("%zu %d\n", wcslen(w), (int)w[5]);
    const __CHAR16_TYPE__ *u16 = u"utf16";
    const __CHAR32_TYPE__ *u32 = U"utf32";
    printf("%d %d %zu %zu\n", u16[0], (int)u32[4], sizeof(*u16), sizeof(*u32));
    const char *u8 = u8"é";
    printf("%zu\n", strlen(u8));
    char arr[] = "abc";
    printf("%zu %d\n", sizeof arr, arr[3]);
    printf("%s\n", __func__);
    return 0;
}
