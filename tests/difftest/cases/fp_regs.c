/* Floating-point register allocation: values live across calls (callee-saved
 * d8-d15), more live values than registers, loop-carried float phis, mixed
 * float/double widths, casts, compares, selects, and float loads/stores. */
#include <math.h>
#include <stdio.h>

int h_fp_callee_saved_ok(void (*fn)(void));
double h_clobber_fp(double x);

static volatile double sink;

/* Many floats live at once and across calls, in a function a clang-built
 * caller expects to preserve d8-d15. */
static void heavy(void)
{
    double a = sink + 1.5, b = a * 2.25, c = b - 0.75, d = c * c;
    double e = d / 3.0, f = e + a, g = f * b, h = g - c;
    double i = h * 0.5, j = i + d, k = j * e, l = k - f;
    double s = h_clobber_fp(a);
    double t = a + b + c + d + e + f + g + h + i + j + k + l;
    sink = s + t;
}

static float dot3(const float *x, const float *y)
{
    return x[0] * y[0] + x[1] * y[1] + x[2] * y[2];
}

struct particle { float x, y, vx, vy; double mass; };

static void step(struct particle *p, int n, float dt)
{
    for (int i = 0; i < n; i++) {
        float ax = -p[i].x * 0.5f, ay = -p[i].y * 0.5f - 9.81f;
        p[i].vx += ax * dt;
        p[i].vy += ay * dt;
        p[i].x += p[i].vx * dt;
        p[i].y += p[i].vy * dt;
        if (p[i].y < 0.0f) {
            p[i].y = -p[i].y;
            p[i].vy = -p[i].vy * 0.8f;
        }
        p[i].mass *= p[i].vy > 0 ? 1.0001 : 0.9999;
    }
}

/* 32 accumulators: more than the 24 allocatable FP registers. */
static double pressure(int n)
{
    double v[32];
    double a0 = 0, a1 = 0, a2 = 0, a3 = 0, a4 = 0, a5 = 0, a6 = 0, a7 = 0;
    double a8 = 0, a9 = 0, a10 = 0, a11 = 0, a12 = 0, a13 = 0, a14 = 0, a15 = 0;
    double a16 = 0, a17 = 0, a18 = 0, a19 = 0, a20 = 0, a21 = 0, a22 = 0, a23 = 0;
    double a24 = 0, a25 = 0, a26 = 0, a27 = 0, a28 = 0, a29 = 0, a30 = 0, a31 = 0;
    for (int i = 0; i < n; i++) {
        double x = (double)i * 0.001;
        a0 += x; a1 += x * 2; a2 += x * 3; a3 += x * 4; a4 += x * 5; a5 += x * 6;
        a6 += x * 7; a7 += x * 8; a8 += x * 9; a9 += x * 10; a10 += x * 11;
        a11 += x * 12; a12 += x * 13; a13 += x * 14; a14 += x * 15; a15 += x * 16;
        a16 -= x; a17 -= x * 2; a18 -= x * 3; a19 -= x * 4; a20 -= x * 5;
        a21 -= x * 6; a22 -= x * 7; a23 -= x * 8; a24 += x / 3; a25 += x / 5;
        a26 += x / 7; a27 += x / 9; a28 += x * x; a29 -= x * x; a30 += 1.0; a31 -= 0.5;
    }
    v[0] = a0; v[1] = a1; v[2] = a2; v[3] = a3; v[4] = a4; v[5] = a5; v[6] = a6; v[7] = a7;
    v[8] = a8; v[9] = a9; v[10] = a10; v[11] = a11; v[12] = a12; v[13] = a13; v[14] = a14;
    v[15] = a15; v[16] = a16; v[17] = a17; v[18] = a18; v[19] = a19; v[20] = a20;
    v[21] = a21; v[22] = a22; v[23] = a23; v[24] = a24; v[25] = a25; v[26] = a26;
    v[27] = a27; v[28] = a28; v[29] = a29; v[30] = a30; v[31] = a31;
    double s = 0;
    for (int i = 0; i < 32; i++)
        s += v[i] * (i + 1);
    return s;
}

/* Floats live across calls to libm and to a clang-built function. */
static double across_calls(int n)
{
    double acc = 0.0, w = 1.0;
    float f = 0.25f;
    for (int i = 1; i <= n; i++) {
        double s = sin(i * 0.1);
        double c = h_clobber_fp(s);
        acc += s * w + c * 1e-3;
        w *= 0.99;
        f = f * 1.01f + (float)s;
    }
    return acc + f;
}

static float mixed(int n)
{
    float m = -INFINITY, lo = INFINITY;
    double sum = 0;
    for (int i = 0; i < n; i++) {
        float x = (float)((i * 37) % 101) / 7.0f - 5.0f;
        m = x > m ? x : m;
        lo = fminf(lo, x);
        sum += (double)x * x;
        if (x == 0.0f || x != x)
            sum += 1000;
        int k = (int)(x * 3.0f);
        sum += (unsigned)(k & 7);
    }
    return m + lo + (float)sum;
}

int main(void)
{
    sink = 0.5;
    printf("callee-saved d8-d15 kept: %d/8\n", h_fp_callee_saved_ok(heavy));
    printf("heavy: %.6f\n", sink);

    float x[3] = { 1.5f, -2.0f, 0.25f }, y[3] = { 4.0f, 0.5f, -8.0f };
    printf("dot3: %.6f\n", dot3(x, y));

    struct particle p[4] = {
        { 0, 10, 1, 0, 1.0 }, { 1, 5, -1, 2, 2.0 }, { -3, 1, 0, 0, 0.5 }, { 2, 2, 2, 2, 3.0 },
    };
    for (int t = 0; t < 1000; t++)
        step(p, 4, 0.01f);
    for (int i = 0; i < 4; i++)
        printf("p%d: %.4f %.4f %.4f %.4f %.6f\n", i, p[i].x, p[i].y, p[i].vx, p[i].vy, p[i].mass);

    printf("pressure: %.6f\n", pressure(1000));
    printf("across_calls: %.6f\n", across_calls(200));
    printf("mixed: %.4f\n", mixed(500));
    return 0;
}
