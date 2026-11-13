// Compound literals and arrays of arrays initialized with brace elision
// (C11 6.7.9p20): after unbraced values, a brace initializes the current
// subobject, the coarsest one whose boundary the position is on; an
// unsized outer dimension counts rows, not values.
#include <stdio.h>

struct P { int x, y; };
struct R { int *p; int v; };
union U { int i; char b[6]; };
static volatile int V = 9;
static int X[4] = {10, 20, 30, 40};

// Unsized arrays of arrays: rows by elision.
static int g1[][3] = {{1, 2}, {3}, 4, 5, 6};
static int g2[][3] = {1, 2, 3, 4};
static int g3[][2][2] = {1, 2, {3}, {4, 5, 6, 7}};
static int g4[3][2][2] = {1, 2, {3}, {4, 5, 6, 7}};
static int g5[][3][1] = {1, {2}, 3};
static struct P g6[][2] = {1, 2, {3, 4}, 5, 6, 7};
static struct P g7[][2][2] = {1, 2, {3}, {{4}, 5}, 6};
static struct R g8[][2][2] = {&X[0], 1, {&X[1], 2}, {{&X[3]}, &X[2], 5}, &X[0]};
static char g9[][2][4] = {"ab", {"cd"}, {"ef", "gh"}, "ij"};
static int *g10[][2][2] = {&X[0], {&X[1]}, {&X[2], 0}, &X[3]};

// File-scope compound literals: declared length, designators, strings,
// elided element braces, arrays of arrays.
static int *c1 = (int[]){5, 6, [5] = 7};
static char *c2 = (char[8]){"ab"};
static const char *c3 = (const char[]){"gstr"};
static struct P *c4 = (struct P[]){1, 2, 3, 4, 5};
static struct P *c5 = (struct P[3]){{1, 2}, [2] = {7, 8}};
static int (*c6)[3] = (int[][3]){{1, 2}, {3}, 4, 5, 6};
static struct P (*c7)[2] = (struct P[][2]){{{1, 2}, {3, 4}}, {5, 6, 7}};
static union U *c8 = (union U[]){{1}, {.b = {1, 2, 3}}, 7};
static int c9[sizeof((char[]){"hello"}) + sizeof((int[]){1, [9] = 2})];

static void pi(const char *tag, const int *p, int n) {
  printf("%s:", tag);
  for (int i = 0; i < n; i++) printf(" %d", p[i]);
  printf("\n");
}

static void pp(const char *tag, const struct P *p, int n) {
  printf("%s:", tag);
  for (int i = 0; i < n; i++) printf(" %d,%d", p[i].x, p[i].y);
  printf("\n");
}

static void pr(const char *tag, const struct R *r, int n) {
  printf("%s:", tag);
  for (int i = 0; i < n; i++) printf(" %d,%d", r[i].p ? *r[i].p : -1, r[i].v);
  printf("\n");
}

int main(void) {
  printf("sizes: %d %d %d %d %d %d %d %d %d %d %d\n", (int)sizeof g1, (int)sizeof g2, (int)sizeof g3,
         (int)sizeof g5, (int)sizeof g6, (int)sizeof g7, (int)sizeof g8, (int)sizeof g9,
         (int)sizeof g10, (int)sizeof c9, (int)(sizeof g2 / sizeof g2[0]));
  pi("g1", &g1[0][0], sizeof g1 / sizeof(int));
  pi("g2", &g2[0][0], sizeof g2 / sizeof(int));
  pi("g3", &g3[0][0][0], sizeof g3 / sizeof(int));
  pi("g4", &g4[0][0][0], sizeof g4 / sizeof(int));
  pi("g5", &g5[0][0][0], sizeof g5 / sizeof(int));
  pp("g6", &g6[0][0], sizeof g6 / sizeof(struct P));
  pp("g7", &g7[0][0][0], sizeof g7 / sizeof(struct P));
  pr("g8", &g8[0][0][0], sizeof g8 / sizeof(struct R));
  printf("g9:");
  for (unsigned i = 0; i < sizeof g9 / 4; i++) printf(" [%s]", &g9[0][0][0] + i * 4);
  printf("\n");
  printf("g10:");
  for (unsigned i = 0; i < sizeof g10 / sizeof(int *); i++) {
    int *q = (&g10[0][0][0])[i];
    printf(" %d", q ? *q : -1);
  }
  printf("\n");

  pi("c1", c1, 6);
  printf("c2: %s %d  c3: %s\n", c2, c2[7], c3);
  pp("c4", c4, 3);
  pp("c5", c5, 3);
  pi("c6", &c6[0][0], 9);
  pp("c7", &c7[0][0], 4);
  printf("c8: %d %d %d\n", c8[0].i, c8[1].b[2], c8[2].i);

  // The same at block scope, with runtime values.
  int n = V - 8;
  int l1[][3] = {{V, 2}, {3}, 4, 5, V};
  int l2[][3] = {1, 2, 3, V};
  int l3[3][2][2] = {1, V, {3}, {4, 5, 6, 7}};
  int l4[][3][1] = {n, {V}, n};
  struct P l5[][2] = {V, 2, {3, 4}, 5, 6, 7};
  struct P l6[][2][2] = {1, 2, {3}, {{4}, 5}, V};
  char l7[][2][4] = {"ab", {"cd"}, {"ef", "gh"}, "ij"};
  printf("lsizes: %d %d %d %d %d %d\n", (int)sizeof l1, (int)sizeof l2, (int)sizeof l4,
         (int)sizeof l5, (int)sizeof l6, (int)sizeof l7);
  pi("l1", &l1[0][0], sizeof l1 / sizeof(int));
  pi("l2", &l2[0][0], sizeof l2 / sizeof(int));
  pi("l3", &l3[0][0][0], 12);
  pi("l4", &l4[0][0][0], sizeof l4 / sizeof(int));
  pp("l5", &l5[0][0], sizeof l5 / sizeof(struct P));
  pp("l6", &l6[0][0][0], sizeof l6 / sizeof(struct P));
  printf("l7:");
  for (unsigned i = 0; i < sizeof l7 / 4; i++) printf(" [%s]", &l7[0][0][0] + i * 4);
  printf("\n");

  // Block-scope compound literals.
  pp("k1", (struct P[]){1, 2, 3, 4, 5}, 3);
  pp("k2", (struct P[]){1, 2, {3, 4}, 5, V}, 3);
  pp("k3", (struct P[]){[1] = 7, 8, 9}, 3);
  struct P (*k4)[2] = (struct P[][2]){V, 2, {3, 4}, 5, 6, 7};
  pp("k4", &k4[0][0], 4);
  int (*k5)[3] = (int[][3]){{V, 2}, {3}, 4, 5, V};
  pi("k5", &k5[0][0], 9);
  union U *k6 = (union U[]){{V}, {.b = {1, 2, 3}}, 7};
  printf("k6: %d %d %d\n", k6[0].i, k6[1].b[2], k6[2].i);
  printf("ksizes: %d %d %d %d %d %d %d\n", (int)sizeof((char[]){"abc"}), (int)sizeof((int[]){1, [6] = 2}),
         (int)sizeof((struct P[]){{1, 2}, {3, 4}, [4] = {5}}), (int)sizeof((struct P[]){1, 2, 3, 4, 5}),
         (int)sizeof((int[][3]){{1}, {2}, 3}), (int)sizeof((struct P[][2]){1, 2, 3, 4, 5}),
         (int)sizeof((union U[]){1, 2, 3}));
  return 0;
}
