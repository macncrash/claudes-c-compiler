/* __builtin_types_compatible_p tells pointers to const from pointers to
 * non-const by their base type. ccc read that only from a const written in
 * the type name, so a const typedef (CI * for typedef const int CI) and
 * typeof(expr) of a pointer to const (typeof(&c) for const int c) compared
 * as pointers to non-const. A string literal is an array: typeof("xy") is
 * char[3], which sizeof and __builtin_types_compatible_p took as char *. */
#include <stdio.h>
#include <wchar.h>
#define C(a, b) __builtin_types_compatible_p(a, b)

typedef const int CI;
typedef const char *CS;
typedef int *IP;
const int c = 1;
const int *cp;
int *p;
const char *s;
struct T { const int m; int *q; } t;
const char *fr(void) { return "r"; }
int *fi(void) { return 0; }

static int g_size = sizeof(typeof("xy"));
static int g_compat = C(typeof(&c), const int *);
static int g_arr[C(CI *, int *) ? 2 : 3];

int main(void)
{
    const char *lc = "q";
    int li = 0;
    printf("typedef %d %d %d %d %d\n", C(CI *, const int *), C(CI *, int *),
           C(CS, const char *), C(CS, char *), C(const IP, int *));
    printf("typeof %d %d %d %d %d %d\n", C(typeof(&c), const int *), C(typeof(&c), int *),
           C(typeof(cp), const int *), C(typeof(cp), int *), C(typeof(s), const char *),
           C(typeof(p), int *));
    printf("member %d %d %d\n", C(typeof(&t.m), const int *), C(typeof(&t.m), int *),
           C(typeof(t.q), int *));
    printf("call %d %d %d\n", C(typeof(fr()), const char *), C(typeof(fr()), char *),
           C(typeof(fi()), int *));
    printf("local %d %d %d\n", C(typeof(lc), const char *), C(typeof(&li), int *),
           C(typeof(&li), const int *));
    printf("deref %d %d %d\n", C(typeof(&*cp), int *), C(typeof(&cp[0]), const int *),
           C(typeof(&c) *, const int **));
    printf("literal %zu %zu %zu %d %d %d\n", sizeof(typeof("xy")), sizeof(typeof(L"xy")),
           sizeof(typeof(u8"abc")), C(typeof("xy"), char[3]), C(typeof(L"xy"), wchar_t[3]),
           C(typeof("xy"), char *));
    typeof("hello") arr;
    printf("decl %zu %d\n", sizeof arr, C(typeof(arr), char[6]));
    printf("static %d %d %zu\n", g_size, g_compat, sizeof g_arr / sizeof g_arr[0]);
    return 0;
}
