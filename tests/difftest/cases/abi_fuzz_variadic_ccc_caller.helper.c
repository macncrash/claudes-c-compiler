/* The callee side of abi_fuzz_variadic_ccc_caller.c, built by the reference compiler. */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <complex.h>
struct S1 { char a; };
struct S2 { short a; };
struct S3 { char a[3]; };
struct S4 { int a; };
struct S5 { char a; int b; };
struct S8 { long a; };
struct S12 { int a[3]; };
struct S16 { long a, b; };
struct S24 { long a[3]; };
struct S32 { long a[4]; };
struct F1 { float a; };
struct F2 { float a, b; };
struct F3 { float a[3]; };
struct F4 { float a[4]; };
struct D1 { double a; };
struct D2 { double a, b; };
struct D3 { double a[3]; };
struct D4 { double a[4]; };
struct M1 { char c; double d; };
struct M2 { float f; int i; };
struct M3 { short s; char c; long l; };
struct BF { unsigned a : 3; int b : 9; unsigned c : 20; };
union U8 { long l; double d; char c[8]; };
union U12 { int i[3]; float f; };
struct E { int z[0]; };
extern char base_[64];

static void vh0_0(va_list *ap) {
  { long v_ = va_arg(*ap, long); printf(" h"); printf(" %lld", (long long)v_); }
}
static void vh4_1(va_list *ap) {
  { struct F2 v_ = va_arg(*ap, struct F2); printf(" h"); printf(" %.9g", (double)v_.a); printf(" %.9g", (double)v_.b); }
  { struct BF v_ = va_arg(*ap, struct BF); printf(" h"); printf(" %.9g", (double)v_.a); printf(" %.9g", (double)v_.b); printf(" %.9g", (double)v_.c); }
  { unsigned long v_ = va_arg(*ap, unsigned long); printf(" h"); printf(" %lld", (long long)v_); }
  { _Complex float v_ = va_arg(*ap, _Complex float); printf(" h"); printf(" %.9g,%.9g", (double)__real__ v_, (double)__imag__ v_); }
  { struct S8 v_ = va_arg(*ap, struct S8); printf(" h"); printf(" %.9g", (double)v_.a); }
  { _Complex float v_ = va_arg(*ap, _Complex float); printf(" h"); printf(" %.9g,%.9g", (double)__real__ v_, (double)__imag__ v_); }
  { struct S32 v_ = va_arg(*ap, struct S32); printf(" h"); printf(" %.9g", (double)v_.a[0]); printf(" %.9g", (double)v_.a[1]); printf(" %.9g", (double)v_.a[2]); printf(" %.9g", (double)v_.a[3]); }
  { struct S5 v_ = va_arg(*ap, struct S5); printf(" h"); printf(" %.9g", (double)v_.a); printf(" %.9g", (double)v_.b); }
}
void vf0(int n, ...) {
  printf("vf0");
  va_list ap;
  va_start(ap, n);
  vh0_0(&ap);
  { unsigned long v_ = va_arg(ap, unsigned long); printf(" %lld", (long long)v_); }
  { long v_ = va_arg(ap, long); printf(" %lld", (long long)v_); }
  va_end(ap);
  printf("\n");
}
void vf1(int n, ...) {
  printf("vf1");
  va_list ap;
  va_start(ap, n);
  { int v_ = va_arg(ap, int); printf(" %lld", (long long)v_); }
  { int v_ = va_arg(ap, int); printf(" %lld", (long long)v_); }
  { int v_ = va_arg(ap, int); printf(" %lld", (long long)v_); }
  { double v_ = va_arg(ap, double); printf(" %.17g", (double)v_); }
  { struct D4 v_ = va_arg(ap, struct D4); printf(" %.9g", (double)v_.a[0]); printf(" %.9g", (double)v_.a[1]); printf(" %.9g", (double)v_.a[2]); printf(" %.9g", (double)v_.a[3]); }
  { int v_ = va_arg(ap, int); printf(" %lld", (long long)v_); }
  { struct M1 v_ = va_arg(ap, struct M1); printf(" %.9g", (double)v_.c); printf(" %.9g", (double)v_.d); }
  { void * v_ = va_arg(ap, void *); printf(" %ld", (long)((char *)v_ - base_)); }
  { struct M1 v_ = va_arg(ap, struct M1); printf(" %.9g", (double)v_.c); printf(" %.9g", (double)v_.d); }
  { struct S16 v_ = va_arg(ap, struct S16); printf(" %.9g", (double)v_.a); printf(" %.9g", (double)v_.b); }
  va_end(ap);
  printf("\n");
}
void vf2(int n, struct F3 x0, long double x1, char x2, __int128 x3, long x4, float x5, struct D2 x6, int x7, ...) {
  printf("vf2");
  printf(" %.9g", (double)x0.a[0]); printf(" %.9g", (double)x0.a[1]); printf(" %.9g", (double)x0.a[2]);
  printf(" %.17Lg", (long double)x1);
  printf(" %lld", (long long)x2);
  printf(" %llx:%llx", (unsigned long long)((unsigned __int128)x3 >> 64), (unsigned long long)x3);
  printf(" %lld", (long long)x4);
  printf(" %.17g", (double)x5);
  printf(" %.9g", (double)x6.a); printf(" %.9g", (double)x6.b);
  printf(" %lld", (long long)x7);
  va_list ap;
  va_start(ap, x7);
  { struct F3 v_ = va_arg(ap, struct F3); printf(" %.9g", (double)v_.a[0]); printf(" %.9g", (double)v_.a[1]); printf(" %.9g", (double)v_.a[2]); }
  { int v_ = va_arg(ap, int); printf(" %lld", (long long)v_); }
  { int v_ = va_arg(ap, int); printf(" %lld", (long long)v_); }
  { struct F1 v_ = va_arg(ap, struct F1); printf(" %.9g", (double)v_.a); }
  { int v_ = va_arg(ap, int); printf(" %lld", (long long)v_); }
  { struct M2 v_ = va_arg(ap, struct M2); printf(" %.9g", (double)v_.f); printf(" %.9g", (double)v_.i); }
  { struct F4 v_ = va_arg(ap, struct F4); printf(" %.9g", (double)v_.a[0]); printf(" %.9g", (double)v_.a[1]); printf(" %.9g", (double)v_.a[2]); printf(" %.9g", (double)v_.a[3]); }
  va_end(ap);
  printf("\n");
}
void vf3(int n, int x0, struct D2 x1, struct S12 x2, char x3, short x4, double x5, struct F3 x6, _Complex double x7, long double x8, ...) {
  printf("vf3");
  printf(" %lld", (long long)x0);
  printf(" %.9g", (double)x1.a); printf(" %.9g", (double)x1.b);
  printf(" %.9g", (double)x2.a[0]); printf(" %.9g", (double)x2.a[1]); printf(" %.9g", (double)x2.a[2]);
  printf(" %lld", (long long)x3);
  printf(" %lld", (long long)x4);
  printf(" %.17g", (double)x5);
  printf(" %.9g", (double)x6.a[0]); printf(" %.9g", (double)x6.a[1]); printf(" %.9g", (double)x6.a[2]);
  printf(" %.9g,%.9g", (double)__real__ x7, (double)__imag__ x7);
  printf(" %.17Lg", (long double)x8);
  va_list ap;
  va_start(ap, x8);
  { unsigned long v_ = va_arg(ap, unsigned long); printf(" %lld", (long long)v_); }
  { struct M3 v_ = va_arg(ap, struct M3); printf(" %.9g", (double)v_.s); printf(" %.9g", (double)v_.c); printf(" %.9g", (double)v_.l); }
  { struct F4 v_ = va_arg(ap, struct F4); printf(" %.9g", (double)v_.a[0]); printf(" %.9g", (double)v_.a[1]); printf(" %.9g", (double)v_.a[2]); printf(" %.9g", (double)v_.a[3]); }
  { union U8 v_ = va_arg(ap, union U8); printf(" %.9g", (double)v_.l); }
  { int v_ = va_arg(ap, int); printf(" %lld", (long long)v_); }
  { __int128 v_ = va_arg(ap, __int128); printf(" %llx:%llx", (unsigned long long)((unsigned __int128)v_ >> 64), (unsigned long long)v_); }
  { struct M2 v_ = va_arg(ap, struct M2); printf(" %.9g", (double)v_.f); printf(" %.9g", (double)v_.i); }
  { double v_ = va_arg(ap, double); printf(" %.17g", (double)v_); }
  { int v_ = va_arg(ap, int); printf(" %lld", (long long)v_); }
  { __int128 v_ = va_arg(ap, __int128); printf(" %llx:%llx", (unsigned long long)((unsigned __int128)v_ >> 64), (unsigned long long)v_); }
  { va_list cp; va_copy(cp, ap);
    { struct S32 v_ = va_arg(cp, struct S32); printf(" c"); printf(" %.9g", (double)v_.a[0]); printf(" %.9g", (double)v_.a[1]); printf(" %.9g", (double)v_.a[2]); printf(" %.9g", (double)v_.a[3]); }
    { struct M1 v_ = va_arg(cp, struct M1); printf(" c"); printf(" %.9g", (double)v_.c); printf(" %.9g", (double)v_.d); }
    { int v_ = va_arg(cp, int); printf(" c"); printf(" %lld", (long long)v_); }
    va_end(cp); }
  { struct S32 v_ = va_arg(ap, struct S32); printf(" %.9g", (double)v_.a[0]); printf(" %.9g", (double)v_.a[1]); printf(" %.9g", (double)v_.a[2]); printf(" %.9g", (double)v_.a[3]); }
  { struct M1 v_ = va_arg(ap, struct M1); printf(" %.9g", (double)v_.c); printf(" %.9g", (double)v_.d); }
  { int v_ = va_arg(ap, int); printf(" %lld", (long long)v_); }
  va_end(ap);
  printf("\n");
}
void vf4(int n, struct D2 x0, void * x1, struct S24 x2, __int128 x3, long double x4, struct F3 x5, float x6, struct D2 x7, char x8, ...) {
  printf("vf4");
  printf(" %.9g", (double)x0.a); printf(" %.9g", (double)x0.b);
  printf(" %ld", (long)((char *)x1 - base_));
  printf(" %.9g", (double)x2.a[0]); printf(" %.9g", (double)x2.a[1]); printf(" %.9g", (double)x2.a[2]);
  printf(" %llx:%llx", (unsigned long long)((unsigned __int128)x3 >> 64), (unsigned long long)x3);
  printf(" %.17Lg", (long double)x4);
  printf(" %.9g", (double)x5.a[0]); printf(" %.9g", (double)x5.a[1]); printf(" %.9g", (double)x5.a[2]);
  printf(" %.17g", (double)x6);
  printf(" %.9g", (double)x7.a); printf(" %.9g", (double)x7.b);
  printf(" %lld", (long long)x8);
  va_list ap;
  va_start(ap, x8);
  { struct D4 v_ = va_arg(ap, struct D4); printf(" %.9g", (double)v_.a[0]); printf(" %.9g", (double)v_.a[1]); printf(" %.9g", (double)v_.a[2]); printf(" %.9g", (double)v_.a[3]); }
  vh4_1(&ap);
  { int v_ = va_arg(ap, int); printf(" %lld", (long long)v_); }
  va_end(ap);
  printf("\n");
}
void vf5(int n, ...) {
  printf("vf5");
  va_list ap;
  va_start(ap, n);
  { struct S24 v_ = va_arg(ap, struct S24); printf(" %.9g", (double)v_.a[0]); printf(" %.9g", (double)v_.a[1]); printf(" %.9g", (double)v_.a[2]); }
  { int v_ = va_arg(ap, int); printf(" %lld", (long long)v_); }
  { int v_ = va_arg(ap, int); printf(" %lld", (long long)v_); }
  { _Complex float v_ = va_arg(ap, _Complex float); printf(" %.9g,%.9g", (double)__real__ v_, (double)__imag__ v_); }
  { struct F3 v_ = va_arg(ap, struct F3); printf(" %.9g", (double)v_.a[0]); printf(" %.9g", (double)v_.a[1]); printf(" %.9g", (double)v_.a[2]); }
  { long long v_ = va_arg(ap, long long); printf(" %lld", (long long)v_); }
  { long long v_ = va_arg(ap, long long); printf(" %lld", (long long)v_); }
  { unsigned long v_ = va_arg(ap, unsigned long); printf(" %lld", (long long)v_); }
  { struct F4 v_ = va_arg(ap, struct F4); printf(" %.9g", (double)v_.a[0]); printf(" %.9g", (double)v_.a[1]); printf(" %.9g", (double)v_.a[2]); printf(" %.9g", (double)v_.a[3]); }
  { struct S24 v_ = va_arg(ap, struct S24); printf(" %.9g", (double)v_.a[0]); printf(" %.9g", (double)v_.a[1]); printf(" %.9g", (double)v_.a[2]); }
  { _Complex double v_ = va_arg(ap, _Complex double); printf(" %.9g,%.9g", (double)__real__ v_, (double)__imag__ v_); }
  va_end(ap);
  printf("\n");
}
void vf6(int n, _Complex double x0, ...) {
  printf("vf6");
  printf(" %.9g,%.9g", (double)__real__ x0, (double)__imag__ x0);
  va_list ap;
  va_start(ap, x0);
  { long v_ = va_arg(ap, long); printf(" %lld", (long long)v_); }
  va_end(ap);
  printf("\n");
}
int vsum(int n, ...) { va_list ap; va_start(ap, n); long s = 0; for (int i = 0; i < n; i++) s += va_arg(ap, int) * (i + 1); va_end(ap); return (int)s; }
