/* Global data: relocations in initializers, tentative/common definitions,
 * static locals, string tables, function pointer tables. */
#include <stdio.h>

int counter;
int initialized = 42;
static int hidden = 7;
const char *names[] = {"zero", "one", "two", "three"};
int table[4] = {1, 2, 3, 4};
int *ptrs[] = {&table[0], &table[3], &initialized};
struct entry { const char *name; int (*fn)(int); int *slot; };
static int dbl(int x) { return x * 2; }
static int sq(int x) { return x * x; }
struct entry entries[] = {{"dbl", dbl, &counter}, {"sq", sq, &hidden}};
char big_zero[100000];
const double consts[] = {1.5, -2.25, 1e300};
long long offsets = (long long)(&table[2] - &table[0]);

static int next(void) { static int n = 10; return n++; }

int main(void) {
    counter = 5;
    for (int i = 0; i < 4; i++) printf("%s=%d ", names[i], table[i]);
    printf("\n%d %d %d\n", *ptrs[0], *ptrs[1], *ptrs[2]);
    for (int i = 0; i < 2; i++) printf("%s(%d)=%d slot=%d\n", entries[i].name, 6, entries[i].fn(6), *entries[i].slot);
    big_zero[99999] = 3;
    printf("%d %d\n", big_zero[0], big_zero[99999]);
    printf("%g %g %g\n", consts[0], consts[1], consts[2]);
    printf("%d %d %d\n", next(), next(), next());
    printf("%lld\n", offsets);
    return 0;
}
