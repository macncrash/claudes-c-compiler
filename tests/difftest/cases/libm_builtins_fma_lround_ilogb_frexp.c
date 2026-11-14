/* __builtin_fma, __builtin_lround, llround, lrint, llrint, ilogb (each with
 * its f and l forms), __builtin_frexp and __builtin_modf: clang and gcc know
 * them, ccc called them as undeclared functions, and the link failed.
 * Values, result types (an integer for lround and ilogb) and sizes. Found by
 * the math function fuzzer. */
#include <stdio.h>
#define T(e) _Generic((e), int: "int", long: "long", long long: "llong", float: "float", double: "double", long double: "ldouble", default: "?")
static volatile double V = 2.5;
static volatile float VF = -3.5f;
int main(void) {
  double x = V; float f = VF; long double ld = V;
  printf("%a %a %La\n", __builtin_fma(x, 3.0, 0.25), (double)__builtin_fmaf(f, 2.0f, 1.0f), __builtin_fmal(ld, ld, ld));
  printf("%ld %ld %lld %lld %ld %lld\n", __builtin_lround(x), __builtin_lroundf(f), __builtin_llround(-x), __builtin_llroundl(ld),
         __builtin_lrint(x), __builtin_llrintf(f));
  printf("%d %d %d\n", __builtin_ilogb(x * 1000), __builtin_ilogbf(f), __builtin_ilogbl(ld / 1024));
  int e = 0; double m = __builtin_frexp(x * 3, &e); printf("%a %d\n", m, e);
  int e2 = 0; float mf = __builtin_frexpf(f, &e2); printf("%a %d\n", (double)mf, e2);
  double ip = 0; double fr = __builtin_modf(-x * 3.25, &ip); printf("%a %a\n", fr, ip);
  float ipf = 0; float frf = __builtin_modff(f, &ipf); printf("%a %a\n", (double)frf, (double)ipf);
  printf("%s %s %s %s %s %s %s\n", T(__builtin_lround(1.5)), T(__builtin_lroundf(1.5f)), T(__builtin_llrintl(1.5L)), T(__builtin_ilogbf(1.f)),
         T(__builtin_fmaf(1, 2, 3)), T(__builtin_frexpl(1.5L, &e)), T(__builtin_modf(1.5, &ip)));
  printf("%zu %zu %zu %zu\n", sizeof __builtin_lround(1.5), sizeof __builtin_ilogb(1.0), sizeof __builtin_fmaf(1, 2, 3), sizeof(__typeof__(__builtin_llround(2.0))));
  printf("%a %ld %d\n", __builtin_fma(1e308, 10.0, -1e308), __builtin_lround(2.5), __builtin_ilogb(0x1p-1074));
  __typeof__(__builtin_lroundf(1.0f)) z = 1L << 40; printf("%ld\n", z);
  return 0;
}
