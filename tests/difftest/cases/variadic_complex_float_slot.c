/* A variadic _Complex float, found by a variadic fuzzer that builds caller
 * and callee with different compilers: on Apple arm64 it takes one 8-byte
 * stack slot holding both parts (real in the low half), as clang passes it.
 * ccc passed and read it as two slots, so a clang-built callee (here libc's
 * vprintf, printing the slot's bits) saw the imaginary part as 0 and every
 * later argument shifted, and a clang-built caller's argument was misread.
 * A named _Complex float of a variadic function called through a pointer
 * stays two floats. */
#include <stdio.h>
#include <stdarg.h>
#include <complex.h>

static void vp(const char *f, ...) {
  va_list ap;
  va_start(ap, f);
  vprintf(f, ap);
  va_end(ap);
}

static void reads(int n, ...) {
  va_list ap;
  va_start(ap, n);
  for (int i = 0; i < n; i++) {
    _Complex float c = va_arg(ap, _Complex float);
    int k = va_arg(ap, int);
    printf("%g %g %d\n", (double)__real__ c, (double)__imag__ c, k);
  }
  double d = va_arg(ap, double);
  printf("%g\n", d);
  va_end(ap);
}

static void named(_Complex float z, int n, ...) {
  va_list ap;
  va_start(ap, n);
  _Complex float w = va_arg(ap, _Complex float);
  int k = va_arg(ap, int);
  printf("named %g %g %d %g %g %d\n", (double)__real__ z, (double)__imag__ z, n, (double)__real__ w, (double)__imag__ w, k);
  va_end(ap);
}

int main(void) {
  _Complex float a = 1.5f + 2.5f * I, b = -3.0f + 0.125f * I;
  vp("%llx %d %llx %d\n", a, 7, b, 8);
  vp("%d %llx %g\n", 9, (_Complex float)(4.0f - 1.0f * I), 2.75);
  reads(2, a, 11, b, 12, 6.5);
  void (*volatile pn)(_Complex float, int, ...) = named;
  pn(a, 3, b, 4);
  void (*volatile pv)(const char *, ...) = vp;
  pv("%llx %d\n", b, 5);
  return 0;
}
