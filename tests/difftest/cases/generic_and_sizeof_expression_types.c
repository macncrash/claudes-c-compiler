// The type of an expression as _Generic, sizeof and typeof see it:
// function designators decay to pointers whose parameter names do not
// matter, c ? p : q of pointers keeps both pointees' const, and a narrow
// bit-field operand is promoted to int (C11 6.3.1.1p2), whatever its
// declared type.
#include <stdio.h>

#define G(e) _Generic((e), int: 1, unsigned: 2, long: 3, unsigned long: 4, unsigned long long: 5, \
                      int *: 6, const int *: 7, int (*)(int): 8, double (*)(double, int): 9, default: 0)

struct B { unsigned u : 5; unsigned v : 31; unsigned w : 32; unsigned long long x : 33; unsigned long long y : 20; };

static int fn(int a) { return a + 1; }
static double fd(double d, int k) { return d * k; }
static volatile int Z;

int main(void) {
  int arr[2] = {1, 2};
  int *ip = arr;
  const int *cip = arr;
  struct B b = {0, 0, 0, 0, 0};
  b.u = Z; b.y = Z;
  printf("%d %d %d %d\n", G(fn), G(&fn), G(fd), G(&fd));
  printf("%d %d %d %d\n", G(1 ? cip : ip), G(1 ? ip : cip), G(0 ? ip : ip), G(Z ? cip : 0));
  printf("%d %d %d %d %d\n", G(b.u + 0), G(b.v + 0), G(b.w + 0), G(b.y + 0), G(-b.y));
  printf("%d %d %d\n", G(b.y << 1), G(~b.u), G(b.y * 2u));
  printf("%d %d %d %d %d\n", (int)sizeof(b.y + 0), (int)sizeof(-b.y), (int)sizeof(b.y << 1),
         (int)sizeof(b.x + 0), (int)sizeof(b.w + 0));
  __typeof__(b.y + 0) t = -1;
  __typeof__(1 ? cip : ip) tp = arr;
  printf("%d %d %d\n", (int)sizeof t, t < 0, G(tp));
  printf("%d %d %d\n", b.u - 1 < 0, b.y - 1 < 0, b.x - 1 < 0);
  return fn(0) - 1;
}
