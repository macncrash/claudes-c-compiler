/* The caller side of abi_fuzz_variadic_ccc_callee.c, built by the reference compiler. */
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

char base_[64];
void vf0(int n, ...);
void vf1(int n, ...);
void vf2(int n, ...);
void vf3(int n, char x0, ...);
void vf4(int n, struct S24 x0, struct M1 x1, int x2, short x3, double x4, __int128 x5, struct S12 x6, long double x7, ...);
void vf5(int n, ...);
int vsum(int n, ...);
int main(void) {
  vf0(7, (base_ + 43), (-54.25 + 103.5 * I), 2696523807u, (signed char)46, -363746696992995511LL, ((struct S12){{-918, 638, -303}}), ((struct S32){{-687L, 868L, -494L, -207L}}), -10212.000000f, ((struct S12){{-154, -114, -889}}), (signed char)55, ((struct S16){53L, 122L}), ((struct F4){{111.9375, 110.9375, 76.375, 207.9375}}), ((struct S3){{(char)-708, (char)844, (char)321}}), ((_Complex float)(-92.75 + -108.0 * I)));
  vf1(8, 12688.312500f, (unsigned short)58989, 873307.8125, ((struct D3){{22.625, -156.5, 33.3125}}), ((struct S4){174}), -902883419106658156L);
  vf2(5, ((struct S16){-257L, 415L}), (char)42, ((struct S5){(char)183, 193}), (short)19590, ((struct S12){{785, 699, 630}}), 42, 161.3125, 781054232501L, -175.625);
  vf3(0, (char)-104, ((struct S12){{-14, -773, 243}}), ((struct F4){{-143.375, -67.6875, -6.25, 82.6875}}), ((struct F4){{-239.75, -165.0, -248.0, 12.4375}}), ((struct F2){68.0625, 195.375}), ((struct S12){{-30, -725, -777}}), ((struct S12){{536, 450, -962}}), ((struct S24){{479L, 495L, 15L}}), ((struct F2){-210.125, 68.25}), ((struct F3){{-214.0, -107.875, -156.25}}), (base_ + 23), (unsigned short)39372, 768300895u, ((struct S24){{448L, 110L, 379L}}), 1e300L);
  vf4(7, ((struct S24){{-910L, -368L, -989L}}), ((struct M1){(char)614, -107.5625}), -2108675776, (short)27401, 406919.8115234375, ((__int128)-413286191106LL << 64 | 4867842428860393738ULL), ((struct S12){{871, -352, 345}}), -0.0L, (char)10, 206200381440082054UL);
  vf5(3, (void *)(base_ + 52), ((struct S3){{(char)-425, (char)377, (char)-456}}), ((struct M3){(short)-368, (char)666, 654L}), ((struct BF){(unsigned)-439 & 7, -648 & 255, (unsigned)-805 & 0xfffff}), ((union U12){{23, -789, 139}}), ((struct S2){(short)834}), ((struct F3){{-144.5625, 145.125, 214.0}}), ((struct S1){(char)824}), ((struct S8){-747L}), (_Bool)0, ((struct BF){(unsigned)-58 & 7, 725 & 255, (unsigned)-505 & 0xfffff}), ((struct S8){-399L}));
  int (*fp)(int, ...) = vsum; printf("%d %d\n", fp(3, 1, 2, 3), vsum(5, 15, -10, -2, -39, -42));
  return 0;
}
