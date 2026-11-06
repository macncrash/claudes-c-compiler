/* A float or double loaded twice is loaded once, unless something may have
 * changed it, and a sum in a loop is updated in place, unless its old value is
 * still wanted. All the values are exact, so that fusing a multiply and an add
 * changes nothing here. */
#include <stdio.h>

struct wave { float a[4], b[4]; double d[2]; };

/* The store may or may not hit what was loaded. */
static float __attribute__((noinline)) through_alias(float *p, float *q)
{
    float x = *p;
    *q = 7.0f;
    return x * 10.0f + *p;
}

static double __attribute__((noinline)) through_alias_d(double *p, double *q)
{
    double x = *p;
    *q = 7.0;
    return x * 10.0 + *p;
}

/* A char store may change anything. */
static float __attribute__((noinline)) through_bytes(float *p, unsigned char *b)
{
    float x = *p;
    b[3] = 0x40;
    return x + *p;
}

static float g = 3.0f;

static void __attribute__((noinline)) bump(void)
{
    g += 1.0f;
}

static float __attribute__((noinline)) across_a_call(void)
{
    float x = g;
    bump();
    return x * 100.0f + g;
}

static float __attribute__((noinline)) volatile_twice(volatile float *p)
{
    float x = *p;
    *p = x + 1.0f;
    return x + *p;
}

/* Copies of a NaN are still NaN, and still unequal. */
static int __attribute__((noinline)) nan_twice(float *p)
{
    float x = *p, y = *p;
    return (x == y) * 2 + (x != y);
}

/* Repeated loads in a loop with no store. */
static float __attribute__((noinline)) repeated(const struct wave *w, int n)
{
    float px = 0, py = 0;
    for (int i = 0; i < n; i++) {
        px += w->a[i] * w->b[i];
        py += w->a[i] + w->b[i];
        px -= w->b[i] * 2.0f;
    }
    return px * 1000.0f + py;
}

/* Sums updated in place, side by side. */
static double __attribute__((noinline)) six_sums(const float *v, int n)
{
    float a = 0, b = 1, c = 2, d = 3, e = 4, f = 5;
    for (int i = 0; i < n; i++) {
        float x = v[i];
        a += x * 2.0f;
        b -= x * 3.0f;
        c += x;
        d = d + x * x;
        e = e - x;
        f += 1.0f;
    }
    return a + b * 10 + c * 100 + d * 1000 + e * 10000 + (double)f * 100000;
}

/* The old sum is read after the new one is made. */
static float __attribute__((noinline)) old_sum_read(const float *v, int n)
{
    float s = 0, t = 0;
    for (int i = 0; i < n; i++) {
        float next = s + v[i];
        t += s;
        s = next;
    }
    return s * 1000.0f + t;
}

/* Each value becomes the other's: neither copy can go. */
static double __attribute__((noinline)) swapping(int n)
{
    double a = 1, b = 2;
    for (int i = 0; i < n; i++) {
        double t = a + b;
        a = b;
        b = t;
    }
    return a * 1000 + b;
}

/* Rotating three sums. */
static float __attribute__((noinline)) rotating(int n)
{
    float a = 1, b = 2, c = 3;
    for (int i = 0; i < n; i++) {
        float x = b + 1, y = c + 1, z = a + 1;
        a = x;
        b = y;
        c = z;
    }
    return a * 10000 + b * 100 + c;
}

/* The new value is also used after the loop's copy. */
static float __attribute__((noinline)) new_value_kept(const float *v, int n)
{
    float s = 0, last = 0;
    for (int i = 0; i < n; i++) {
        float t = s + v[i];
        s = t;
        last = t * 2.0f;
    }
    return s * 1000.0f + last;
}

/* A sum in a condition, and one that a branch sometimes leaves alone. */
static float __attribute__((noinline)) branching(const float *v, int n)
{
    float s = 0, u = 0;
    for (int i = 0; i < n; i++) {
        if (v[i] > 2.0f)
            s += v[i];
        else
            u -= v[i];
        if (s > 100.0f)
            break;
    }
    return s * 1000.0f + u;
}

int main(void)
{
    float f[2] = { 2.0f, 5.0f };
    double d[2] = { 2.0, 5.0 };
    float v[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    struct wave w = { { 1, 2, 3, 4 }, { 5, 6, 7, 8 }, { 1, 2 } };
    union { float f; unsigned u; } nan = { .u = 0x7fc00000u };
    float one = 1.5f;
    volatile float vol = 2.0f;

    printf("%g %g\n", through_alias(&f[0], &f[1]), through_alias(&f[0], &f[0]));
    f[0] = 2.0f;
    printf("%g %g\n", through_alias_d(&d[0], &d[1]), through_alias_d(&d[0], &d[0]));
    printf("%g\n", through_bytes(&one, (unsigned char *)&one));
    printf("%g %g\n", across_a_call(), g);
    printf("%g %g\n", volatile_twice(&vol), vol);
    printf("%d\n", nan_twice(&nan.f));
    printf("%g\n", repeated(&w, 4));
    printf("%g\n", six_sums(v, 8));
    printf("%g\n", old_sum_read(v, 8));
    printf("%g\n", swapping(10));
    printf("%g\n", rotating(7));
    printf("%g\n", new_value_kept(v, 8));
    printf("%g\n", branching(v, 8));
    return 0;
}
