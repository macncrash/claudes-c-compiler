/* The callee side of abi_fuzz_structs_ccc_caller.c, built by the reference compiler. */
#include <stdio.h>
struct T1 {
  long long f0;
} __attribute__((packed));
struct T2 {
  long f0;
  _Bool f1;
  long f2;
  _Bool f3;
  long long f4;
  _Complex float f5;
};
struct T3 {
  double f0;
  __int128 f1;
  short f2[6];
  float f3;
};
struct T4 {
  double f0;
  double f1;
};
struct T5 {
  double f0[2];
  double f1;
  double f2;
  double f3[1];
};
static void pr_T1(struct T1 v) {
  printf(" {");
  printf(" %lld", (long long)(v.f0));
  printf(" }");
}
static void pr_T2(struct T2 v) {
  printf(" {");
  printf(" %lld", (long long)(v.f0));
  printf(" %lld", (long long)(v.f1));
  printf(" %lld", (long long)(v.f2));
  printf(" %lld", (long long)(v.f3));
  printf(" %lld", (long long)(v.f4));
  printf(" %g,%g", (double)__real__ (v.f5), (double)__imag__ (v.f5));
  printf(" }");
}
static void pr_T3(struct T3 v) {
  printf(" {");
  printf(" %.9g", (double)(v.f0));
  printf(" %llx:%llx", (unsigned long long)((unsigned __int128)(v.f1) >> 64), (unsigned long long)(v.f1));
  printf(" %lld", (long long)(v.f2[0]));
  printf(" %lld", (long long)(v.f2[1]));
  printf(" %lld", (long long)(v.f2[2]));
  printf(" %lld", (long long)(v.f2[3]));
  printf(" %lld", (long long)(v.f2[4]));
  printf(" %lld", (long long)(v.f2[5]));
  printf(" %.9g", (double)(v.f3));
  printf(" }");
}
static void pr_T4(struct T4 v) {
  printf(" {");
  printf(" %.9g", (double)(v.f0));
  printf(" %.9g", (double)(v.f1));
  printf(" }");
}
static void pr_T5(struct T5 v) {
  printf(" {");
  printf(" %.9g", (double)(v.f0[0]));
  printf(" %.9g", (double)(v.f0[1]));
  printf(" %.9g", (double)(v.f1));
  printf(" %.9g", (double)(v.f2));
  printf(" %.9g", (double)(v.f3[0]));
  printf(" }");
}
double g0(float p0, float p1, struct T4 p2, _Complex double p3, _Complex double p4, double p5, struct T2 p6) {
  printf("g0");
  printf(" %.9g", (double)(p0));
  printf(" %.9g", (double)(p1));
  pr_T4(p2);
  printf(" %g,%g", (double)__real__ (p3), (double)__imag__ (p3));
  printf(" %g,%g", (double)__real__ (p4), (double)__imag__ (p4));
  printf(" %.9g", (double)(p5));
  pr_T2(p6);
  printf("\n");
  return 123.8125;
}
double g1(double p0, char p1, long p2, struct T1 p3, long p4, struct T1 p5, struct T2 p6, _Complex double p7) {
  printf("g1");
  printf(" %.9g", (double)(p0));
  printf(" %lld", (long long)(p1));
  printf(" %lld", (long long)(p2));
  pr_T1(p3);
  printf(" %lld", (long long)(p4));
  pr_T1(p5);
  pr_T2(p6);
  printf(" %g,%g", (double)__real__ (p7), (double)__imag__ (p7));
  printf("\n");
  return 20.4375;
}
struct T4 g2(char p0) {
  printf("g2");
  printf(" %lld", (long long)(p0));
  printf("\n");
  return ((struct T4){133.75, -216.875});
}
void g3(int p0, long double p1, struct T1 p2, long p3, struct T5 p4, struct T5 p5, struct T1 p6, _Complex double p7, _Complex double p8, char p9) {
  printf("g3");
  printf(" %lld", (long long)(p0));
  printf(" %.17Lg", p1);
  pr_T1(p2);
  printf(" %lld", (long long)(p3));
  pr_T5(p4);
  pr_T5(p5);
  pr_T1(p6);
  printf(" %g,%g", (double)__real__ (p7), (double)__imag__ (p7));
  printf(" %g,%g", (double)__real__ (p8), (double)__imag__ (p8));
  printf(" %lld", (long long)(p9));
  printf("\n");
}
struct T3 g4(struct T5 p0, struct T4 p1, int p2, struct T3 p3, struct T2 p4, struct T3 p5, struct T1 p6, double p7, struct T4 p8, double p9, struct T1 p10, struct T3 p11, struct T1 p12, float p13, struct T4 (*cb)(struct T3, int, double)) {
  printf("g4");
  pr_T5(p0);
  pr_T4(p1);
  printf(" %lld", (long long)(p2));
  pr_T3(p3);
  pr_T2(p4);
  pr_T3(p5);
  pr_T1(p6);
  printf(" %.9g", (double)(p7));
  pr_T4(p8);
  printf(" %.9g", (double)(p9));
  pr_T1(p10);
  pr_T3(p11);
  pr_T1(p12);
  printf(" %.9g", (double)(p13));
  { struct T4 r_ = cb(((struct T3){-0.0625, ((__int128)-563 << 64 | 5492294458700000334ULL), {-8719, 26044, 4932, 6080, 2481, 15666}, -6.875f}), 59, 2.5); printf(" cb->"); pr_T4(r_); }
  printf("\n");
  return ((struct T3){8.25, ((__int128)734 << 64 | 4549437942311603974ULL), {-10139, 6721, 22420, -24875, -28291, 13258}, 139.0625f});
}
struct T3 g5(float p0, long p1, int p2) {
  printf("g5");
  printf(" %.9g", (double)(p0));
  printf(" %lld", (long long)(p1));
  printf(" %lld", (long long)(p2));
  printf("\n");
  return ((struct T3){73.4375, ((__int128)-790 << 64 | 4587102653675889295ULL), {-12688, -10483, 2809, 15791, -29999, -14584}, -150.6875f});
}
struct T5 g6(struct T5 p0, struct T2 p1, int p2, char p3) {
  printf("g6");
  pr_T5(p0);
  pr_T2(p1);
  printf(" %lld", (long long)(p2));
  printf(" %lld", (long long)(p3));
  printf("\n");
  return p0;
}
