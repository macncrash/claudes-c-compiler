/* Loops whose int counter the optimizer replaces with another induction
 * variable (a pointer, or the counter scaled for indexing): bounds and starts
 * from parameters, empty ranges, counting down, steps of 2 with != and <=,
 * several arrays indexed by one counter, a pointer used after the loop,
 * nested loops whose inner start comes from the outer pointer, and loops
 * where the counter is still needed (used after the loop, in arithmetic, or
 * with a long bound). */
#include <stdio.h>

struct rec { int a, b; long c; };

static struct rec recs[64];
static int xs[256], ys[256], zs[256];
static long grid[16][16];

__attribute__((noinline)) static long sum_recs(int lo, int hi) {
    long s = 0;
    struct rec *p = &recs[lo];
    for (int i = lo; i < hi; i++, p++)
        s += p->a * 3 + p->c;
    return s;
}

__attribute__((noinline)) static long down(int hi, int lo) {
    long s = 0;
    struct rec *p = &recs[hi];
    for (int i = hi; i >= lo; i--, p--)
        s = s * 5 + p->b;
    return s;
}

__attribute__((noinline)) static long steps(int n) {
    long s = 0;
    struct rec *p = recs;
    for (int i = 0; i != n; i += 2, p += 2)
        s += p->a - p->b;
    for (int i = 1; i <= n; i += 2)
        s += (long)xs[i] << 1;
    return s;
}

/* One index for three arrays, written through a third. */
__attribute__((noinline)) static void combine(int n) {
    for (int i = 0; i < n; i++)
        zs[i] = xs[i] * 7 - ys[i] + zs[i];
}

/* The pointer itself is used after the loop. */
__attribute__((noinline)) static long find(int n, int x) {
    int *p = xs;
    int i;
    for (i = 0; i < n; i++, p++)
        if (*p == x)
            break;
    return (p - xs) * 1000 + i;
}

/* Inner loops starting from where the outer one points. */
__attribute__((noinline)) static long nested(int rows, int cols) {
    long s = 0;
    for (int r = 0; r < rows; r++) {
        long *row = grid[r];
        for (int c = 0; c < cols; c++)
            s += row[c] * (r + 1);
        for (int c = r; c < cols; c++)
            grid[r][c] += c;
    }
    return s;
}

/* The outer counter indexes one array and steps a pointer into another, which
 * starts each inner loop (the inner loop's phi reads the outer pointer). */
static float fa[64][4], fb[64][4];

__attribute__((noinline)) static void rows(const float *w) {
    for (int i = 0; i < 64; i++)
        for (int k = 0; k < 4; k++)
            fa[i][k] = w[k] * fb[i][k] + fa[i][k] * 0.5f;
}

/* The counter is needed for more than the test. */
__attribute__((noinline)) static long still_counted(int n, long big) {
    long s = 0;
    int i;
    for (i = 0; i < n; i++)
        s += xs[i] * i;
    for (long j = 0; j < big; j++)
        s += ys[j & 255];
    return s + i;
}

int main(int argc, char **argv) {
    (void)argv;
    for (int i = 0; i < 64; i++)
        recs[i] = (struct rec){ i * 3 - 50, 7 - i, (long)i * i };
    for (int i = 0; i < 256; i++) {
        xs[i] = i * 37 % 101 - 40;
        ys[i] = i ^ 0x55;
        zs[i] = i;
    }
    for (int r = 0; r < 16; r++)
        for (int c = 0; c < 16; c++)
            grid[r][c] = r * 16 + c;
    printf("recs %ld %ld %ld %ld\n", sum_recs(0, 64), sum_recs(10, 20), sum_recs(30, 30), sum_recs(40, 35));
    printf("down %ld %ld\n", down(63, 0), down(5, 9));
    printf("steps %ld %ld\n", steps(20), steps(0));
    combine(200 + argc);
    long z = 0;
    for (int i = 0; i < 256; i++)
        z = z * 31 + zs[i];
    printf("combine %ld\n", z);
    printf("find %ld %ld\n", find(256, xs[77]), find(256, 12345));
    printf("nested %ld %ld\n", nested(16, 16), nested(3, 0));
    printf("nested2 %ld\n", nested(5, 11));
    printf("counted %ld\n", still_counted(100, 300));
    float w[4] = { 0.5f, -1.0f, 2.0f, 0.25f };
    for (int i = 0; i < 64; i++)
        for (int k = 0; k < 4; k++) {
            fa[i][k] = i - k * 3.5f;
            fb[i][k] = i * 0.25f + k;
        }
    rows(w);
    rows(w);
    printf("rows %g %g %g\n", fa[0][1], fa[31][2], fa[63][3]);
    return 0;
}
