/* _Complex values as conditions, complex constants, and mixed real/complex
 * arithmetic, found by a runtime complex-arithmetic fuzzer:
 * - __real__/__imag__ typed as the complex (Ptr) in conditions, so -0.0
 *   from __imag__ was tested by its bits and taken as true;
 * - sema had no CType for the complex type specifiers (Int fallback), so
 *   (float _Complex)0.25f folded to 0 and `?:`/`&&` took the wrong arm;
 *   complex constants as conditions, casts to real and __real__ in static
 *   initializers came out 0;
 * - sema typed __builtin_complex(re, im) as double, so its negation was
 *   taken for a double holding the value's address;
 * - a real operand was made x+0i, changing signed zeros (Annex G: x + z
 *   leaves z's imaginary part alone, x * z scales both parts). */
#include <stdio.h>

typedef double _Complex C;
typedef float _Complex F;

static void pc(const char *n, C v) { printf("%s %g %g\n", n, __real__ v, __imag__ v); }

__attribute__((noinline)) int im_cond(C c) { return __imag__ c ? 1 : 2; }
__attribute__((noinline)) int re_if(C c) { if (__real__ c) return 1; return 2; }
__attribute__((noinline)) int im_not(C c) { return !__imag__ c; }
__attribute__((noinline)) int im_and(C c) { return __imag__ c && 1; }
__attribute__((noinline)) int fim_cond(F c) { return __imag__ c ? 1 : 2; }

__attribute__((noinline)) int k_cond(void) { return ((float _Complex)0.25f) ? 1 : 2; }
__attribute__((noinline)) int k_cond2(void) { return ((C)0.25) ? 1 : 2; }
__attribute__((noinline)) int k_and(void) { return ((float _Complex)0.25f) && 1; }
__attribute__((noinline)) int k_or(void) { return 0 || (C)0.5; }
__attribute__((noinline)) int k_while(void) { int n = 0; while ((C)0.5 && n < 3) n++; return n; }
__attribute__((noinline)) int k_bool(void) { return (_Bool)(float _Complex)0.25f; }
__attribute__((noinline)) int k_not(void) { return !(float _Complex)0.25f; }
__attribute__((noinline)) double k_sel(F c) { return c ? 2.0 : 3.0; }

static int g_bool = (_Bool)(C)0.5;
static int g_cond = ((C)0.5) ? 1 : 2;
static int g_not = !(C)0.5;
static int g_and = (C)0.5 && 1;
static int g_imag = (_Bool)(0.5i);
static int g_icond = (0.5i) ? 1 : 2;
static double g_re = __real__ (C)(1.0 + 2.0i);
static double g_im = __imag__ (C)(1.0 + 2.0i);
static int g_int = (int)(C)3.5;
static int g_zero = (_Bool)(C)0.0;

__attribute__((noinline)) C neg_mul(C c) { c *= -(__builtin_complex(__real__ c, __imag__ c)); return c; }
__attribute__((noinline)) C neg_init(C c) { C d = -(__builtin_complex(__real__ c, __imag__ c)); return d; }
__attribute__((noinline)) C neg_const(void) { return -(__builtin_complex(1.0, 2.0)); }
__attribute__((noinline)) C neg_float(F f) { return -(__builtin_complex(__real__ f, __imag__ f)); }

__attribute__((noinline)) C add1(double x, C z) { return x + z; }
__attribute__((noinline)) C add2(C z, double x) { return z + x; }
__attribute__((noinline)) C sub1(C z, double x) { return z - x; }
__attribute__((noinline)) C sub2(double x, C z) { return x - z; }
__attribute__((noinline)) C mul1(double x, C z) { return x * z; }
__attribute__((noinline)) C mul2(C z, int k) { return z * k; }
__attribute__((noinline)) C div1(C z, double x) { return z / x; }
__attribute__((noinline)) C cadd(C z, double x) { z += x; return z; }
__attribute__((noinline)) C csub(C z, double x) { z -= x; return z; }
__attribute__((noinline)) C cmul(C z, double x) { z *= x; return z; }
__attribute__((noinline)) C cdiv(C z, double x) { z /= x; return z; }
__attribute__((noinline)) F fmul(F z, double x) { z *= x; return z; }

int main(void)
{
    C nz = __builtin_complex(-0.0, -0.0);
    printf("%d %d %d %d %d\n", im_cond(nz), re_if(nz), im_not(nz), im_and(nz), fim_cond(__builtin_complex(-0.0f, -0.0f)));
    printf("%d %d %d %d %d %d %d %g %g\n", k_cond(), k_cond2(), k_and(), k_or(), k_while(), k_bool(), k_not(),
           k_sel(0.25f), k_sel(0));
    printf("%d %d %d %d %d %d %g %g %d %d\n", g_bool, g_cond, g_not, g_and, g_imag, g_icond, g_re, g_im, g_int, g_zero);

    C w = 4.0 + 1.0i;
    pc("neg_mul", neg_mul(w));
    pc("neg_init", neg_init(w));
    pc("neg_const", neg_const());
    pc("neg_float", neg_float(1.5f - 2.0if));

    C v = __builtin_complex(3.0, -4.0);
    double xs[] = {0.0, -0.0, 2.0};
    for (int i = 0; i < 3; i++) {
        double x = xs[i];
        pc("add1", add1(x, nz));
        pc("add2", add2(nz, x));
        pc("sub1", sub1(nz, x));
        pc("sub2", sub2(x, nz));
        pc("mul1", mul1(x, v));
        pc("mul1z", mul1(x, nz));
        pc("mul2", mul2(nz, -2));
        pc("div1", div1(v, x ? x : 4.0));
        pc("cadd", cadd(nz, x));
        pc("csub", csub(nz, x));
        pc("cmul", cmul(v, x));
        pc("cdiv", cdiv(nz, 3.0));
        F f = fmul(__builtin_complex(-0.0f, 2.0f), x);
        printf("fmul %g %g\n", (double)__real__ f, (double)__imag__ f);
    }
    return 0;
}
