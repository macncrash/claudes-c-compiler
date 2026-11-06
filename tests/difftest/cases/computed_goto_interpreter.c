/* Computed gotos, the way bytecode interpreters use them: a static table of
 * label addresses, a goto at the end of every opcode, many variables live
 * through all of it, and labels used before they are defined. Each goto could
 * reach every label, so every variable merged at every label and each goto
 * copied them all: 3,000 loads and stores per opcode in oniguruma's matcher.
 * Now all of them go through one block. */
#include <stdio.h>

enum { OP_PUSH, OP_ADD, OP_MUL, OP_DUP, OP_SWAP, OP_JNZ, OP_DEC, OP_ACC, OP_FLOAT, OP_CALL, OP_END, OP_COUNT };

static int calls;
static int __attribute__((noinline)) helper(int x)
{
    calls++;
    return x * 2 + 1;
}

static long __attribute__((noinline)) run(const int *code, int seed)
{
    static const void *const table[OP_COUNT] = {
        &&op_push, &&op_add, &&op_mul, &&op_dup, &&op_swap, &&op_jnz, &&op_dec, &&op_acc, &&op_float, &&op_call, &&op_end,
    };
    int stack[64], sp = 0;
    const int *pc = code;
    long acc = seed, a1 = 1, a2 = 2, a3 = 3, a4 = 4, a5 = 5, a6 = 6, a7 = 7, a8 = 8;
    double f = 0.5;
    unsigned steps = 0;

#define NEXT do { steps++; goto *table[*pc++]; } while (0)
    NEXT;

op_push:
    stack[sp++] = *pc++;
    a1 += sp;
    NEXT;
op_add:
    sp--;
    stack[sp - 1] += stack[sp];
    a2 ^= stack[sp - 1];
    NEXT;
op_mul:
    sp--;
    stack[sp - 1] *= stack[sp];
    a3 += a2 & 7;
    NEXT;
op_dup:
    stack[sp] = stack[sp - 1];
    sp++;
    a4 = a4 * 3 + 1;
    NEXT;
op_swap: {
    int t = stack[sp - 1];
    stack[sp - 1] = stack[sp - 2];
    stack[sp - 2] = t;
    a5 -= t;
    NEXT;
}
op_jnz: {
    int to = *pc++;
    if (stack[sp - 1])
        pc = code + to;
    a6 += to;
    NEXT;
}
op_dec:
    stack[sp - 1]--;
    a7++;
    NEXT;
op_acc:
    acc = acc * 31 + stack[sp - 1];
    a8 ^= acc;
    NEXT;
op_float:
    f = f * 1.5 + (double)(stack[sp - 1] & 3);
    NEXT;
op_call:
    stack[sp - 1] = helper(stack[sp - 1]) & 0xffff;
    NEXT;
op_end:
    return acc + a1 + a2 * 3 + a3 * 5 + a4 * 7 + a5 * 11 + a6 * 13 + a7 * 17 + a8 * 19 + (long)f + steps;
}

/* A target held in a variable, chosen at run time, and reached twice. */
static int __attribute__((noinline)) chosen(int n)
{
    void *next = n & 1 ? &&odd : &&even;
    int r = n;
    goto *next;
even:
    r += 100;
    next = &&done;
    goto *next;
odd:
    r += 200;
    next = n & 2 ? &&even : &&done;
    goto *next;
done:
    return r;
}

/* No fall through to a label: the blocks are reached only by the goto. */
static int __attribute__((noinline)) only_gotos(int i)
{
    static const void *const t[] = { &&a, &&b, &&c };
    int x = i * 10;
    goto *t[i % 3];
a:
    return x + 1;
b:
    return x + 2;
c:
    return x + 3;
}

int main(void)
{
    static const int program[] = {
        OP_PUSH, 3, OP_PUSH, 4, OP_ADD, OP_DUP, OP_MUL, OP_ACC, OP_FLOAT,
        OP_PUSH, 50,
        /* 11: */ OP_DEC, OP_ACC, OP_CALL, OP_ACC, OP_PUSH, 7, OP_MUL, OP_PUSH, 3, OP_SWAP, OP_ADD, OP_FLOAT,
        OP_PUSH, 1000, OP_SWAP, OP_DUP, OP_PUSH, 16, OP_MUL, OP_ADD, OP_ACC, OP_ADD, OP_PUSH, 255, OP_SWAP, OP_DUP, OP_JNZ, 42,
        OP_END, 0, 0,
        /* 42: */ OP_ADD, OP_ACC, OP_PUSH, 1, OP_ADD, OP_ACC, OP_END,
    };
    printf("%ld %d\n", run(program, 9), calls);
    for (int i = 0; i < 8; i++)
        printf("%d %d\n", chosen(i), only_gotos(i));
    return 0;
}
