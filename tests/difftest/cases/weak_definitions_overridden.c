/* Weak definitions replaced by strong ones from the helper file: a variable,
 * a const, functions with jump-table switches, and weak ones that stay. ccc
 * emitted no .subsections_via_symbols, so ld64 took each section of its
 * object as one atom and could not drop the weak variable for the strong
 * one (duplicate symbol _wv). Found by probing linkage attributes. */
#include <stdio.h>
/* Weak definitions overridden from w2.c, and kept ones beside them. */
__attribute__((weak)) int wv = 5;
__attribute__((weak)) int keep_v = 11;
__attribute__((weak)) const int wc = 3;
static const char *const names[] = { "zero", "one", "two" };
__attribute__((weak)) int wsw(int k) {
    switch (k) {
    case 0: return 100; case 1: return 101; case 2: return 102; case 3: return 103;
    case 4: return 104; case 5: return 105; case 6: return 106; default: return -1;
    }
}
__attribute__((weak)) int keep_sw(int k) {
    switch (k) {
    case 0: return 10; case 1: return 11; case 2: return 12; case 3: return 13;
    case 4: return 14; case 5: return 15; default: return -2;
    }
}
__attribute__((weak)) const char *wname(int k) { return names[k % 3]; }
int other(int k);
int main(void) {
    int s = 0;
    for (int k = 0; k < 8; k++) s = s * 7 + wsw(k) + keep_sw(k) + other(k);
    printf("wv %d keep_v %d wc %d s %d %s %s\n", wv, keep_v, wc, s, wname(1), wname(5));
    return 0;
}
