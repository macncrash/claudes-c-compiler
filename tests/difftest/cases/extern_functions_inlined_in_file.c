/* Functions with external linkage that this file also calls, small once
 * cleaned up (SQLite's sqlite3_free and sqlite3_value_type): inlined at
 * the calls here, while keeping their own definitions for other callers,
 * a function pointer among them. A weak one stays a call, since the linker
 * may choose another definition. */
#include <stdio.h>
#include <stdlib.h>

typedef struct { void (*xFree)(void *); int bMemstat; } Config;
struct Stat { long used; long count; };

static int nfreed;
static void count_free(void *p) {
    nfreed++;
    free(p);
}

Config config = { count_free, 0 };
struct Stat stat;

void api_free(void *p) {
    if (p == 0)
        return;
    if (config.bMemstat) {
        stat.used -= 16;
        stat.count--;
        config.xFree(p);
    } else {
        config.xFree(p);
    }
}

typedef struct { unsigned short flags; int n; } Value;

int api_value_type(const Value *v) {
    static const unsigned char aType[] = { 5, 1, 2, 1, 3, 1, 2, 1, 4, 1, 2, 1, 3, 1, 2, 1 };
    return aType[v->flags & 15];
}

__attribute__((weak)) int api_hook(int x) {
    return x * 3 + 1;
}

static void (*const free_fn)(void *) = api_free;

static long __attribute__((noinline)) churn(int n) {
    long t = 0;
    for (int i = 0; i < n; i++) {
        int *p = malloc(sizeof(int) * 4);
        p[0] = i;
        t += p[0];
        if (i % 3 == 0)
            config.bMemstat = !config.bMemstat;
        if (i % 4 == 0) {
            stat.used += 16;
            stat.count++;
        }
        if (i & 1)
            api_free(p);
        else
            free_fn(p);
        api_free(0);
    }
    return t;
}

static int __attribute__((noinline)) classify(const Value *vals, int n) {
    int hist[6] = { 0 };
    for (int i = 0; i < n; i++)
        hist[api_value_type(&vals[i])]++;
    int h = 0;
    for (int k = 0; k < 6; k++)
        h = h * 31 + hist[k];
    return h;
}

int main(void) {
    printf("churn %ld freed %d used %ld count %ld\n", churn(50), nfreed, stat.used, stat.count);
    Value vals[40];
    for (int i = 0; i < 40; i++) {
        vals[i].flags = (unsigned short)(i * 7 + 3);
        vals[i].n = i;
    }
    printf("classify %d\n", classify(vals, 40));
    int (*type_fn)(const Value *) = api_value_type;
    printf("type %d %d\n", type_fn(&vals[3]), api_value_type(&vals[4]));
    printf("hook %d\n", api_hook(5) + api_hook(-2));
    return 0;
}
