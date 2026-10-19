/* Darwin arm64 data model: plain char is signed, long double is double,
 * int64_t is long long, wchar_t is int. */
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <wchar.h>
#include <float.h>

int main(void) {
    printf("char %zu short %zu int %zu long %zu llong %zu ptr %zu\n",
           sizeof(char), sizeof(short), sizeof(int), sizeof(long), sizeof(long long), sizeof(void *));
    printf("float %zu double %zu ldouble %zu\n", sizeof(float), sizeof(double), sizeof(long double));
    printf("wchar %zu size_t %zu ptrdiff %zu intmax %zu\n",
           sizeof(wchar_t), sizeof(size_t), sizeof(ptrdiff_t), sizeof(intmax_t));
    char c = (char)200;
    printf("char signed %d CHAR_MIN %d\n", c < 0, CHAR_MIN);
    printf("LDBL_MANT_DIG %d DBL_MANT_DIG %d\n", LDBL_MANT_DIG, DBL_MANT_DIG);
    long double x = 1.0L / 3.0L;
    printf("%.17Lg\n", x);
    printf("_Alignof(long double) %zu _Alignof(long long) %zu\n", _Alignof(long double), _Alignof(long long));
    struct { char c; long double d; } s;
    printf("offsetof ld %zu\n", (size_t)((char *)&s.d - (char *)&s));
    printf("__INT64_TYPE__ is long long: %d\n", _Generic((int64_t)0, long long: 1, default: 0));
    printf("__APPLE__ %d\n",
#ifdef __APPLE__
           1
#else
           0
#endif
    );
    return 0;
}
