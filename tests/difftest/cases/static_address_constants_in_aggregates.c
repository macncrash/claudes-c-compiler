// Address constants in static initializers: function designators through
// *, &*e, &"str"[n] and wide strings plus offsets, addresses cast to
// pointer-sized integers in struct members, integer arrays and arrays of
// structs, braced scalars in pointer arrays, and pointer differences in a
// global that follows others whose initializers are copied (the type of
// &SA[2] was taken from a cached &SA[0].in[1]).
#include <stdio.h>
#include <wchar.h>

struct In { char c; short s; int a[3]; };
struct S { int x; struct In in[2]; long l; char name[6]; };
int A[10] = {10, 11, 12, 13, 14, 15, 16, 17, 18, 19};
int M[3][4] = {{20, 21, 22, 23}, {24, 25, 26, 27}, {28, 29, 30, 31}};
struct S SA[3] = {
  {1, {{'a', 2, {3, 4, 5}}, {'b', 6, {7, 8, 9}}}, 100, "abcde"},
  {11, {{'c', 12, {13, 14, 15}}, {'d', 16, {17, 18, 19}}}, 200, "fghij"},
  {21, {{'e', 22, {23, 24, 25}}, {'f', 26, {27, 28, 29}}}, 300, "klmno"},
};
long L = 5;
static int f2(int v) { return v * 2; }

#define OFF(p, b) ((long)((const char *)(p) - (const char *)(b)))

// *f is f.
int (*fp1)(int) = *f2;
int (*fp2)(int) = **f2;
struct FP { int (*f)(int); int *p; } fs1 = {*f2, A + 8}, fs2[] = {{*f2, &A[1]}, {&f2, 0}};

// &*e is e; &"str"[n] in members; wide strings count elements.
const char *s1 = &*"xyz";
struct SP { short s; char *f0; const char *f1; } sp1 = {0, 0, &"abcd"[1]}, sp2 = {.f1 = &("" "ab" "cd")[3]};
struct SP1 { const char *p; } sp3 = {&"abcd"[2]};
const wchar_t *w1 = L"abc" + 1;
const wchar_t *w2 = &L"abc"[2];
struct WP { const wchar_t *p; } w3 = {&L"xyz"[1]};
const wchar_t *w4[] = {L"pq" + 1};

// Addresses held in pointer-sized integers.
struct IL { long f0; char *f1; } il1 = {(long)&A[3], (char *)A}, il2 = {(long)(A + 3)};
struct IL1 { long f0; } il3 = {(long)&A[4]};
struct ILP { unsigned long f0; char pad; } il4[] = {{.f0 = (unsigned long)(A + 4)}, {(unsigned long)(A + 5)}};
struct ILA { long tag; struct In in; unsigned long arr[2]; } il5 = {7, {0}, {(unsigned long)&A[1], (unsigned long)&A[2]}};
struct ILB { long tag; unsigned long arr[2]; } il6 = {7, {(unsigned long)&A[1], (unsigned long)&A[2]}},
  il7 = {.arr = {(unsigned long)&M[1][1], [1] = (unsigned long)&M[2][2]}},
  il8[] = {{1, {(long)A}}, {2, (long)(A + 1), (long)(A + 2)}};
struct ILC { char pad; __INTPTR_TYPE__ f0; char *f1; } il9 = {0, (__INTPTR_TYPE__)(&M[0][0] + 2), (char *)A + 20};

// A braced scalar in an array of pointers inside a struct.
struct BP { int n; int *p[2]; } bp1 = {1, {{&A[6]}, {A + 7}}};

// Pointer differences after globals whose initializers are copied.
struct T1 { int *f0; long f1; long f2; int bits : 3; };
const struct T1 t1 = {.f0 = &6[A], .f2 = (long)(&(A + 2)[2]), .f1 = (char *)&SA[2] - (char *)&SA[0].in[1]};
struct T5 { short s; long *f0; long f1; };
struct T5 t5 = {.f0 = &L, .f1 = &SA[2] - SA};
struct T6 { struct S *f0; long f1; } t6 = {SA + 1, &SA[2].in[1] - &SA[0].in[0]};

int main(void) {
  static struct IL sil = {(long)&M[1][3], (char *)&SA[1].name[2]};
  static struct SP ssp = {.f1 = &"local"[3]};
  printf("%d %d %d %d %d %ld\n", fp1(1), fp2(2), fs1.f(3), fs2[0].f(4), fs2[1].f(5), fs1.p - A);
  printf("%d %ld\n", fs2[0].p[0], (long)(fs2[1].p == 0));
  printf("%s %s %s %s\n", s1, sp1.f1, sp2.f1, sp3.p);
  printf("%lc %lc %lc %lc\n", (wint_t)*w1, (wint_t)*w2, (wint_t)*w3.p, (wint_t)*w4[0]);
  printf("%ld %ld %ld %ld\n", OFF(il1.f0, A), OFF(il1.f1, A), OFF(il2.f0, A), OFF(il3.f0, A));
  printf("%ld %ld\n", OFF(il4[0].f0, A), OFF(il4[1].f0, A));
  printf("%ld %ld %ld %ld %ld\n", il5.tag, OFF(il5.arr[0], A), OFF(il5.arr[1], A), OFF(il6.arr[0], A), OFF(il6.arr[1], A));
  printf("%ld %ld\n", OFF(il7.arr[0], M), OFF(il7.arr[1], M));
  printf("%ld %ld %ld %ld %ld\n", OFF(il8[0].arr[0], A), il8[0].arr[1], OFF(il8[1].arr[0], A), OFF(il8[1].arr[1], A), il8[1].tag);
  printf("%ld %ld\n", OFF(il9.f0, M), OFF(il9.f1, A));
  printf("%d %d\n", *bp1.p[0], *bp1.p[1]);
  printf("%d %ld %d\n", *t1.f0, t1.f1, *(int *)t1.f2);
  printf("%ld %ld %d %ld\n", *t5.f0, t5.f1, t6.f0->x, t6.f1);
  printf("%d %c %s\n", *(int *)sil.f0, *sil.f1, ssp.f1);
  return 0;
}
