/* Variadic macros, found by a variadic macro fuzzer:
 * - __VA_OPT__ (C23, a GNU extension before) was left unexpanded;
 * - #__VA_ARGS__ and S(__VA_ARGS__) rejoined the arguments as "a, b", losing
 *   the spacing written around the commas ("a,b , c");
 * - , ## __VA_ARGS__ dropped the comma when a variable argument was given
 *   but empty (M(f, )), and the space before the first variable argument.
 * With the same macros through indirection and argument counting. */
#include <stdio.h>
#define S_(...) #__VA_ARGS__
#define S(...) S_(__VA_ARGS__)
#define P(...) printf("%d [%s]\n", __LINE__, S(__VA_ARGS__))
#define CNT_(_1, _2, _3, _4, _5, _6, _7, _8, N, ...) N
#define CNT(...) CNT_(__VA_ARGS__, 8, 7, 6, 5, 4, 3, 2, 1, 0)
#define CNT0(...) CNT_(x, ##__VA_ARGS__, 7, 6, 5, 4, 3, 2, 1, 0)
#define GL(fmt, ...) sum(0, fmt, ##__VA_ARGS__)
#define OPT(a, ...) f(a __VA_OPT__(,) __VA_ARGS__)
#define OPT2(...) [__VA_OPT__(x __VA_ARGS__ y)]
#define OPT3(a, ...) __VA_OPT__(#a) z
#define NAMED(fmt, args...) g(fmt, ## args)
#define NAMED2(args...) #args
#define FIRST(a, ...) a
#define REST(a, ...) __VA_ARGS__
#define EMPTY()
#define DEFER(id) id EMPTY()
#define EXPAND(...) __VA_ARGS__
#define A() 123
#define CAT(a, ...) a ## __VA_ARGS__
#define CAT2(a, b, ...) a ## b ## __VA_ARGS__
#define APPLY(m, ...) m(__VA_ARGS__)
#define ID(x) x
#define COMMA ,
#define SP(...) S_( __VA_ARGS__ )
#define SP2(x, ...) S_(x/**/,  __VA_ARGS__   )
#define TWO(a, b) <a|b>
static int sum(int z, ...) { (void)z; return 0; }
int main(void) {
  P(CNT()); P(CNT(a)); P(CNT(a, b)); P(CNT(a, b, c, d, e, f, g, h)); P(CNT(,)); P(CNT(, , ));
  P(CNT0()); P(CNT0(a)); P(CNT0(a, b)); P(CNT0(a, (b, c), d));
  P(GL("x")); P(GL("x", 1)); P(GL("x", 1, 2)); P(GL("x", )); P(GL("x",,));
  P(OPT(1)); P(OPT(1, 2)); P(OPT(1, 2, 3)); P(OPT(1, )); P(OPT2(a)); P(OPT2(a, b)); P(OPT3(q)); P(OPT3(q, r));
  P(NAMED("f")); P(NAMED("f", 1)); P(NAMED("f", 1, 2)); P(NAMED2()); P(NAMED2(a,b ,  c));
  P(FIRST(1, 2, 3)); P(REST(1, 2, 3)); P(REST(1)); P(FIRST((a, b), c));
  P(DEFER(A)()); P(EXPAND(DEFER(A)())); P(CAT(x, y)); P(CAT(x)); P(CAT(x, y, z)); P(CAT2(a, b)); P(CAT2(a, b, c, d));
  P(APPLY(TWO, 1, 2)); P(APPLY(ID, (1, 2))); P(APPLY(TWO, ID(1) COMMA 3)); P(ID(TWO EMPTY() (4, 5)));
  P(SP(  a  ,  b  )); P(SP()); P(SP2(a)); P(SP2(a, b  , "s, t" , 'c')); P(S_(a "\n" '\'' , )); P(S_(  ));
  P(S_(__VA_ARGS__)); P(CNT(S_(a, b))); P(S(__LINE__, x ));
  P(EXPAND(EXPAND(1, 2), EXPAND())); P(EXPAND(,)); P(FIRST(,)); P(REST(, x));
  printf("%d %d %d\n", CNT(1, 2, 3), CNT0(), GL("q", 3, 4));
  return 0;
}
