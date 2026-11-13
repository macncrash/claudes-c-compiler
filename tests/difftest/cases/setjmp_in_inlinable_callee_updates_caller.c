// A function that calls setjmp must not be inlined: the caller's locals it
// changes through pointers between its setjmp and longjmp could then live in
// callee-saved registers, which the longjmp puts back to their old values.
#include <stdio.h>
#include <setjmp.h>

static int g(int *p, int k) {
  jmp_buf jb;
  if (setjmp(jb) == 0) {
    *p += k;
    longjmp(jb, 1);
  } else {
    *p += 100;
  }
  return *p;
}

static long h2(long *acc, int n) {
  jmp_buf jb;
  volatile int i = 0;
  if (setjmp(jb) < n) {
    *acc = *acc * 3 + i;
    i = i + 1;
    longjmp(jb, i + 1);
  }
  return *acc;
}

static inline unsigned long sig(unsigned long *s, int k) {
  sigjmp_buf jb;
  switch (sigsetjmp(jb, 0)) {
  case 0:
    *s ^= (unsigned long)k << 8;
    siglongjmp(jb, 2);
  case 2:
    *s += 1;
    break;
  default:
    *s = 0;
  }
  return *s;
}

int main(void) {
  int c = 0;
  long a = 1;
  unsigned long s = 5;
  for (int i = 0; i < 5; i++) {
    g(&c, i + 1);
    h2(&a, i + 2);
    sig(&s, i * 3 + 1);
  }
  printf("%d %ld %lu\n", c, a, s);
  return 0;
}
