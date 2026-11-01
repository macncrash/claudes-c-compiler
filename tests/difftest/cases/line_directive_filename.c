/* #line and GNU linemarkers set __FILE__ to the whole string literal,
 * spaces and escape sequences included (ccc once cut it at the first
 * space: "a b.c" became "a"). */
#include <stdio.h>
#include <string.h>

static const char *spaced(void) {
#line 7 "a b.c"
    return __FILE__;
}

static int spaced_line(void) {
    return __LINE__;
}

static const char *escaped(void) {
#line 20 "dir\\with \"quote\".c"
    return __FILE__;
}

static const char *marker(void) {
# 30 "gnu marker.c" 1
    return __FILE__;
}

int main(void) {
    printf("%s|%d|%s|%s\n", spaced(), spaced_line(), escaped(), marker());
    printf("%zu %zu\n", strlen(escaped()), sizeof(__FILE__));
    return 0;
}
