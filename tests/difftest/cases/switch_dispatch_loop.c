/* Loops around a switch, whose dispatch is copied into the cases that jump
 * back to it: bytecode interpreters (cases that branch, fall out of range to
 * the default, halt), a state machine with a nested switch, and a dispatch
 * that makes a call (not copied). Along the way: byte and halfword loads
 * used as indexes (no re-masking), a stack array indexed by variables (its
 * address computed once per block), and int counters whose upper halves
 * nothing reads. */
#include <stdio.h>
#include <string.h>

enum { LI, ADD, SUB, MUL, REM, BLT, BEQZ, BNEZ, JMP, OUT, HALT, BAD = 200 };

typedef struct {
    unsigned char op, a, b, c;
    int imm;
} Insn;

static long outputs[64];
static int nout;

__attribute__((noinline)) static long run(const Insn *code, long *steps_out) {
    long r[8] = { 0 };
    long steps = 0;
    for (int pc = 0;;) {
        const Insn *in = &code[pc++];
        steps++;
        switch (in->op) {
        case LI: r[in->a] = in->imm; break;
        case ADD: r[in->a] = r[in->b] + r[in->c]; break;
        case SUB: r[in->a] = r[in->b] - r[in->c]; break;
        case MUL: r[in->a] = r[in->b] * r[in->c]; break;
        case REM: r[in->a] = r[in->c] ? r[in->b] % r[in->c] : -1; break;
        case BLT: if (r[in->a] < r[in->b]) pc = in->imm; break;
        case BEQZ: if (r[in->a] == 0) pc = in->imm; break;
        case BNEZ: if (r[in->a] != 0) pc = in->imm; break;
        case JMP: pc = in->imm; break;
        case OUT: if (nout < 64) outputs[nout++] = r[in->a]; break;
        case HALT: *steps_out = steps; return r[0];
        default: r[7]++; break; /* unknown opcodes count themselves */
        }
    }
}

/* Sum of 1..n, the primes below n, and a countdown with unknown opcodes. */
static const Insn sum_prog[] = {
    { LI, 0, 0, 0, 0 }, { LI, 1, 0, 0, 1 }, { LI, 2, 0, 0, 0 }, { LI, 3, 0, 0, 1 },
    /* 4 */ { ADD, 0, 0, 1, 0 }, { ADD, 1, 1, 3, 0 }, { BLT, 1, 2, 0, 4 }, { OUT, 0, 0, 0, 0 }, { HALT, 0, 0, 0, 0 },
};

static const Insn prime_prog[] = {
    /* 0 */ { LI, 1, 0, 0, 2 }, { LI, 2, 0, 0, 0 }, { LI, 0, 0, 0, 0 }, { LI, 6, 0, 0, 1 },
    /* 4 */ { BLT, 1, 2, 0, 6 }, { JMP, 0, 0, 0, 17 },
    /* 6 */ { LI, 4, 0, 0, 2 },
    /* 7 */ { MUL, 5, 4, 4, 0 }, { BLT, 1, 5, 0, 13 },
    /* 9 */ { REM, 5, 1, 4, 0 }, { BEQZ, 5, 0, 0, 15 }, { ADD, 4, 4, 6, 0 }, { JMP, 0, 0, 0, 7 },
    /* 13 */ { ADD, 0, 0, 6, 0 }, { OUT, 1, 0, 0, 0 },
    /* 15 */ { ADD, 1, 1, 6, 0 }, { JMP, 0, 0, 0, 4 },
    /* 17 */ { HALT, 0, 0, 0, 0 },
};

static const Insn odd_prog[] = {
    { LI, 0, 0, 0, 5 }, { LI, 6, 0, 0, 1 },
    /* 2 */ { BAD, 0, 0, 0, 0 }, { 99, 0, 0, 0, 0 }, { SUB, 0, 0, 6, 0 }, { BNEZ, 0, 0, 0, 2 },
    { ADD, 0, 7, 7, 0 }, { REM, 1, 0, 2, 0 }, { OUT, 1, 0, 0, 0 }, { HALT, 0, 0, 0, 0 },
};

/* A tokenizer state machine; the inner switch picks a character class. */
__attribute__((noinline)) static void tokens(const char *s, int *counts) {
    enum { START, WORD, NUM, PUNCT } state = START;
    for (const unsigned char *p = (const unsigned char *)s;; p++) {
        int cls;
        switch (*p) {
        case 'a' ... 'z': case 'A' ... 'Z': case '_': cls = 0; break;
        case '0' ... '9': cls = 1; break;
        case ' ': case '\t': case '\n': cls = 2; break;
        case 0: cls = 4; break;
        default: cls = 3; break;
        }
        switch (state) {
        case START: break;
        case WORD: if (cls != 0 && cls != 1) counts[0]++; break;
        case NUM: if (cls != 1) counts[1]++; break;
        case PUNCT: counts[2]++; break;
        }
        if (cls == 4)
            return;
        state = cls == 0 ? WORD : cls == 1 ? (state == WORD ? WORD : NUM) : cls == 3 ? PUNCT : START;
    }
}

/* The dispatch makes a call: not copied. */
static int hits;
__attribute__((noinline)) static int fetch(const unsigned short *ops, int i) { hits++; return ops[i]; }

__attribute__((noinline)) static unsigned calls(const unsigned short *ops, int n) {
    unsigned acc = 1;
    for (int i = 0; i < n; i++) {
        switch (fetch(ops, i) & 7) {
        case 0: acc = acc * 3 + 1; break;
        case 1: acc ^= acc >> 3; break;
        case 2: acc += (unsigned)i; break;
        case 3: acc = acc * 7; break;
        case 4: acc -= 11; break;
        case 5: acc = (acc << 2) | (acc >> 30); break;
        default: acc += 1; break;
        }
    }
    return acc;
}

/* Byte and halfword loads used as indexes into a stack table. */
__attribute__((noinline)) static unsigned long histo(const unsigned char *b, const unsigned short *h, int n) {
    unsigned long t[256];
    memset(t, 0, sizeof t);
    for (int i = 0; i < n; i++) {
        t[b[i]] += 1;
        t[h[i] & 0xff] += 2;
        t[(h[i] >> 8) & 0xff] += 3;
    }
    unsigned long s = 0;
    for (int i = 0; i < 256; i++)
        s = s * 31 + t[i];
    return s;
}

int main(void) {
    Insn prog[sizeof sum_prog / sizeof sum_prog[0]];
    memcpy(prog, sum_prog, sizeof prog);
    long steps;
    for (int n = 0; n <= 100; n += 25) {
        prog[2].imm = n;
        long r = run(prog, &steps);
        printf("sum %d: %ld in %ld steps\n", n, r, steps);
    }
    Insn primes[sizeof prime_prog / sizeof prime_prog[0]];
    memcpy(primes, prime_prog, sizeof primes);
    primes[1].imm = 60;
    nout = 0;
    long np = run(primes, &steps);
    printf("primes below 60: %ld in %ld steps:", np, steps);
    for (int i = 0; i < nout; i++)
        printf(" %ld", outputs[i]);
    printf("\n");
    nout = 0;
    long odd = run(odd_prog, &steps);
    printf("odd: %ld in %ld steps, out %ld\n", odd, steps, nout ? outputs[0] : -99);

    int counts[3] = { 0, 0, 0 };
    tokens("int x9 = 42 + y_1 * (3 - z);\n\tfoo(bar, 7);", counts);
    printf("tokens: %d words, %d numbers, %d punct\n", counts[0], counts[1], counts[2]);

    unsigned short ops[100];
    unsigned char bytes[100];
    for (int i = 0; i < 100; i++) {
        ops[i] = (unsigned short)(i * 2654435761u >> 7);
        bytes[i] = (unsigned char)(i * 37 + 11);
    }
    printf("calls: %u after %d fetches\n", calls(ops, 100), hits);
    printf("histo: %lu\n", histo(bytes, ops, 100));
    return 0;
}
