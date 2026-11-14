/* An array of zero-size structs (GNU empty structs, or structs of only
 * zero-width bit-fields) declared as an object is empty: ccc gave it a byte
 * per element (sizeof of struct Z[2] was 2), though sizeof of the type name
 * and arrays inside structs were right. Found by the bit-field layout fuzzer. */
#include <stdio.h>
struct Z { int : 0; };
struct E { };
struct __attribute__((packed)) P { long long : 0; int : 0; };
static struct Z gz[2] = { {}, {} };
static struct E ge[3];
static struct P gp[2] = { {}, {} };
struct H { int a; struct E e[4]; int b; };
static struct H gh = { 1, {}, 2 };
int main(void) {
  struct Z lz[2] = { {}, {} };
  struct E le[3];
  printf("%zu %zu %zu %zu %zu %zu %zu %zu\n", sizeof(struct Z), sizeof gz, sizeof ge, sizeof gp, sizeof lz, sizeof le, sizeof(struct E[5]), sizeof gh);
  printf("%d %d %ld\n", gh.a, gh.b, (long)((char *)&gh.b - (char *)&gh));
  return 0;
}
