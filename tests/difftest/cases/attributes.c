/* Constructors/destructors, weak definitions, aliases, aligned and packed data. */
#include <stdio.h>
#include <stdint.h>

static int order;
__attribute__((constructor)) static void ctor(void) { order = order * 10 + 1; }
__attribute__((destructor)) static void dtor(void) { printf("dtor ran\n"); }

__attribute__((weak)) int weak_fn(void) { return 1; }

struct __attribute__((packed)) packed { char c; int i; short s; };
struct aligned { char c; } __attribute__((aligned(64)));
__attribute__((aligned(32))) static int aligned_var = 3;

static __attribute__((noinline)) int add(int a, int b) { return a + b; }

int main(void) {
    printf("order %d\n", order);
    printf("weak_fn %d\n", weak_fn());
    printf("packed %zu aligned %zu\n", sizeof(struct packed), sizeof(struct aligned));
    printf("aligned_var mod32 %d val %d\n", (int)((uintptr_t)&aligned_var % 32), aligned_var);
    struct packed p = {1, 0x12345678, 0x0102};
    printf("%x %x\n", p.i, p.s);
    printf("%d\n", add(2, 3));
    return 0;
}
