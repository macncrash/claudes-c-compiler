/* Complex constants folded in static initializers. Division, conjugate
 * (~), ?:, __builtin_complex, CMPLX and == all once folded to 0 + 0i;
 * multiply and divide now fold as __muldc3 / __divdc3 compute at run
 * time (C11 Annex G: infinities kept, quotients scaled). */
#include <complex.h>
#include <math.h>
#include <stdio.h>

static double _Complex g1 = (INFINITY + 0.0i) * (1.0 + 1.0i);
static double _Complex g2 = (1.0 + 1.0i) / (0.0 + 0.0i);
static double _Complex g3 = (1e300 + 1e300i) / (1e300 + 1e300i);
static double _Complex g4 = (2.0 + 3.0i) * (4.0 - 1.0i);
static double _Complex g5 = (2.0 + 3.0i) / (4.0 - 1.0i);
static float _Complex g6 = (1e30f + 1e30fi) / (1e30f + 1e30fi);
static double _Complex g7 = (1.0 + 1.0i) / (INFINITY + 0.0i);
static double _Complex g8 = (INFINITY + 1.0i) / (2.0 + 1.0i);
static double _Complex h1 = ~(1.0 + 2.0i);
static double _Complex h2 = 1 ? (1.0 + 2.0i) : (3.0 + 4.0i);
static double h3 = __real__ (5.0 + 6.0i);
static double h4 = __imag__ (5.0 + 6.0i);
static double _Complex h5 = __builtin_complex(7.0, -8.0);
static double _Complex h6 = CMPLX(1.5, 2.5);
static float _Complex h6f = CMPLXF(0.5, INFINITY);
static double _Complex h7 = 3.0 * I;
static double _Complex h8 = 2.0 + 3.0 * I;
static double _Complex h9 = (double _Complex)(1.5f + 2.5fi);
static float _Complex h10 = (float _Complex)(1.0 / 3.0 + 1.0i / 7.0);
static double _Complex h11 = (2.0 + 4.0i) / 2;
static double _Complex h12 = 1.0 / (0.0 + 1.0i);
static double _Complex h13 = (1.0 + 1.0i) * (1.0 - 1.0i) / (2.0 + 0.0i);
static double _Complex h14 = -(1.0 + 2.0i) * 2;
static double _Complex h15 = (1.0 + 2.0i) == (1.0 + 2.0i);
static double _Complex h16 = (3 / 2) + 0.0i;
static double _Complex h17 = ~5 + 1.0i;
static double _Complex hs[2] = { (1.0 + 1.0i) / (1.0 - 1.0i), 6.0 / (1.0 + 1.0i) };

static void p(const char *n, double _Complex z)
{
    double r = __real__ z, i = __imag__ z;
    printf("%s ", n);
    if (isnan(r)) printf("nan "); else printf("%.17g ", r);
    if (isnan(i)) printf("nan\n"); else printf("%.17g\n", i);
}

int main(void)
{
    p("g1", g1); p("g2", g2); p("g3", g3); p("g4", g4); p("g5", g5); p("g6", g6); p("g7", g7);
    p("g8", g8); p("h1", h1); p("h2", h2); printf("h3 %g h4 %g\n", h3, h4); p("h5", h5);
    p("h6", h6); p("h6f", h6f); p("h7", h7); p("h8", h8); p("h9", h9); p("h10", h10);
    p("h11", h11); p("h12", h12); p("h13", h13); p("h14", h14); p("h15", h15); p("h16", h16);
    p("h17", h17); p("hs0", hs[0]); p("hs1", hs[1]);
    return 0;
}
