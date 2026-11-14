/* The caller side of abi_fuzz_structs_ccc_callee.c, built by the reference compiler. */
#include <stdio.h>
struct T2 {
  double f0;
};
struct T1 {
  struct T2 f0;
  double f1[2];
} __attribute__((packed, aligned(16)));
struct T3 {
  double f0;
  long double f1;
  float f2[4];
  int f3 : 2;
  int f4;
};
struct T4 {
  int f0[6];
};
struct T5 {
  double f0[3];
  double f1;
};
static void pr_T2(struct T2 v) {
  printf(" {");
  printf(" %.9g", (double)(v.f0));
  printf(" }");
}
static void pr_T1(struct T1 v) {
  printf(" {");
  pr_T2(v.f0);
  printf(" %.9g", (double)(v.f1[0]));
  printf(" %.9g", (double)(v.f1[1]));
  printf(" }");
}
static void pr_T3(struct T3 v) {
  printf(" {");
  printf(" %.9g", (double)(v.f0));
  printf(" %.17Lg", v.f1);
  printf(" %.9g", (double)(v.f2[0]));
  printf(" %.9g", (double)(v.f2[1]));
  printf(" %.9g", (double)(v.f2[2]));
  printf(" %.9g", (double)(v.f2[3]));
  printf(" %lld", (long long)(v.f3));
  printf(" %lld", (long long)(v.f4));
  printf(" }");
}
static void pr_T4(struct T4 v) {
  printf(" {");
  printf(" %lld", (long long)(v.f0[0]));
  printf(" %lld", (long long)(v.f0[1]));
  printf(" %lld", (long long)(v.f0[2]));
  printf(" %lld", (long long)(v.f0[3]));
  printf(" %lld", (long long)(v.f0[4]));
  printf(" %lld", (long long)(v.f0[5]));
  printf(" }");
}
static void pr_T5(struct T5 v) {
  printf(" {");
  printf(" %.9g", (double)(v.f0[0]));
  printf(" %.9g", (double)(v.f0[1]));
  printf(" %.9g", (double)(v.f0[2]));
  printf(" %.9g", (double)(v.f1));
  printf(" }");
}
int g0(struct T4 p0, double p1, long double p2, struct T4 p3, struct T1 p4, float p5, struct T4 p6, int p7);
struct T4 g1(struct T1 p0, struct T5 p1, struct T4 p2, long double p3, struct T1 p4, float p5, struct T1 p6, struct T4 p7, char p8);
struct T1 g2(struct T4 p0, struct T1 p1, struct T5 p2, struct T1 p3, struct T3 p4);
double g3(struct T5 p0, struct T3 p1);
double g4(long double p0, struct T5 p1, struct T4 p2, float p3, struct T5 p4, struct T1 p5, struct T1 p6, struct T5 p7, struct T1 p8, long double p9, struct T3 p10, struct T5 p11, struct T3 p12);
struct T1 g5(struct T5 p0, struct T3 p1, struct T1 p2, struct T5 p3, struct T5 p4, char p5, _Complex double p6, float p7, struct T4 p8, long double p9, struct T3 p10, struct T5 p11, long double p12);
int g6(long double p0, struct T3 p1);
struct T3 g7(int p0, struct T3 p1, struct T1 p2);
int main(void) {
  { int r_ = g0(((struct T4){{-695411332, -109968368, 994249646, -883454166, 974604674, -682146567}}), 235.375, -74.75L, ((struct T4){{760009621, 604319259, 180900726, 785396976, -508015204, 132827348}}), ((struct T1){{131.75}, {-10.75, -29.25}}), 209.5625f, ((struct T4){{1012901420, 731300318, 572195182, 386717937, 488919691, -1038706001}}), -622733360); printf("ret"); printf(" %lld", (long long)(r_)); printf("\n"); }
  { struct T4 r_ = g1(((struct T1){{214.9375}, {-68.375, 48.4375}}), ((struct T5){{-61.0625, 2.875, 80.9375}, -85.0625}), ((struct T4){{-490657691, 738717838, -510069281, -839608201, -708069737, -425524449}}), 166.5625L, ((struct T1){{59.375}, {-33.375, -99.625}}), -208.625f, ((struct T1){{-63.4375}, {-209.25, -61.5625}}), ((struct T4){{297527466, -662457492, 402198434, 177728963, 642437789, -591106817}}), 103); printf("ret"); pr_T4(r_); printf("\n"); }
  { struct T1 r_ = g2(((struct T4){{564615833, 1021514257, 314574030, -923866866, 360195614, 763202628}}), ((struct T1){{-229.75}, {-153.875, 220.0625}}), ((struct T5){{97.125, 2.375, 222.75}, -48.375}), ((struct T1){{-74.375}, {116.125, -22.5625}}), ((struct T3){120.6875, 15.5625L, {-194.9375f, 237.25f, 28.125f, 130.6875f}, 1, 573337428})); printf("ret"); pr_T1(r_); printf("\n"); }
  { double r_ = g3(((struct T5){{248.8125, -43.375, 207.5}, 124.5}), ((struct T3){218.0625, -48.8125L, {96.6875f, 61.875f, -65.125f, 32.6875f}, 0, -434150662})); printf("ret"); printf(" %.9g", (double)(r_)); printf("\n"); }
  { double r_ = ((double (*volatile)(long double, struct T5, struct T4, float, struct T5, struct T1, struct T1, struct T5, struct T1, long double, struct T3, struct T5, struct T3))g4)(-38.8125L, ((struct T5){{184.4375, 49.8125, 74.625}, 199.6875}), ((struct T4){{463763608, -563838573, 762200882, 444986630, -675386287, -271671379}}), 229.6875f, ((struct T5){{185.3125, -70.125, 196.4375}, -103.75}), ((struct T1){{-206.5}, {-49.9375, -203.125}}), ((struct T1){{-94.375}, {130.0625, 118.6875}}), ((struct T5){{207.4375, -70.0625, 13.125}, 173.875}), ((struct T1){{-212.75}, {19.375, 116.375}}), 169.1875L, ((struct T3){-130.1875, -198.3125L, {-114.875f, -118.125f, -121.0f, 45.5f}, 1, 317514440}), ((struct T5){{200.0625, 97.3125, 198.1875}, 198.9375}), ((struct T3){-3.0, -40.5L, {121.0f, 218.875f, -183.1875f, -108.6875f}, 1, 899593446})); printf("ret"); printf(" %.9g", (double)(r_)); printf("\n"); }
  { struct T1 r_ = g5(((struct T5){{-191.875, -218.3125, -180.125}, 240.25}), ((struct T3){-25.8125, -170.8125L, {134.5f, -41.0625f, 137.375f, -166.0625f}, 0, -287988547}), ((struct T1){{-137.25}, {12.0, 81.875}}), ((struct T5){{-122.5625, 57.0, 168.4375}, 38.25}), ((struct T5){{99.625, -161.8125, 87.625}, 81.0625}), 72, (18.5 + -10.0 * 1.0i), -30.1875f, ((struct T4){{-391570422, -524506525, -863976234, -248599729, -696662048, 334988167}}), -195.5L, ((struct T3){199.375, 183.375L, {65.75f, -222.5625f, -193.5625f, 86.5f}, 1, -881927624}), ((struct T5){{86.375, 169.125, 94.875}, -9.875}), 235.0L); printf("ret"); pr_T1(r_); printf("\n"); }
  { int r_ = g6(243.0L, ((struct T3){69.25, 230.875L, {26.875f, -128.25f, -242.5f, -240.0f}, 0, -1011892118})); printf("ret"); printf(" %lld", (long long)(r_)); printf("\n"); }
  { struct T3 r_ = g7(872592747, ((struct T3){-148.3125, -35.875L, {-128.5f, -202.25f, -223.25f, -18.6875f}, 1, -859831203}), ((struct T1){{58.875}, {150.3125, -10.0}})); printf("ret"); pr_T3(r_); printf("\n"); }
  { printf("mem"); printf(" %.9g", (double)(g7(872592747, ((struct T3){-148.3125, -35.875L, {-128.5f, -202.25f, -223.25f, -18.6875f}, 1, -859831203}), ((struct T1){{58.875}, {150.3125, -10.0}})).f0)); printf("\n"); }
  return 0;
}
