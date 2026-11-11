/* Loops adding float terms into sums, which reduce_vectorize does four
 * iterations at a time with each sum's additions in order, so the result
 * is the scalar loop's exactly. Products are their own statements, so
 * neither compiler fuses them into the sums. Counters of each form the
 * pass meets (an int, a pointer after lftr, a byte offset), starts and
 * lengths that leave 0-3 iterations to the scalar loop, cos/sin pairs
 * deinterleaved, arrays of one struct, a broadcast operand, subtraction,
 * division, negation, two updates of one sum, and loops it must leave
 * alone: a store, a sum read by another, an unsigned counter. */
#include <stdio.h>

struct Waves { int n; float k[64], amp[64], dx[64], dz[64]; };

__attribute__((noinline)) float plain(const float *a, int n)
{
    float s = 0.25f;
    for (int i = 0; i < n; i++)
        s += a[i];
    return s;
}

__attribute__((noinline)) float from_m(const float *a, const float *b, int m, int n)
{
    float s = -1.0f, t = 2.0f;
    for (int i = m; i < n; i++) {
        float p = a[i] * b[i + 1];
        float q = a[i + 3] / (b[i] + 3.0f);
        s -= p;
        t = t + q;
        t = (-a[i + 2]) + t;
    }
    return s * 3.0f + t;
}

/* The ocean simulation's inner loop: cos/sin pairs of two tables, one
 * struct's arrays, six sums. */
__attribute__((noinline)) void gerstner(const struct Waves *o, const float *col, const float *row, int nw, float out[6])
{
    float px = 0.5f, py = 0.0f, pz = -0.5f, nx = 0.0f, ny = 1.0f, nz = 0.0f;
    for (int w = 0; w < nw; w++) {
        float cc = col[2 * w], cs = col[2 * w + 1];
        float rc = row[2 * w], rs = row[2 * w + 1];
        float c1 = cc * rc, c2 = cs * rs, s1 = cs * rc, s2 = cc * rs;
        float c = c1 - c2, s = s1 + s2;
        float a = o->amp[w];
        float wa = o->k[w] * a;
        float tpx = a * o->dx[w], tpz = a * o->dz[w];
        float t0 = tpx * c, t1 = tpz * c, t2 = a * s;
        float t3 = o->dx[w] * wa, t4 = o->dz[w] * wa;
        float t5 = t3 * c, t6 = t4 * c, t7 = wa * s;
        px += t0;
        pz += t1;
        py += t2;
        nx -= t5;
        nz -= t6;
        ny -= t7;
    }
    out[0] = px; out[1] = py; out[2] = pz; out[3] = nx; out[4] = ny; out[5] = nz;
}

__attribute__((noinline)) float scaled(const float *a, float k, const float *b, int n)
{
    float s = 0.0f;
    for (int i = 0; i < n; i++) {
        float t = a[i] * k;
        float u = t * b[7];
        s += u;
    }
    return s;
}

/* Not vectorized: a store, a sum read by another, an unsigned counter. */
__attribute__((noinline)) float stores(const float *a, float *dst, int n)
{
    float s = 0.0f;
    for (int i = 0; i < n; i++) {
        s += a[i];
        dst[i & 7] = s;
    }
    return s;
}

__attribute__((noinline)) float coupled(const float *a, int n)
{
    float s = 0.0f, t = 1.0f;
    for (int i = 0; i < n; i++) {
        s += a[i];
        float h = s * 0.5f;
        t += h;
    }
    return s + t;
}

__attribute__((noinline)) float unsigned_count(const float *a, unsigned n)
{
    float s = 0.0f;
    for (unsigned i = 0; i < n; i++)
        s += a[i];
    return s;
}

int main(void)
{
    static float a[200], b[200], col[200], row[200], dst[8];
    static struct Waves o;
    for (int i = 0; i < 200; i++) {
        a[i] = (float)((i * 37) % 29) / 7.0f - 1.5f;
        b[i] = 0.5f + (float)((i * 11) % 23) / 13.0f;
        col[i] = (float)((i * 5) % 17) / 9.0f - 0.9f;
        row[i] = (float)((i * 3) % 19) / 10.0f - 0.8f;
    }
    for (int w = 0; w < 64; w++) {
        o.k[w] = 0.1f + (float)w * 0.03f;
        o.amp[w] = 1.0f / (float)(w + 2);
        o.dx[w] = (float)((w * 7) % 11) / 11.0f - 0.5f;
        o.dz[w] = (float)((w * 5) % 13) / 13.0f - 0.4f;
    }
    int lens[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 15, 24, 33, 64 };
    for (unsigned q = 0; q < sizeof lens / sizeof lens[0]; q++) {
        int n = lens[q];
        float g[6];
        gerstner(&o, col, row, n, g);
        printf("n=%d: %a %a %a %a\n", n, plain(a, n), from_m(a, b, 0, n), from_m(a, b, 3, n), scaled(a, 0.75f, b, n));
        printf("  %a %a %a %a %a %a\n", g[0], g[1], g[2], g[3], g[4], g[5]);
        printf("  %a %a %a\n", stores(a, dst, n), coupled(a, n), unsigned_count(a, (unsigned)n));
    }
    return 0;
}
