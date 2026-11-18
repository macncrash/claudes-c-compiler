/* Floats live across calls, passed to and returned from calls, as
 * parameters (register and stack), variadic arguments, and sin/cos pairs
 * of one argument (fused into one libm call on Apple arm64). */
#include <math.h>
#include <stdio.h>

static volatile int sink;

__attribute__((noinline)) static float addf(float a, float b) { sink++; return a + b; }
__attribute__((noinline)) static double addd(double a, double b) { sink++; return a + b; }

/* Ten float parameters: two arrive on the stack. */
__attribute__((noinline)) static float many(float a, float b, float c, float d, float e,
                                            float f, float g, float h, float i, float j)
{
    return a - b * 2 + c * 3 - d * 4 + e * 5 - f * 6 + g * 7 - h * 8 + i * 9 - j * 10;
}

/* Mixed int and double parameters, more doubles than registers. */
__attribute__((noinline)) static double mixed(int n, double a, long m, double b, double c, double d,
                                              double e, double f, double g, double h, double i, float k)
{
    return n * a + m * b + c - d + e * f - g + h * i + k;
}

/* Many float values live across calls: more than d8-d15 hold. */
__attribute__((noinline)) static float pressure(float x)
{
    float a = x * 1.5f, b = x + 2, c = x * x, d = x - 3, e = x * 0.25f;
    float f = a + b, g = c - d, h = e * a, i = b * c, j = d + e;
    float r = addf(a, b);
    r = addf(r, c) + d;
    r = addf(r, e) * f;
    r = addf(r, g) - h;
    r = addf(r, i) + j;
    return r + a + b + c + d + e + f + g + h + i + j;
}

__attribute__((noinline)) static double chain(double t)
{
    double acc = 0;
    for (int k = 0; k < 6; k++) {
        double u = addd(t, k);       /* result kept across the next call */
        double v = addd(u, t * 0.5);
        acc += u * v - t;
    }
    return acc;
}

typedef float (*binf)(float, float);

__attribute__((noinline)) static float through(binf fn, float a, float b) { return fn(a, b) * fn(b, a); }

struct grid { int n; float k[8], ph[8]; float *out; };

/* The ocean shape: sin and cos of one angle per element, stored apart,
 * with loads of the struct between them. */
__attribute__((noinline)) static void table(struct grid *g, float t)
{
    for (int j = 0; j < g->n; j++) {
        float a = g->k[j] * t + g->ph[j];
        g->out[2 * j] = cosf(a);
        g->out[2 * j + 1] = sinf(a);
    }
}

__attribute__((noinline)) static double rot(double x, double y, double th, double *ny)
{
    double s = sin(th);           /* sin first, then cos */
    double nx = x * cos(th) - y * s;
    *ny = x * s + y * cos(th);    /* a second cos(th): fused once, then CSE'd or called */
    return nx;
}

int main(void)
{
    float out[16];
    struct grid g = { 8, { 0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f },
                      { 1, 2, 3, 4, 5, 6, 7, 8 }, out };
    double sum = 0;
    for (int f = 0; f < 50; f++) {
        table(&g, f / 7.0f);
        for (int i = 0; i < 16; i++)
            sum += out[i];
    }
    printf("table %.6f\n", sum);

    double ny, nx = rot(1.0, 2.0, 0.7, &ny);
    printf("rot %.9f %.9f\n", nx, ny);

    float s = 0;
    for (int i = 0; i < 20; i++)
        s += many(i, i + 1, i + 2, i + 3, i + 4, i + 5, i + 6, i + 7, i + 8, i + 9 + 0.5f);
    printf("many %.4f\n", s);
    printf("mixed %.6f\n", mixed(3, 1.5, 4, 2.5, 3.5, 4.5, 5.5, 6.5, 7.5, 8.5, 9.5, 0.25f));
    printf("pressure %.4f %.4f\n", pressure(1.25f), pressure(-3.5f));
    printf("chain %.6f\n", chain(0.375));
    printf("through %.4f\n", through(addf, 1.5f, -0.25f));

    /* Variadic doubles from floats held in registers across printf calls. */
    float p = 0.5f, q = -1.25f;
    for (int i = 0; i < 3; i++) {
        printf("var %.3f %.3f %d\n", p, q, i);
        p = addf(p, q);
        q = q * -0.5f;
    }

    /* Both orders and both widths of the sin/cos pair in one block. */
    float fa = 0.9f;
    float c1 = cosf(fa), s1 = sinf(fa);
    double da = 2.2;
    double s2 = sin(da), c2 = cos(da);
    printf("pairs %.7f %.7f %.12f %.12f\n", c1, s1, s2, c2);
    /* -0.0 and 0.0 are different arguments: sin keeps the sign. */
    float z1 = sinf(-0.0f), z2 = cosf(0.0f), z3 = sinf(0.0f);
    printf("zeros %g %g %g\n", z1, z2, z3);
    printf("sink %d\n", sink);
    return 0;
}
