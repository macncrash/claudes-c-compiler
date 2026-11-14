/* A braced initializer (or a string for a char array) initializes the whole
 * subobject it targets, so a later one for the same member, element or
 * sub-array replaces an earlier one instead of merging with it (C11
 * 6.7.9p19; clang and gcc agree): { .a = {1, 2, 3}, .a = {7} } leaves
 * a = {7, 0, 0}, .name = "ab" clears the rest of name, and
 * { [1].a = {1, 2, 3}, [1] = { .l = 5 } } zeroes a. ccc kept the earlier
 * values, in static and automatic objects, structs, arrays of structs (and
 * structs with a long, which take the relocation-aware path) and
 * multi-dimensional arrays. Found by the aggregate initializer fuzzer. */
#include <stdio.h>

struct P { char c; int a[3]; short s; };
struct Q { struct P p[2]; char name[7]; struct { short x, y; } pt; };
struct R { int a[3]; char name[7]; long l; };

static struct P g1 = { .a = {1, 2, 3}, .c = 9, {7} };
static struct P g2 = { .a = {1, 2, 3}, .a = {[1] = 5} };
static struct P g3 = { .a[2] = 4, .a = {8} };
static struct Q g4 = { .name = {1, 2, 3, 4, 5, 6}, .name = "ab" };
static struct Q g5 = { .p[1] = {1, {2, 3, 4}, 5}, .p[1] = {.s = 6} };
static struct Q g6 = { .pt = {3, 4}, .pt = {.y = 9} };
static struct Q g7 = { .p[0].a = {1, 2, 3}, .p[0].a[1] = 7 };
static struct Q g8 = { .p = {{1, {1, 1, 1}, 1}, {2, {2, 2, 2}, 2}}, .p[1].a = {9} };
static int g9[2][3] = { {1, 2, 3}, {4, 5, 6}, [0] = {7} };

static struct R ra[2] = { {0}, { .name = {1, 2, 3, 4, 5, 6}, .name = {"ab"} } };
static struct R rb[2] = { {0}, { .name = {1, 2, 3, 4, 5, 6}, .name = "ab" } };
static struct R rc[2] = { {0}, { .a = {1, 2, 3}, .l = 4, .a = {9} } };
static struct R rd[2] = { [1] = { .a = {1, 2, 3} }, [1].a = {9} };
static struct R re[2] = { [1].a = {1, 2, 3}, [1] = { .l = 5 } };
static struct R rf = { .name = {1, 2, 3, 4, 5, 6}, .name = {"ab"} };
static struct P pa[2] = { [1].a = {1, 2, 3}, [1] = { .s = 5 } };

/* Members that are arrays of structs or 2-D arrays, in a struct with a long
 * (the relocation-aware path, which also put { [1] = {...} } in element 0). */
struct L { struct P p[2]; short g[2][3]; long l; };
static struct L lg1 = { .p = { [1] = { 5, {6, 7, 8}, 9 } } };
static struct L lg2 = { .p = { {1, {2, 3, 4}, 5}, [0] = { .s = 6 } } };
static struct L lg3 = { .g = { {1, 2, 3}, [0] = { }, {4, 5, 6} } };
#define PL(v) printf(#v " %d %d %d %d %d | %d %d %d %d %d | %d %d %d %d %d %d\n", \
  v.p[0].c, v.p[0].a[0], v.p[0].a[1], v.p[0].a[2], v.p[0].s, v.p[1].c, v.p[1].a[0], v.p[1].a[1], v.p[1].a[2], \
  v.p[1].s, v.g[0][0], v.g[0][1], v.g[0][2], v.g[1][0], v.g[1][1], v.g[1][2])

static void members(void) {
  struct L ll1 = { .p = { [1] = { 5, {6, 7, 8}, 9 } } };
  struct L ll2 = { .p = { {1, {2, 3, 4}, 5}, [0] = { .s = 6 } } };
  struct L ll3 = { .g = { {1, 2, 3}, [0] = { }, {4, 5, 6} } };
  PL(lg1); PL(lg2); PL(lg3); PL(ll1); PL(ll2); PL(ll3);
}

#define PP(v) printf(#v " %d %d %d %d %d\n", v.c, v.a[0], v.a[1], v.a[2], v.s)
#define PQ(v) printf(#v " %d %d %d %d %d | %d %d %d %d %d | %d %d %d %d %d %d %d | %d %d\n", \
  v.p[0].c, v.p[0].a[0], v.p[0].a[1], v.p[0].a[2], v.p[0].s, v.p[1].c, v.p[1].a[0], v.p[1].a[1], v.p[1].a[2], \
  v.p[1].s, v.name[0], v.name[1], v.name[2], v.name[3], v.name[4], v.name[5], v.name[6], v.pt.x, v.pt.y)
#define PR(v) printf(#v " %d %d %d | %d %d %d %d %d %d %d | %ld\n", v.a[0], v.a[1], v.a[2], \
  v.name[0], v.name[1], v.name[2], v.name[3], v.name[4], v.name[5], v.name[6], v.l)

static void arrays_of_structs(void) {
  struct R la[2] = { {0}, { .name = {1, 2, 3, 4, 5, 6}, .name = {"ab"} } };
  struct R ld[2] = { [1] = { .a = {1, 2, 3} }, [1].a = {9} };
  struct R le[2] = { [1].a = {1, 2, 3}, [1] = { .l = 5 } };
  struct P lp[2] = { [1].a = {1, 2, 3}, [1] = { .s = 5 } };
  PR(ra[1]); PR(rb[1]); PR(rc[1]); PR(rd[1]); PR(re[1]); PR(rf); PP(pa[1]);
  PR(la[1]); PR(ld[1]); PR(le[1]); PP(lp[1]);
}

int main(void) {
  struct P l1 = { .a = {1, 2, 3}, .c = 9, {7} };
  struct P l2 = { .a = {1, 2, 3}, .a = {[1] = 5} };
  struct P l3 = { .a[2] = 4, .a = {8} };
  struct Q l4 = { .name = {1, 2, 3, 4, 5, 6}, .name = "ab" };
  struct Q l5 = { .p[1] = {1, {2, 3, 4}, 5}, .p[1] = {.s = 6} };
  struct Q l6 = { .pt = {3, 4}, .pt = {.y = 9} };
  struct Q l7 = { .p[0].a = {1, 2, 3}, .p[0].a[1] = 7 };
  struct Q l8 = { .p = {{1, {1, 1, 1}, 1}, {2, {2, 2, 2}, 2}}, .p[1].a = {9} };
  int l9[2][3] = { {1, 2, 3}, {4, 5, 6}, [0] = {7} };
  PP(g1); PP(g2); PP(g3); PQ(g4); PQ(g5); PQ(g6); PQ(g7); PQ(g8);
  PP(l1); PP(l2); PP(l3); PQ(l4); PQ(l5); PQ(l6); PQ(l7); PQ(l8);
  printf("g9 %d %d %d %d %d %d\n", g9[0][0], g9[0][1], g9[0][2], g9[1][0], g9[1][1], g9[1][2]);
  printf("l9 %d %d %d %d %d %d\n", l9[0][0], l9[0][1], l9[0][2], l9[1][0], l9[1][1], l9[1][2]);
  arrays_of_structs();
  members();
  return 0;
}
