/* __typeof__ of a statement expression whose locals shadow names of the
 * enclosing scope: the operand of typeof is never evaluated, so sema never
 * walked it, and its last expression was typed with the outer symbols.
 * The nested kernel max(i, max(d, 0.5)) typed the inner _x as the outer
 * int (2 instead of 2.75); older builds also emitted fcmp d16, s1. */
#include <stdio.h>

#define max(a, b) ({ __typeof__(a) _x = (a); __typeof__(b) _y = (b); _x > _y ? _x : _y; })
#define min(a, b) ({ __typeof__(a) _x = (a); __typeof__(b) _y = (b); _x < _y ? _x : _y; })
#define twice(a) ({ __typeof__(a) _x = (a); _x + _x; })

__attribute__((noinline)) double f1(int i, double d) { return max(i, max(d, 0.5)); }
__attribute__((noinline)) long f2(char c, long l) { return max(c, max(l, 3L)); }
__attribute__((noinline)) double f3(float f, double d) { float _x = f; return max(f, twice(d)) + _x; }
__attribute__((noinline)) int f4(double d) { return sizeof(({ int _x = 1; ({ double _x = d; _x; }); })); }
__attribute__((noinline)) double f5(int i, double d)
{
    int _x = i;
    __typeof__(({ double _x = d; _x; })) r = d * 0.25;
    return r + _x;
}
__attribute__((noinline)) double f6(int i, double d)
{
    int _x = i;
    __auto_type r = ({ double _x = d; _x * 0.5; });
    return r + _x;
}
__attribute__((noinline)) double f7(short s, float f, double d) { return min(s, max(f, min(d, 9.5))); }
__attribute__((noinline)) unsigned long long f8(unsigned char c, unsigned long long u)
{
    return max(c, max(u, 1ULL << 40));
}
__attribute__((noinline)) double f9(int n, double d)
{
    double v[2] = { d, d * 3 };
    double *p = v;
    int _x = n;
    __typeof__(*({ double *_x = p + 1; _x; })) w = 0.125;
    return w + v[1] + _x;
}
__attribute__((noinline)) int f10(int i, double d)
{
    return (int)sizeof(__typeof__(max(i, max(d, 0.5)))) * 10 + (int)sizeof(max(i, min(i, 2)));
}

int main(void)
{
    printf("%g %ld %g %d %g %g\n", f1(1, 2.75), f2(5, 10000000000L), f3(1.5f, 0.75), f4(1.0),
           f5(3, 1.5), f6(3, 1.5));
    printf("%g %g %llu %g %d\n", f7(20, 2.5f, 7.25), f7(-4, -8.5f, 1.0), f8(200, 5), f9(2, 1.5),
           f10(1, 2.0));
    return 0;
}
