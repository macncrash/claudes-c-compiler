/* Functions that return function pointers, declared here (with nested
 * declarators, not typedefs) and defined in decl_fn_returning_fptr.helper.c,
 * so every call goes through the declaration's signature. */
#include <stdio.h>

struct s { long a, b; };

double twice(int x);
double sum(struct s v);
int one(void);

double (*get_d(void))(int);
double (*get_s(void))(struct s);
int (*get_i(int n))(void);
double (*(*get_get_d(void))(void))(int);

/* A variable of the same shape must stay a variable, not a function. */
double (*(*pick)(int))(struct s);
/* Three levels, an array of them, and extra indirection. */
double (*(*(*pick3)(char))(int))(struct s);
double (*(*picks[2])(int))(struct s);
double (**(*pick_pp)(int))(struct s);

static double (*pick_impl(int n))(struct s) { (void)n; return sum; }
static double (*(*pick3_impl(char c))(int))(struct s) { (void)c; return pick_impl; }
static double (*sum_ptr)(struct s) = sum;
static double (**pick_pp_impl(int n))(struct s) { (void)n; return &sum_ptr; }

int main(void) {
    struct s v = {3, 4};
    printf("get_d %d %.1f\n", get_d() == twice, get_d()(5));
    printf("get_s %d %.1f\n", get_s() == sum, get_s()(v));
    printf("get_i %d %d\n", get_i(7) == one, get_i(7)());
    printf("get_get_d %d %.1f\n", get_get_d()() == twice, get_get_d()()(6));
    {
        double (*get_d(void))(int);   /* block-scope redeclaration */
        printf("block get_d %d\n", get_d() == twice);
    }
    pick = pick_impl;
    printf("pick %d %.1f\n", pick(1) == sum, pick(1)(v));
    pick3 = pick3_impl;
    printf("pick3 %d %.1f\n", pick3('x')(2) == sum, pick3('x')(2)(v));
    picks[1] = pick_impl;
    printf("picks %d %.1f\n", picks[1](3) == sum, picks[1](3)(v));
    pick_pp = pick_pp_impl;
    printf("pick_pp %d %.1f\n", *pick_pp(4) == sum, (*pick_pp(4))(v));
    return 0;
}
