/* Variably modified types through typeof, casts and statement expressions,
 * found by a VLA sizeof/typeof fuzzer:
 * - typeof of an expression of VLA type (typeof(a), typeof(b[0]),
 *   typeof(*p)) declared an array of the wrong, constant size (memset
 *   through its sizeof overran it), also as T x[2], T *p and typedef T U;
 * - typeof of a pointer to a VLA (typeof(p), typeof(&a), typeof(a + 1),
 *   typeof(int (*)[n])) and __auto_type of one stepped by the pointer size;
 * - a cast to a pointer to a VLA type ((int (*)[n])p) subscripted, added to,
 *   or under sizeof stepped by the pointer size;
 * - a statement expression yielding an array had the array's type, not the
 *   decayed pointer's: sizeof ({ a; }) was the array's size, typeof(({ a; }))
 *   an array;
 * - typeof of an operand that is not variably modified evaluated the sizes
 *   of a cast in it, and sizeof of c ? x : y evaluated both arms. */
#include <stdio.h>
#include <string.h>

#define P(e) printf("%d %s = %ld\n", __LINE__, #e, (long)(e))
#define OFF(x, b) ((long)((char *)(x) - (char *)(b)))
static int calls;
static int side(int x) { calls++; return x; }
static long double buf[256];

__attribute__((noinline)) static void g(int n, int m, int p[][m], int (*q)[n][m]) {
  P(sizeof p[0]); P(sizeof *q); P(sizeof (*q)[1]);
  __typeof__(p) p2 = p; P(OFF(p2 + 1, p2)); P(sizeof *p2); P(p2[1][2]);
  __typeof__(*q) qa; memset(qa, 1, sizeof qa); P(sizeof qa); P(sizeof qa[0]); P(OFF(&qa[1], qa));
  __typeof__(p[0]) row; memset(row, 2, sizeof row); P(sizeof row);
}

__attribute__((noinline)) static void f(int n, int m) {
  int *ib = (int *)buf;
  for (int i = 0; i < 64; i++) ib[i] = i;
  void *vp = buf;
  P(((int (*)[n])vp)[1][2]); P(*(*((int (*)[m])vp + 1) + 2)); P(sizeof(((int (*)[n])vp)[0]));
  P(OFF((int (*)[n][m])vp + 1, vp)); P(sizeof *(1, (int (*)[m][3])vp)); P(sizeof *((char (*)[side(m)][n])vp));
  int (*cp)[m] = (int (*)[m])vp; P(cp[2][1]);
  int a[n][m];
  memcpy(a, buf, sizeof a);
  double b[n][m];
  memset(b, 0, sizeof b);
  P(sizeof (n ? a : a)); P(sizeof (0, a[1])); P(sizeof ({ a[1]; })); P(sizeof ({ a; }));
  P(((n ? a : a) + 1)[0][0]); P((0, a + 1)[0][0]); P(({ a + 1; })[0][1]); P((&a[1])[0][1]);
  __typeof__(a + 1) ap = a + 1; P(ap[0][1]); P(sizeof *ap);
  __typeof__(&a) pa = &a; P((*pa)[1][2]); P(sizeof *pa); P(OFF(pa + 1, pa));
  __typeof__(a) acopy; memcpy(acopy, a, sizeof acopy); P(acopy[n - 1][m - 1]); P(sizeof acopy);
  __typeof__(a[0]) r0; P(sizeof r0); r0[m - 1] = 9; P(r0[m - 1]);
  __typeof__(__typeof__(a[0]) *) rp = a; P(rp[1][1]); P(sizeof *rp);
  __typeof__(int (*)[m]) rq = a; P(rq[1][2]); P(OFF(rq + 1, rq));
  __typeof__(a) *pp = &a; P((*pp)[1][1]); P(sizeof **pp);
  __typeof__(a[0]) arr2[2]; P(sizeof arr2); P(sizeof arr2[1]); P(OFF(&arr2[1], &arr2[0]));
  typedef __typeof__(a) TA; TA ta; P(sizeof ta); P(sizeof(TA));
  __typeof__(b[0]) b0; P(sizeof b0); P(sizeof(__typeof__(b)));
  __typeof__(&b[0]) qb = &b[0]; P(sizeof *qb); P(OFF(qb + 1, qb));
  __auto_type au = a + 1; P(OFF(au + 1, au)); P(au[0][1]);
  __auto_type ac = (int (*)[n])vp; P(OFF(ac + 1, ac)); P(ac[1][0]);
  __typeof__(({ a[0]; })) sp = a[0]; P(sizeof sp); P(OFF(sp + 1, sp));
  __typeof__(*((int (*)[side(n)])vp)) tv; P(sizeof tv);
  __typeof__((0, ((char (*)[side(m)][3])vp)[1][1]) + 0) tc = 0; P(sizeof tc); P(calls);
  P(sizeof *(n ? (int (*)[side(m)])vp : (int (*)[side(m)])vp)); P(calls);
  g(n, m, a, &a);
}

int main(void) {
  f(3, 4);
  f(2, 5);
  return 0;
}
