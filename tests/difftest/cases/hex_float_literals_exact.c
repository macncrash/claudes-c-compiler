/* Hexadecimal floating constants are rounded once, exactly: ccc computed
 * (int + frac) * powi(2, exp), which gave 0 for every subnormal (0x1p-1074:
 * powi went through 2^1074 = inf), dropped digits past 16, rounded twice
 * and rounded float constants through double. Subnormals and their ties,
 * overflow to infinity, long digit strings, float constants. Found by the
 * math function fuzzer (ilogb(0x1p-1074) was ilogb(0)). */
#include <stdio.h>
#define D(x) printf("%-36s %a\n", #x, (double)(x))
#define F(x) printf("%-36s %a\n", #x, (double)(x))
int main(void) {
  D(0x1p-1074); D(0x1.8p-1073); D(0x1p-1075); D(0x1.8p-1075); D(0x1.0000000000001p-1075); D(0x1p-1076);
  D(0x1.fffffffffffffp-1023); D(0x1.ffffffffffffffp-1023); D(0x1p-1022); D(0x0.8p-1021); D(0x3p-1076);
  D(0x1.fffffffffffffp+1023); D(0x1.fffffffffffff7ffp+1023); D(0x1.fffffffffffff8p+1023); D(0x1p+1024); D(0x10p+1020);
  D(0x1.00000000000008p0); D(0x1.00000000000008000000001p0); D(0x1.00000000000018p0); D(0x1.0000000000000fffffffffffp0);
  D(0x123456789abcdef0123456789p0); D(0x0.000000000000000000000000000000001p0); D(0xffffffffffffffffffffp-80);
  D(0x.8p1); D(0x1.p3); D(0x0p0); D(-0x1p-1074); D(0xABCDEFp-24); D(0x1P+4);
  F(0x1p-149f); F(0x1p-150f); F(0x1.8p-150f); F(0x1.000001p0f); F(0x1.000003p0f); F(0x1.0000018p0f);
  F(0x1.fffffep127f); F(0x1.ffffffp127f); F(0x1p128f); F(0x1.0000010000000001p0f); F(0x1.00000100000000000000000000001p0f);
  F(0x1p-126f); F(0x0.fffffep-126f); F(0x1.fffffcp-127f);
  return 0;
}
