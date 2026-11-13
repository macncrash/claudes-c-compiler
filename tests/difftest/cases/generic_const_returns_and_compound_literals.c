/* const in _Generic and typeof, found by a _Generic fuzzer:
 * - a call to a function returning a pointer to const (directly, through
 *   (*f)(), or through a function pointer variable) matched the unqualified
 *   pointer association;
 * - an array compound literal of const elements ((const int[]){1}, also by
 *   a const typedef) decayed to a pointer to non-const, and &(const int){1}
 *   was int *;
 * - typeof((*f)(x)) for a function f could not be resolved and became int,
 *   so the variable had the wrong size and value. */
#include <stdio.h>

#define G(e) _Generic((e), int *: "int*", const int *: "cint*", char *: "char*", const char *: "cchar*", \
  int **: "int**", int *const *: "intc*", const int *const *: "cintc*", const int **: "cint**", \
  int (*)[2]: "int(*)[2]", const int (*)[2]: "cint(*)[2]", struct S *: "S*", const struct S *: "cS*", \
  void *: "void*", const void *: "cvoid*", int: "int", default: "other")
#define K(e) _Generic((e), char *: 1.5, const char *: (short)7, default: 'z')
#define T(...) printf("%-28s %-10s %zu\n", #__VA_ARGS__, G((__VA_ARGS__)), sizeof(__VA_ARGS__))

struct S { int a; const char *p; int arr[2]; };
typedef const int CI;
typedef int IA2[2];
typedef const IA2 CIA2;
typedef CI *PCI;

static const char *fcs(void) { return "xyz"; }
static const int *fci(void) { static int k = 7; return &k; }
static const int *const *fcc(void) { return 0; }
static int *fi(void) { return 0; }
static const int (*fca(void))[2] { return 0; }
static const struct S *fcsp(void) { static struct S s = {4, "q", {5, 6}}; return &s; }
static const void *fcv(void) { return 0; }
static PCI fpci(void) { return 0; }
static CI *fcit(void) { return 0; }
const char *fext(void);
const char *fext(void) { return "e"; }
static double fd(int x) { return x * 1.5; }
static struct S fs(void) { struct S s = {1, "s", {2, 3}}; return s; }

int main(void) {
  int i = 1, ia[2] = {0};
  const char *(*fp)(void) = fcs;
  const int *(**fpp)(void) = 0;
  int *(*fpi)(void) = fi;
  T(fcs()); T(fci()); T(fcc()); T(*fcc()); T(fi()); T(fca()); T(*fca()); T(*fca() + 1);
  T(fcsp()); T(fcsp()->arr); T(&fcsp()->a); T(fcsp()->p); T(fcv()); T(fpci()); T(fcit()); T(fext());
  T((*fcs)()); T((**fci)()); T(fp()); T((*fp)()); T(fpi()); T((1, fcs()));
  T(i ? fci() : ia); T(i ? ia : fci()); T(i ? fcv() : ia); T(fci() + i);
  if (fpp) T((*fpp)());
  T((const int[]){1, 2}); T((const int[2]){1}); T((CI[]){3}); T((CIA2){1, 2}); T((const int [][2]){{1}});
  T((const char[]){"ab"}); T((const struct S){0}.arr); T((const struct S){0}.p); T(&(const int){1});
  T(&(const struct S){0}); T(&(const struct S){0}.a); T((const int *[]){0}); T((int *const[]){0});
  T((const int *const[]){0}); T(&(CIA2){0}); T(i ? (const int[]){1} : ia); T((int[]){1});
  __typeof__((*fcs)()) p = fcs();
  __typeof__((*fd)(3)) d = (*fd)(3);
  __typeof__((**fd)(3)) d2 = 2.25;
  __typeof__((*fs)()) s = (*fs)();
  printf("%zu %c %zu %g %g %zu %d %d\n", sizeof p, p[1], sizeof d, d, d2, sizeof s, s.arr[1], *fci());
  printf("%zu %zu %zu\n", sizeof((*fcs)()), sizeof((*fd)(1)), sizeof((*fs)()));
  long k = K(fcs()) + K((const char[]){"a"}) + K(fp());
  double kd = K(fcs()) / 2;
  printf("%ld %g %zu %zu %d\n", k, kd, sizeof K(fcs()), sizeof(K(&(const char){1})), (int)(K(fp()) * 3));
  return 0;
}
