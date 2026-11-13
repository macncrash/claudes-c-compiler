// sizeof evaluates an operand of variable length array type (sizeof p[k++]
// increments k), and static functions called only from VLA sizes - in
// declarators, parameters, casts, compound literals, typeof and sizeof -
// must still be emitted.
#include <stdio.h>

static int calls;
static int idx(int i) { calls++; return i; }
static int dim(int i) { calls += 10; return i + 1; }
static int pdim(int i) { calls += 100; return i; }
static int cdim(int i) { calls += 1000; return i; }
static int tdim(int i) { calls += 10000; return i; }
static int sdim(int i) { calls += 100000; return i; }

__attribute__((noinline)) static unsigned long par(int n, int m, int a[pdim(n)][m], int k) {
  unsigned long s = sizeof a[k++];
  s = s * 100 + sizeof *a++;
  s = s * 100 + sizeof a[idx(1)];
  return s * 100 + (unsigned long)k * 10 + (unsigned long)(a != 0);
}

__attribute__((noinline)) static int f(int n) {
  int k = n;
  int m2[n][n + 1];
  long (*p)[n + 2] = 0;
  long (*pp)[n + 2] = (long (*)[n + 2])16;
  printf("a %lu %d\n", sizeof m2[k++], k);
  printf("b %lu %d\n", sizeof m2[idx(0)], calls);
  printf("c %lu %d\n", sizeof *(p + k++), k);
  printf("d %lu %ld\n", sizeof *pp++, (long)pp);
  printf("e %lu %d\n", sizeof(p[idx(2)]), calls);
  printf("f %lu %d\n", sizeof m2[k++][0], k);
  printf("g %lu %d\n", par(n, n + 3, (int (*)[n + 3])m2, 2), calls);
  printf("h %lu %d\n", sizeof(int[sdim(4)][sdim(5)]), calls);
  int v[dim(n)];
  printf("i %lu %d\n", sizeof v, calls);
  char (*q)[n] = (char (*)[cdim(n)])m2;
  printf("j %d %d\n", (int)((char *)(q + 1) - (char *)q), calls);
  typeof(int[tdim(n)]) t;
  printf("k %lu %d\n", sizeof t, calls);
  // Each dimension of a declaration is evaluated once.
  calls = 0;
  int a2[dim(2)][dim(3)][idx(4)];
  printf("l %lu %lu %d\n", sizeof a2, sizeof a2[1], calls);
  int (*pa)[idx(n + 1)][dim(2)] = (void *)a2;
  printf("m %d %d\n", (int)((char *)(pa + 1) - (char *)pa), calls);
  typedef short T2[idx(2)][dim(4)];
  T2 tt[2];
  printf("n %lu %lu %d\n", sizeof tt, sizeof(T2), calls);
  return k;
}

int main(void) {
  printf("%d\n", f(3));
  return 0;
}
