/* A flag tested again by an inlined helper under the branch that tested it
 * (the second test decided); values reloaded from the stack a few lines
 * apart, with 128-bit overflow results and a register rewritten between;
 * an interpreter loop whose handlers all jump back to its dispatch. */
#include <stdio.h>
#include <stdint.h>

struct mem { uint16_t flags; int64_t i; };

static inline void set_null(struct mem *m) {
    if (m->flags & 0x9000)
        m->flags = 1 | 0x40;
    else
        m->flags = 1;
}

__attribute__((noinline)) static int clear(struct mem *m, int n) {
    int nulls = 0;
    for (int k = 0; k < n; k++) {
        if (m[k].flags & 0x9000) {
            set_null(&m[k]);
            nulls++;
        }
    }
    return nulls;
}

__attribute__((noinline)) static unsigned long add_carry(unsigned long a, unsigned long b) {
    unsigned long i = __builtin_add_overflow(a, b, &a);
    return a + i;
}

__attribute__((noinline)) static unsigned long sub_carry(unsigned long a, unsigned long b) {
    unsigned long i = __builtin_add_overflow(a, b, &a);
    return a - i;
}

enum { OP_ADD, OP_SUB, OP_MUL, OP_JNZ, OP_DUP, OP_SWAP, OP_HALT };

__attribute__((noinline)) static int64_t run(const int *code) {
    int64_t stack[16] = { 0 };
    int sp = 0, pc = 0;
    int64_t steps = 0;
    for (;;) {
        steps++;
        switch (code[pc++]) {
        case OP_ADD: stack[sp] += code[pc++]; continue;
        case OP_SUB: stack[sp] -= code[pc++]; continue;
        case OP_MUL: stack[sp] = stack[sp] * code[pc++] % 1000003; continue;
        case OP_JNZ: { int t = code[pc++]; if (stack[sp] % 7 != 0) pc = t; continue; }
        case OP_DUP: if (sp < 15) { stack[sp + 1] = stack[sp]; sp++; } continue;
        case OP_SWAP: if (sp > 0) { int64_t t = stack[sp]; stack[sp] = stack[sp - 1]; stack[sp - 1] = t; } continue;
        default: return stack[sp] * 31 + steps + sp;
        }
    }
}

int main(void) {
    struct mem m[50];
    for (int k = 0; k < 50; k++)
        m[k] = (struct mem){ (uint16_t)(k * 0x1357), k };
    int nulls = clear(m, 50);
    unsigned h = 0;
    for (int k = 0; k < 50; k++)
        h = h * 33 + m[k].flags;
    printf("clear %d %u\n", nulls, h);
    printf("carry %lu %lu %lu %lu\n", add_carry(16UL, -18UL), add_carry(16UL, -16UL), sub_carry(24UL, -26UL), sub_carry(24UL, -24UL));
    static const int code[] = { OP_ADD, 5, OP_DUP, OP_MUL, 3, OP_ADD, 1, OP_SWAP, OP_SUB, 2, OP_JNZ, 2, OP_HALT };
    printf("run %lld\n", (long long)run(code));
    return 0;
}
