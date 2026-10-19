/* Calls between ccc code and clang-compiled helpers (abi_args.helper.c):
 * many args (stack spill), mixed int/float, small/large/HFA structs. */
#include <stdio.h>

struct small { int a, b; };
struct pair { long a, b; };
struct big { long a, b, c, d; };
struct hfa { double x, y, z; };
struct hfa_f { float x, y; };
struct mixed { char c; short s; int i; };

long h_many_ints(long a, long b, long c, long d, long e, long f, long g, long h, long i, long j);
double h_many_doubles(double a, double b, double c, double d, double e, double f, double g, double h, double i, double j);
long h_small_stack(long a, long b, long c, long d, long e, long f, long g, long h, char i, short j, int k);
long h_small(struct small s);
long h_pair(struct pair p);
long h_big(struct big b);
double h_hfa(struct hfa h);
float h_hfa_f(struct hfa_f h);
long h_mixed(struct mixed m);
struct small h_ret_small(int a, int b);
struct pair h_ret_pair(long a, long b);
struct big h_ret_big(long a);
struct hfa h_ret_hfa(double a);
long h_call_back(long (*fn)(long, double, struct pair), long x);

static long cb(long a, double d, struct pair p) { return a + (long)(d * 10) + p.a * 100 + p.b * 1000; }

int main(void) {
    printf("many_ints %ld\n", h_many_ints(1, 2, 3, 4, 5, 6, 7, 8, 9, 10));
    printf("many_doubles %.2f\n", h_many_doubles(1, 2, 3, 4, 5, 6, 7, 8, 9.5, 10.25));
    printf("small_stack %ld\n", h_small_stack(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11));
    printf("small %ld\n", h_small((struct small){3, 4}));
    printf("pair %ld\n", h_pair((struct pair){5, 6}));
    printf("big %ld\n", h_big((struct big){1, 2, 3, 4}));
    printf("hfa %.2f\n", h_hfa((struct hfa){1.5, 2.5, 3.5}));
    printf("hfa_f %.2f\n", h_hfa_f((struct hfa_f){1.25f, 2.5f}));
    printf("mixed %ld\n", h_mixed((struct mixed){'a', 300, 70000}));
    struct small s = h_ret_small(7, 8); printf("ret_small %d %d\n", s.a, s.b);
    struct pair p = h_ret_pair(9, 10); printf("ret_pair %ld %ld\n", p.a, p.b);
    struct big b = h_ret_big(11); printf("ret_big %ld %ld %ld %ld\n", b.a, b.b, b.c, b.d);
    struct hfa h = h_ret_hfa(0.5); printf("ret_hfa %.2f %.2f %.2f\n", h.x, h.y, h.z);
    printf("callback %ld\n", h_call_back(cb, 2));
    return 0;
}
