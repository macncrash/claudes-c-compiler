struct small { int a, b; };
struct pair { long a, b; };
struct big { long a, b, c, d; };
struct hfa { double x, y, z; };
struct hfa_f { float x, y; };
struct mixed { char c; short s; int i; };

long h_many_ints(long a, long b, long c, long d, long e, long f, long g, long h, long i, long j) {
    return a + 2*b + 3*c + 4*d + 5*e + 6*f + 7*g + 8*h + 9*i + 10*j;
}
double h_many_doubles(double a, double b, double c, double d, double e, double f, double g, double h, double i, double j) {
    return a + b + c + d + e + f + g + h + i * 2 + j * 3;
}
long h_small_stack(long a, long b, long c, long d, long e, long f, long g, long h, char i, short j, int k) {
    return a + b + c + d + e + f + g + h + i * 100 + j * 1000 + k * 10000;
}
long h_small(struct small s) { return s.a * 10 + s.b; }
long h_pair(struct pair p) { return p.a * 10 + p.b; }
long h_big(struct big b) { return b.a + b.b * 10 + b.c * 100 + b.d * 1000; }
double h_hfa(struct hfa h) { return h.x + h.y * 2 + h.z * 3; }
float h_hfa_f(struct hfa_f h) { return h.x + h.y * 2; }
long h_mixed(struct mixed m) { return m.c + m.s * 1000L + m.i * 1000000L; }
struct small h_ret_small(int a, int b) { struct small s = {a, b}; return s; }
struct pair h_ret_pair(long a, long b) { struct pair p = {a, b}; return p; }
struct big h_ret_big(long a) { struct big b = {a, a + 1, a + 2, a + 3}; return b; }
struct hfa h_ret_hfa(double a) { struct hfa h = {a, a * 2, a * 3}; return h; }
long h_call_back(long (*fn)(long, double, struct pair), long x) { struct pair p = {3, 4}; return fn(x, 1.5, p); }
