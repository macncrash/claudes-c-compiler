struct s { long a, b; };

double twice(int x) { return x * 2.0; }
double sum(struct s v) { return v.a + v.b * 10; }
int one(void) { return 1; }

double (*get_d(void))(int) { return twice; }
double (*get_s(void))(struct s) { return sum; }
int (*get_i(int n))(void) { (void)n; return one; }
double (*(*get_get_d(void))(void))(int) { return get_d; }
