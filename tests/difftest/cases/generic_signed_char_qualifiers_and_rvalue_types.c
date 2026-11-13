// _Generic and typeof: signed char is distinct from char, a pointer to an
// array is not a pointer to a pointer, const survives casts, pointer
// arithmetic and assignment, and operands of , and ?: are rvalues (arrays
// decay, a pointer's own const goes, narrow bit-fields promote). sizeof
// follows the same types.
#include <stdio.h>
#include <stdint.h>

#define T(e) _Generic((e), char: "char", signed char: "schar", unsigned char: "uchar", \
  int: "int", unsigned: "uint", long: "long", float: "float", double: "double", \
  char *: "char*", signed char *: "schar*", const signed char *: "cschar*", const char *: "cchar*", \
  int *: "int*", const int *: "cint*", int **: "int**", const int **: "cint**", int *const *: "intc*", \
  int (*)[3]: "int(*)[3]", const int (*)[3]: "cint(*)[3]", int (*)[4]: "int(*)[4]", \
  void *: "void*", const void *: "cvoid*", int (*)(void): "fn*", default: "other")
#define TQ(e) _Generic((__typeof__(e) *)0, int *: "int", const int *: "const int", \
  signed char *: "schar", const signed char *: "const schar", char *: "char", const char *: "const char", \
  double *: "double", int **: "int*", int *const *: "int*const", const int **: "const int*", \
  char (*)[4]: "char[4]", const char (*)[5]: "const char[5]", int (*)(void): "fn", default: "other")
#define P(e) printf("%-28s %-10s %2zu %s\n", #e, T(e), sizeof(e), TQ(e))

typedef const int CI;
typedef signed char SC;
typedef int *IP;
typedef const IP CIP;
struct S { unsigned u3 : 3; int s5 : 5; };
static int fi(void) { return 1; }

int main(int argc, char **argv) {
  (void)argv;
  int c = argc > 5, i = 2;
  signed char sc = 1; const signed char csc = 2; SC tsc = 3; int8_t i8 = 4;
  int a3[3] = {1, 2, 3}; const int ca3[3] = {4, 5, 6}; int m[2][3] = {{0}};
  int *ip = a3; const int *cip = ca3; int *const ipc = a3; CIP cipt = a3;
  _Complex double cxd = 1; struct S s = {5, -3};
  P(sc); P(csc); P(tsc); P(i8); P(&sc); P(&csc); P((signed char)i);
  P(&a3); P(&ca3); P(m); P(&m[0]); P(&a3[0]);
  P((const int *)ip); P((CI *)ip); P((const SC *)0); P((const int **)0);
  P(cip + 1); P(1 + cip); P(ca3 + 1); P(cip - 1); P(cip++); P(++cip); P(cip += 1); P(cip = ip);
  P((c, a3)); P((c, ca3)); P((c, "lit")); P((c, csc)); P((c, ipc)); P((c, fi));
  P(c ? a3 : 0); P(c ? ca3 : ip); P(c ? ipc : 0); P(c ? 0 : s.u3); P(c ? s.s5 : 1u);
  P(c ? csc : !cxd); P(__real__ cxd); P(!cxd);
  P((unsigned long long)c * 1.0f); P(i * 2.0); P(sc * (char)1);
  P("lit"); P(__func__); P(*fi); P(cipt); P((CIP)ip);
  return (int)(cip - ca3) + (int)sizeof(__func__);
}
