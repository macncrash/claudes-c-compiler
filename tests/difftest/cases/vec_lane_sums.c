/* Vectors loaded from memory, multiplied, then read one lane at a time
 * into scalar sums (a vectorized reduction kept in order). Such a vector
 * is a temporary held in a register only between its load or op and its
 * last lane read, so a dozen live at once take registers past v28-v31
 * and leave the rest to the float sums; a float lane goes straight into
 * a float register. Lanes of every size, 8- and 16-byte vectors, a lane
 * read in a later block (not a temporary), and a lane loaded at an
 * offset that is not its own size (not a lane read). */
#include <stdio.h>
#include <string.h>

typedef float f4 __attribute__((vector_size(16)));
typedef float f2 __attribute__((vector_size(8)));
typedef double d2 __attribute__((vector_size(16)));
typedef int i4 __attribute__((vector_size(16)));
typedef unsigned short s8 __attribute__((vector_size(16)));
typedef unsigned char u8x8 __attribute__((vector_size(8)));

static f4 ld4(const float *p) { f4 v; memcpy(&v, p, sizeof v); return v; }

__attribute__((noinline)) void sums6(const float *a, const float *b, const float *c, int n, float out[6])
{
    float s0 = 0, s1 = 1, s2 = 2, s3 = 0, s4 = 0, s5 = -1;
    int w = 0;
    for (; w + 4 <= n; w += 4) {
        f4 x = ld4(a + w), y = ld4(b + w), z = ld4(c + w);
        f4 xy = x * y, yz = y * z, xz = x * z;
        f4 p0 = xy * z, p1 = yz - x, p2 = xz + y, p3 = xy * xy, p4 = yz * xz, p5 = (x - y) * (y - z);
        s0 += p0[0]; s0 += p0[1]; s0 += p0[2]; s0 += p0[3];
        s1 -= p1[0]; s1 -= p1[1]; s1 -= p1[2]; s1 -= p1[3];
        s2 += p2[0]; s2 += p2[1]; s2 += p2[2]; s2 += p2[3];
        s3 += p3[0]; s3 += p3[1]; s3 += p3[2]; s3 += p3[3];
        s4 -= p4[0]; s4 -= p4[1]; s4 -= p4[2]; s4 -= p4[3];
        s5 += p5[0]; s5 += p5[1]; s5 += p5[2]; s5 += p5[3];
    }
    for (; w < n; w++) {
        s0 += a[w] * b[w] * c[w];
        s1 -= b[w] * c[w] - a[w];
    }
    out[0] = s0; out[1] = s1; out[2] = s2; out[3] = s3; out[4] = s4; out[5] = s5;
}

__attribute__((noinline)) double dsum(const double *a, const double *b, int n)
{
    double s = 0.5, t = 0;
    for (int w = 0; w + 2 <= n; w += 2) {
        d2 x, y;
        memcpy(&x, a + w, 16);
        memcpy(&y, b + w, 16);
        d2 p = x * y, q = x - y;
        s += p[0]; s += p[1];
        t -= q[1]; t -= q[0];
    }
    return s * 3 + t;
}

__attribute__((noinline)) long isum(const int *a, const short *h, int n)
{
    long s = 0;
    for (int w = 0; w + 8 <= n; w += 8) {
        i4 x, y;
        s8 z;
        memcpy(&x, a + w, 16);
        memcpy(&y, a + w + 4, 16);
        memcpy(&z, h + w, 16);
        i4 p = x * y + 3;
        s8 q = z * z - z;
        s += p[0] - p[1] + (long)p[2] * p[3];
        s += q[0] + q[1] + q[2] + q[3] + q[4] + q[5] + q[6] + q[7];
    }
    return s;
}

__attribute__((noinline)) float f2sum(const float *a, int n)
{
    float s = 0;
    for (int w = 0; w + 2 <= n; w += 2) {
        f2 x;
        memcpy(&x, a + w, 8);
        f2 p = x * x;
        p = p + x;
        s += p[1];
        s += p[0];
    }
    return s;
}

/* The lane read is in another block: the vector is not a temporary. */
__attribute__((noinline)) float later_block(const float *a, int k)
{
    f4 x = ld4(a) * 2.0f;
    if (k > 0)
        return x[k & 3] + x[0];
    return x[3] - x[1];
}

/* Bytes of an 8-byte vector, and a 2-byte load at an odd offset: not a
 * lane, so that vector stays in memory. */
__attribute__((noinline)) unsigned bytes(const unsigned char *p)
{
    u8x8 v, w;
    memcpy(&v, p, 8);
    w = v + v;
    unsigned short odd;
    memcpy(&odd, (char *)&w + 3, 2);
    return w[0] + w[7] * 3u + v[5] + odd;
}

int main(void)
{
    float a[37], b[37], c[37];
    double da[20], db[20];
    int ia[32];
    short ha[32];
    unsigned char ub[8];
    for (int i = 0; i < 37; i++) {
        a[i] = (float)(i % 7) * 0.375f - 1.0f;
        b[i] = (float)((i * 5) % 11) * 0.125f + 0.5f;
        c[i] = (float)((i * 3) % 13) * -0.25f + 2.0f;
    }
    for (int i = 0; i < 20; i++) {
        da[i] = i * 0.1 - 0.7;
        db[i] = 1.0 / (i + 1);
    }
    for (int i = 0; i < 32; i++) {
        ia[i] = i * 37 - 500;
        ha[i] = (short)(i * 91 - 1400);
    }
    for (int i = 0; i < 8; i++)
        ub[i] = (unsigned char)(i * 41 + 7);
    for (int n = 0; n <= 37; n += 5) {
        float o[6];
        sums6(a, b, c, n, o);
        printf("%d: %a %a %a %a %a %a\n", n, o[0], o[1], o[2], o[3], o[4], o[5]);
    }
    printf("%a %a\n", dsum(da, db, 20), dsum(da, db, 7));
    printf("%ld %ld\n", isum(ia, ha, 32), isum(ia, ha, 15));
    printf("%a %a\n", f2sum(a, 37), f2sum(b, 10));
    printf("%a %a %a\n", later_block(a, 2), later_block(b, 5), later_block(c, 0));
    printf("%u\n", bytes(ub));
    return 0;
}
