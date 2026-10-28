/* A step counter carried around an interpreter's switch dispatch (live in
 * every handler, used only at the loop's head and shared latch), handlers
 * that call out, and a hash-chain search whose countdown is handed from its
 * loop phi to its decrement and back. */
#include <stdio.h>
#include <string.h>

enum { OP_PUSH, OP_ADD, OP_SUB, OP_MUL, OP_DUP, OP_SWAP, OP_JNZ, OP_DEC,
       OP_CALL, OP_STORE, OP_LOAD, OP_HALT };

struct insn { unsigned char op; int arg; };

static long __attribute__((noinline)) helper(long x) {
    return (x * 7 + 3) % 1009;
}

static long __attribute__((noinline)) run(const struct insn *prog, long *mem, unsigned long *steps_out) {
    long stack[64];
    int sp = 0;
    unsigned long steps = 0;
    const struct insn *pc = prog;
    for (;;) {
        steps++;
        switch (pc->op) {
        case OP_PUSH: stack[sp++] = pc->arg; break;
        case OP_ADD: sp--; stack[sp - 1] += stack[sp]; break;
        case OP_SUB: sp--; stack[sp - 1] -= stack[sp]; break;
        case OP_MUL: sp--; stack[sp - 1] = (stack[sp - 1] * stack[sp]) % 100003; break;
        case OP_DUP: stack[sp] = stack[sp - 1]; sp++; break;
        case OP_SWAP: { long t = stack[sp - 1]; stack[sp - 1] = stack[sp - 2]; stack[sp - 2] = t; break; }
        case OP_JNZ:
            if (stack[--sp] != 0) {
                pc = prog + pc->arg;
                continue;
            }
            break;
        case OP_DEC: stack[sp - 1]--; break;
        case OP_CALL: stack[sp - 1] = helper(stack[sp - 1]); break;
        case OP_STORE: mem[pc->arg] = stack[--sp]; break;
        case OP_LOAD: stack[sp++] = mem[pc->arg]; break;
        case OP_HALT:
            *steps_out = steps;
            return sp ? stack[sp - 1] : -1;
        default:
            *steps_out = steps;
            return -2;
        }
        pc++;
    }
}

/* Longest run of equal bytes between pos and earlier positions on its
 * hash chain, giving up after chain_length links. */
static int __attribute__((noinline)) chain_search(const unsigned char *buf, int len, const unsigned short *prev,
                                                  int pos, unsigned chain_length, int *best_at) {
    int best = 0;
    unsigned cur = prev[pos];
    *best_at = -1;
    do {
        if (cur >= (unsigned)pos)
            break;
        if (buf[cur + best] == buf[pos + best] && buf[cur] == buf[pos]) {
            int n = 0;
            while (pos + n < len && buf[cur + n] == buf[pos + n])
                n++;
            if (n > best) {
                best = n;
                *best_at = (int)cur;
            }
        }
        cur = prev[cur];
    } while (cur != 0 && --chain_length != 0);
    return best;
}

int main(void) {
    /* mem[0] = acc; loop counter on the stack. */
    struct insn prog[] = {
        { OP_PUSH, 0 }, { OP_STORE, 0 },
        { OP_PUSH, 300 },
        /* 3: */ { OP_DUP, 0 }, { OP_LOAD, 0 }, { OP_ADD, 0 }, { OP_CALL, 0 },
        { OP_LOAD, 0 }, { OP_PUSH, 3 }, { OP_MUL, 0 }, { OP_ADD, 0 }, { OP_STORE, 0 },
        { OP_DEC, 0 }, { OP_DUP, 0 }, { OP_JNZ, 3 },
        { OP_LOAD, 0 }, { OP_SWAP, 0 }, { OP_SUB, 0 }, { OP_HALT, 0 },
    };
    long mem[4] = { 0 };
    unsigned long steps = 0;
    long r = run(prog, mem, &steps);
    printf("run %ld steps %lu mem %ld\n", r, steps, mem[0]);

    struct insn bad[] = { { OP_PUSH, 1 }, { 200, 0 } };
    r = run(bad, mem, &steps);
    printf("bad %ld steps %lu\n", r, steps);

    unsigned char buf[2048];
    unsigned short prev[2048], head[256];
    unsigned x = 12345;
    for (int i = 0; i < 2048; i++) {
        x = x * 1103515245u + 12345u;
        buf[i] = (unsigned char)("abcab"[(x >> 16) % 5]);
    }
    memset(head, 0, sizeof head);
    for (int i = 0; i < 2048; i++) {
        unsigned h = (buf[i] * 31u + (i + 1 < 2048 ? buf[i + 1] : 0)) & 255u;
        prev[i] = head[h];
        head[h] = (unsigned short)i;
    }
    long total = 0;
    for (int pos = 100; pos < 2000; pos += 37) {
        int at;
        int best = chain_search(buf, 2048, prev, pos, (unsigned)(pos % 9 + 1), &at);
        total += best * 3 + at;
    }
    printf("chain %ld\n", total);
    return 0;
}
