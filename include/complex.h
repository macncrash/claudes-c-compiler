/* ccc's <complex.h>: the system's, plus C11's CMPLX, CMPLXF and CMPLXL
 * (7.3.9.3) where it defines them only for clang (Apple's SDK checks
 * __clang__). As in gcc, they are __builtin_complex, so they work in
 * static initializers and keep a NaN or infinite part as it is. */
#include_next <complex.h>

#ifndef CMPLX
#define CMPLX(x, y) __builtin_complex((double)(x), (double)(y))
#endif
#ifndef CMPLXF
#define CMPLXF(x, y) __builtin_complex((float)(x), (float)(y))
#endif
#ifndef CMPLXL
#define CMPLXL(x, y) __builtin_complex((long double)(x), (long double)(y))
#endif
