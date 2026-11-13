/* VLA parameters, VLA typedefs and typeof of type names, found by a second
 * VLA fuzzer:
 * - a VLA parameter dimension other than a plain name (c[][n + 1],
 *   q[][g(k)], (*r)[n * 2]) was taken as 1, so subscripts and pointer
 *   arithmetic on it stepped by the element size, and its size expression
 *   was not evaluated on entry;
 * - typeof of a VLA type name (typeof(Row) for a VLA typedef Row,
 *   typeof(int[n][m]), typeof(Row[2])) declared an array of a constant,
 *   wrong size;
 * - typeof(T[2]) for an array typedef T of constant size counted T's
 *   dimensions twice (typedef char T[3]: 18 bytes, not 6). */
#include <stdio.h>
#include <string.h>

#define P(e) printf("%d %s = %ld\n", __LINE__, #e, (long)(e))
#define OFF(x, b) ((long)((char *)(x) - (char *)(b)))
static int calls;
static int side(int x) { calls++; return x; }
struct S { short s; char c; };
typedef char G3[3];

__attribute__((noinline)) static void par(int n, int m, int k, char c[][n + 1], int q[][side(k)],
                                          int (*r)[n * 2], struct S (*s)[m][side(k) + 1]) {
  P(sizeof c[0]); P(OFF(c + 1, c)); P(OFF(&c[1][2], c)); P(c[1][2]);
  P(sizeof q[0]); P(OFF(q + 1, q)); P(q[1][1]); P(sizeof *r); P(OFF(r + 1, r)); P(r[1][3]);
  P(sizeof *s); P(sizeof (*s)[1]); P(OFF(s + 1, s)); P(OFF(&(*s)[1][2], s));
  __typeof__(c) c2 = c; P(OFF(c2 + 1, c2)); __typeof__(*s) sw; memset(sw, 0, sizeof sw); P(sizeof sw);
  P(calls);
}

__attribute__((noinline)) static void f(int n, int m, int k) {
  typedef int Row[m];
  typedef Row Mat[n];
  Row r; Mat mt;
  P(sizeof r); P(sizeof mt);
  __typeof__(Row) r2; memset(r2, 1, sizeof r2); P(sizeof r2);
  __typeof__(Mat[2]) m3; memset(m3, 2, sizeof m3); P(sizeof m3); P(sizeof m3[1]); P(OFF(&m3[1], m3));
  __typeof__(int[side(n)][m]) tt; memset(tt, 3, sizeof tt); P(sizeof tt); P(sizeof tt[1]); P(calls);
  typedef __typeof__(Row[3]) R3; R3 r3; P(sizeof r3); P(sizeof(R3));
  __typeof__(Row *) rp = mt; P(OFF(rp + 1, rp));
  (void)r; (void)mt;
  char cc[3][n + 1]; for (int i = 0; i < 3 * (n + 1); i++) ((char *)cc)[i] = (char)i;
  int qq[3][k]; for (int i = 0; i < 3 * k; i++) ((int *)qq)[i] = i;
  int rr[2][n * 2]; for (int i = 0; i < 4 * n; i++) ((int *)rr)[i] = i;
  struct S ss[m][k + 1];
  par(n, m, k, cc, qq, rr, &ss);
}

int main(void) {
  typedef unsigned char T1[2];
  typedef struct S T3[3];
  __typeof__(T1[2]) a; __typeof__(T3[2]) b; __typeof__(G3[5]) c; __typeof__(T1[2][4]) h;
  P(sizeof a); P(sizeof a[1]); P(sizeof b); P(sizeof c); P(sizeof h); P(sizeof h[1]);
  P(OFF(&b[1], b)); P(OFF(&h[1], h));
  f(3, 4, 2);
  f(2, 5, 3);
  return 0;
}
