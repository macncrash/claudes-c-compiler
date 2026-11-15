/* Block pointer types (Apple blocks) in declarations: typedefs, struct
 * members, parameters (named and abstract), arrays, with nullability
 * qualifiers. ccc did not parse R (^name)(params) outside parameter lists, so
 * the CoreGraphics headers failed; in parameters it gave the block the type
 * R itself. Blocks are not supported, but a block pointer is one pointer in
 * the ABI, so ccc takes it as an opaque pointer: stored, passed (here to a
 * helper the reference compiler builds) and compared. (Block literals are
 * still a syntax error, and calling a block value is not diagnosed.) */
#include <stdio.h>
#include <stddef.h>
#include <stdbool.h>

typedef void (^Done)(int status);
typedef bool (^Pred)(const char *key, int value);
typedef char *(^Mk)(int);
typedef void (^ _Nullable MaybeDone)(int);
struct Job { char tag; Done done; int prio; Pred filter; double w; int (^inline_block)(void); };
int takes_blocks(int a, Done d, double x, Pred p, int b);
void abstract_param(void (^)(int));
void named_param(int (^ _Nonnull cb)(int), int n);

int main(void) {
    struct Job j = { 'q', NULL, 7, NULL, 2.5, NULL };
    printf("sizes %zu %zu %zu %zu %zu\n", sizeof(Done), sizeof(Pred), sizeof(Mk), sizeof(MaybeDone), sizeof(struct Job));
    printf("offsets %zu %zu %zu %zu %zu\n", offsetof(struct Job, done), offsetof(struct Job, prio),
           offsetof(struct Job, filter), offsetof(struct Job, w), offsetof(struct Job, inline_block));
    printf("null %d %d\n", j.done == NULL, j.filter == 0);
    printf("call %d\n", takes_blocks(11, j.done, 1.5, j.filter, 22));
    Done arr[3] = { 0 };
    printf("array %zu %d\n", sizeof arr, arr[2] == NULL);
    return 0;
}
