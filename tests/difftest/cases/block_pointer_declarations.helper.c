#include <stdbool.h>
typedef void (^Done)(int status);
typedef bool (^Pred)(const char *key, int value);
int takes_blocks(int a, Done d, double x, Pred p, int b) {
    return a * 1000 + b * 10 + (d == 0) + (p == 0) * 2 + (int)(x * 100);
}
