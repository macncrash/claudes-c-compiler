/* C11 Annex G complex multiply and divide: an infinite part makes the
 * result infinite even when the textbook formula gives NaN + NaN i,
 * division by zero gives infinity, and division scales so c^2 + d^2 does
 * not overflow or underflow. Operands are opaque (volatile), so this is
 * the run-time code, not constant folding. NaN prints without its sign. */
#include <math.h>
#include <stdio.h>

static void pd(const char *n, double _Complex z)
{
    double r = __real__ z, i = __imag__ z;
    printf("%s ", n);
    if (isnan(r)) printf("nan "); else printf("%.17g ", r);
    if (isnan(i)) printf("nan\n"); else printf("%.17g\n", i);
}

static void pf(const char *n, float _Complex z)
{
    float r = __real__ z, i = __imag__ z;
    printf("%s ", n);
    if (isnan(r)) printf("nan "); else printf("%.9g ", (double)r);
    if (isnan(i)) printf("nan\n"); else printf("%.9g\n", (double)i);
}

static volatile double V[8];
static volatile float VF[8];

static double _Complex mk(int k, double r, double i) { V[k] = r; V[k + 1] = i; return __builtin_complex(V[k], V[k + 1]); }
static float _Complex mkf(int k, float r, float i) { VF[k] = r; VF[k + 1] = i; return __builtin_complex(VF[k], VF[k + 1]); }

int main(void)
{
    double inf = INFINITY, nan = NAN;
    double _Complex z[] = {
        mk(0, inf, 0), mk(0, inf, inf), mk(0, -inf, 1), mk(0, 1, -inf), mk(0, nan, inf),
        mk(0, inf, nan), mk(0, 0, 0), mk(0, -0.0, 0), mk(0, 1, 2), mk(0, -3, 0.5),
        mk(0, 1e300, 1e300), mk(0, 1e-300, -1e-300), mk(0, nan, 0), mk(0, nan, nan),
        mk(0, 1e308, -1e308), mk(0, 4.9e-324, 4.9e-324),
    };
    int n = sizeof z / sizeof z[0];
    char name[32];
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++) {
            snprintf(name, sizeof name, "m%d.%d", i, j);
            pd(name, z[i] * z[j]);
            snprintf(name, sizeof name, "d%d.%d", i, j);
            pd(name, z[i] / z[j]);
        }

    float finf = INFINITY;
    float _Complex f[] = {
        mkf(0, finf, 0), mkf(0, finf, finf), mkf(0, 1, -finf), mkf(0, 0, 0), mkf(0, 1, 2),
        mkf(0, 1e30f, 1e30f), mkf(0, 1e-30f, 1e-30f), mkf(0, NAN, finf),
    };
    int nf = sizeof f / sizeof f[0];
    for (int i = 0; i < nf; i++)
        for (int j = 0; j < nf; j++) {
            snprintf(name, sizeof name, "fm%d.%d", i, j);
            pf(name, f[i] * f[j]);
            snprintf(name, sizeof name, "fd%d.%d", i, j);
            pf(name, f[i] / f[j]);
        }

    /* Compound assignment takes the same paths. */
    double _Complex acc = mk(0, 1, 1);
    acc *= mk(2, inf, 0);
    pd("acc*=", acc);
    acc = mk(0, 1, 1);
    acc /= mk(2, 0, 0);
    pd("acc/=", acc);
    float _Complex facc = mkf(0, 2, 3);
    facc /= mkf(2, 1e-30f, 1e-30f);
    pf("facc/=", facc);
    return 0;
}
