/* Tokens are formed before macro expansion, so MAJOR.MINOR.RELEASE is
 * MAJOR . MINOR . RELEASE even once MAJOR has become 1: ccc decided whether
 * an identifier was part of a pp-number (like the I of 1.I) from the
 * expansion so far, so "1." kept MINOR from expanding (zstd and lz4 version
 * strings came out "1.ZSTD_VERSION_MINOR. 7"), and x.y with x = 1 did not
 * substitute y. Pieces of an expansion that touched without whitespace
 * also stringified with a space ("1. 5. 7"). Real pp-numbers (1.I, 0x1p2,
 * 10L, 1e+E, and 1.x in a macro body) are still single tokens. Found by
 * the real-program tests (tests/projects). */
#include <stdio.h>
#define S_(x) #x
#define S(x) S_(x)
#define MAJ 1
#define MIN 5
#define REL 7
#define VER MAJ.MIN.REL
#define I 99
#define L 98
#define E 97
#define ULL 96
#define G(x, y) x.y
#define H(x) 1.x
#define K(x) x.E
#define Q(x) 0x1p##x
#define F 3
int main(void) {
  printf("%s\n", S(VER));
  printf("%s %s %s\n", S(1.I), S(15.IF), S(0x1p2));
  printf("%s %s %s %s\n", S(10L), S(0xFFULL), S(1e+E), S(1.e+L));
  printf("%s %s %s\n", S(G(MAJ, MIN)), S(G(1, I)), S(H(I)));
  printf("%s %s\n", S(K(MAJ)), S(K(2)));
  printf("%s %s %s\n", S(MAJ.I), S(MAJ.L), S(MAJ .MIN));
  return 0;
}
