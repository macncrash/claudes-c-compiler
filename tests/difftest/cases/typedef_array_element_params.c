/* Array parameters whose element is a typedef'd array (typedef uint64_t fe[4];
 * fe pre[][2], fe (*q)[2], pt a[] with typedef fe pt[3]): the subscript strides
 * were taken from the declarator only, so pre[i][j] was treated as the last
 * dimension and loaded, where it is an array that decays to its address; the
 * loaded words were then used as a pointer. ring's P-256 code
 * (fiat_p256_select_point_affine) crashed. Found by building ring with ccc. */
#include <stdio.h>
#include <stdint.h>

typedef uint64_t fe[4];
typedef fe pt[3];
typedef const uint64_t cfe[4];
struct S { fe m[2][2]; pt p; };

static const fe tab[3][2] = { { { 1, 2, 3, 4 }, { 5, 6, 7, 8 } }, { { 9, 10, 11, 12 }, { 13, 14, 15, 16 } },
                              { { 17, 18, 19, 20 }, { 21, 22, 23, 24 } } };
static fe mtab[3][2] = { { { 1, 2, 3, 4 }, { 5, 6, 7, 8 } }, { { 9, 10, 11, 12 }, { 13, 14, 15, 16 } },
                         { { 17, 18, 19, 20 }, { 21, 22, 23, 24 } } };
static fe g2[3][2];

__attribute__((noinline)) static uint64_t sum(const uint64_t a[4]) { return a[0] + a[1] * 10 + a[2] * 100 + a[3] * 1000; }
/* const typedef'd array as the element of a two-dimensional array parameter */
__attribute__((noinline)) static uint64_t p1(const fe pre[][2], int i) { return sum(pre[i][0]) + sum(pre[i][1]); }
__attribute__((noinline)) static uint64_t p2(fe pre[][2], int i) { return sum(pre[i][0]) + sum(pre[i][1]); }
__attribute__((noinline)) static uint64_t p3(const fe pre[], int i) { return sum(pre[i]) + pre[i][3]; }
__attribute__((noinline)) static uint64_t p4(const fe x) { return sum(x) + x[2]; }
__attribute__((noinline)) static uint64_t p5(const fe *pre, int i) { return sum(pre[i]) + sum(*pre); }
__attribute__((noinline)) static uint64_t p6(cfe pre[][2], int i) { return sum(pre[i][1]) + pre[i][1][2]; }
__attribute__((noinline)) static uint64_t viaptr(fe (*q)[2], int i) { return sum(q[i][1]) + sum(*q[i]) + (uint64_t)(q[i + 1] - q[i]); }
__attribute__((noinline)) static uint64_t viapt(pt a[], int i) { return sum(a[i][2]) + a[i][2][3]; }
__attribute__((noinline)) static uint64_t viastruct(struct S *s) { return sum(s->m[1][0]) + sum(s->p[2]) + s->m[1][1][3]; }
/* The constant-time selection from ring's P-256 code, reduced. */
__attribute__((noinline)) static void cmov(uint64_t out[4], uint64_t z, const uint64_t a[4], const uint64_t b[4]) {
    uint64_t m = 0 - (uint64_t)(z == 0);
    for (int i = 0; i < 4; i++) out[i] = (m & a[i]) | (~m & b[i]);
}
__attribute__((noinline)) static void select_affine(uint64_t idx, int size, const fe pre[][2], fe out[3]) {
    static const fe one = { 1, 0, 0, 0 };
    for (int i = 0; i < 3; i++) for (int k = 0; k < 4; k++) out[i][k] = 0;
    for (int i = 0; i < size; i++) {
        uint64_t mismatch = (uint64_t)i ^ (idx - 1);
        cmov(out[0], mismatch, pre[i][0], out[0]);
        cmov(out[1], mismatch, pre[i][1], out[1]);
    }
    cmov(out[2], idx, out[2], one);
}

int main(void) {
    printf("p1 %llu p2 %llu\n", (unsigned long long)p1(tab, 2), (unsigned long long)p2(mtab, 2));
    printf("p3 %llu p4 %llu\n", (unsigned long long)p3(tab[1], 1), (unsigned long long)p4(tab[2][1]));
    printf("p5 %llu p6 %llu\n", (unsigned long long)p5(tab[0], 1), (unsigned long long)p6(tab, 1));
    fe l2[3][2];
    pt pts[2];
    struct S s;
    for (int i = 0; i < 3; i++) for (int j = 0; j < 2; j++) for (int k = 0; k < 4; k++) { l2[i][j][k] = i * 100 + j * 10 + k; g2[i][j][k] = l2[i][j][k] + 1; }
    for (int i = 0; i < 2; i++) for (int j = 0; j < 3; j++) for (int k = 0; k < 4; k++) pts[i][j][k] = i * 7 + j * 3 + k;
    for (int i = 0; i < 2; i++) for (int j = 0; j < 2; j++) for (int k = 0; k < 4; k++) s.m[i][j][k] = i + j + k * 2;
    for (int j = 0; j < 3; j++) for (int k = 0; k < 4; k++) s.p[j][k] = j * k + 5;
    printf("local %llu %llu\n", (unsigned long long)sum(l2[2][1]), (unsigned long long)viaptr(l2, 1));
    printf("global %llu %llu\n", (unsigned long long)sum(g2[2][0]), (unsigned long long)viaptr(g2, 0));
    printf("pt %llu %llu\n", (unsigned long long)viapt(pts, 1), (unsigned long long)sum(pts[1][1]));
    printf("struct %llu\n", (unsigned long long)viastruct(&s));
    for (uint64_t idx = 0; idx < 4; idx++) {
        fe out[3];
        select_affine(idx, 3, (const fe (*)[2])mtab, out);
        printf("select %llu: %llu %llu %llu\n", (unsigned long long)idx, (unsigned long long)sum(out[0]),
               (unsigned long long)sum(out[1]), (unsigned long long)sum(out[2]));
    }
    printf("sizes %zu %zu %zu %zu\n", sizeof l2, sizeof l2[1], sizeof pts[1], sizeof s.m[1]);
    return 0;
}
