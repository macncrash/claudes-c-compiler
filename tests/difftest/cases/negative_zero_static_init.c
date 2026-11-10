/* -0.0 in static data. The data emitter merged constants equal to zero into
 * a .zero run, and -0.0 == 0.0, so an array element, a compound literal or
 * a long double -0.0 was stored as +0.0 (a scalar global was right). Static
 * complex initializers widened a real operand to (x, +0) before negating
 * or multiplying it: -1.0 got a -0.0 imaginary part, while -(1.0 + 0.0i)
 * must have one. Each value prints as its bytes. */
#include <stdio.h>
#include <string.h>

struct S { int i; double d; float f[3]; };
struct S gs = { 1, -0.0, { 0.0f, -0.0f, 1.0f } };
static struct S ga[2] = { [1] = { .d = -0.0, .f[2] = -0.0f } };
static const double dv[] = { -0.0, 0.0, -__builtin_inf() };
static const float fv[] = { -0.0f, 0.0f };
double gm[2][2] = { { -0.0 }, { 0.0, -0.0 } };
union U { double d; long l; } gu = { -0.0 };
double *gp = (double[]){ -0.0, 0.0 };
long double gl[] = { -0.0L, 0.0L };
_Complex double gc[] = { -1.0, -0.0, 2.0 * 1.0i, -(1.0 + 0.0i), -(_Complex double)1.0, 3.0 - 1.0i,
                         (double)(2.0 + 3.0i), 1.0i * 1.0i, -2.0 * (1.0 + 1.0i), 0.0i - 1.0 };
_Complex float gcf = -2.0f;

static void dump(const char *n, const void *p, unsigned len)
{
    const unsigned char *b = p;
    printf("%s:", n);
    for (unsigned i = 0; i < len; i++)
        printf("%02x", b[i]);
    printf("\n");
}

__attribute__((noinline)) void locals(double x)
{
    double la[4] = { 1.0, -0.0 };
    static float sf[3] = { 0.0f, 0.0f, -0.0f };
    _Complex double a = -1.0, b = -x;
    dump("la", la, sizeof la);
    dump("sf", sf, sizeof sf);
    dump("a", &a, sizeof a);
    dump("b", &b, sizeof b);
}

int main(void)
{
    dump("gs.d", &gs.d, 8);
    dump("gs.f", gs.f, 12);
    dump("ga1.d", &ga[1].d, 8);
    dump("ga1.f", ga[1].f, 12);
    dump("dv", dv, sizeof dv);
    dump("fv", fv, sizeof fv);
    dump("gm", gm, sizeof gm);
    dump("gu", &gu, 8);
    dump("gp", gp, 16);
    dump("gl", gl, sizeof gl);
    for (unsigned i = 0; i < sizeof gc / sizeof gc[0]; i++)
        dump("gc", &gc[i], sizeof gc[i]);
    dump("gcf", &gcf, sizeof gcf);
    locals(3.0);
    return 0;
}
