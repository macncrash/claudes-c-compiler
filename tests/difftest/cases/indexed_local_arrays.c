/* Local arrays indexed by a variable inside loops, whose address the
 * compiler keeps in a value across the loop: a word splitter storing
 * letters and hashing each word around a call, a register-machine
 * interpreter indexing its register file, nested loops over two arrays,
 * an array also passed to a callee that writes it, and a loop entered
 * from two places. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static const char *text = "The quick brown Fox jumps over the lazy dog; THE DOG sleeps, the fox runs away quickly!";

static uint32_t __attribute__((noinline)) seen(const char *w, uint32_t len, uint32_t h) {
    static uint32_t acc;
    acc = acc * 33 + h + len + (uint8_t)w[0];
    return acc;
}

static uint32_t words(void) {
    char w[16];
    uint32_t len = 0, s = 0, n = (uint32_t)strlen(text);
    for (uint32_t i = 0; i <= n; i++) {
        char c = i < n ? text[i] : ' ';
        if (c >= 'A' && c <= 'Z')
            c = (char)(c - 'A' + 'a');
        if (c >= 'a' && c <= 'z') {
            if (len < sizeof w)
                w[len] = c;
            len++;
        } else if (len) {
            uint32_t h = 2166136261u, m = len < sizeof w ? len : sizeof w;
            for (uint32_t j = 0; j < m; j++)
                h = (h ^ (uint8_t)w[j]) * 16777619u;
            s ^= seen(w, m, h);
            len = 0;
        }
    }
    return s;
}

typedef struct { uint8_t op, a, b, c; int32_t imm; } Insn;

static int64_t run(const Insn *code, int steps) {
    int64_t r[8] = { 0 };
    for (int pc = 0, k = 0; k < steps; k++) {
        const Insn *in = &code[pc];
        switch (in->op) {
        case 0: r[in->a] = in->imm; pc++; break;
        case 1: r[in->a] = r[in->b] + r[in->c]; pc++; break;
        case 2: r[in->a] = r[in->b] * r[in->c] + 1; pc++; break;
        case 3: pc = r[in->a] < r[in->b] ? in->imm : pc + 1; break;
        default: pc = 0; break;
        }
    }
    return r[0] * 7 + r[1] * 5 + r[2] * 3 + r[3];
}

static void __attribute__((noinline)) fill(int *a, int n, int k) {
    for (int i = 0; i < n; i++)
        a[i] = (i * k) % 17;
}

static int nested(int n) {
    int a[32], b[32];
    int s = 0;
    for (int round = 0; round < n; round++) {
        fill(a, 32, round + 3);
        for (int i = 0; i < 32; i++)
            b[i] = a[(i * 5 + round) & 31] - a[i];
        for (int i = 1; i < 32; i++)
            s += b[i] * b[i - 1] + a[b[i] & 31];
    }
    return s;
}

static int two_ways_in(int x) {
    int t[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    int i = x & 1 ? 3 : 0;
    if (x > 5)
        goto inside;
    for (; i < 8; i++) {
        t[i] += t[(i + x) & 7];
    inside:
        t[(i * 3) & 7] ^= x + i;
    }
    return t[0] + t[3] * 2 + t[7] * 3;
}

int main(void) {
    Insn prog[] = {
        { 0, 0, 0, 0, 1 }, { 0, 1, 0, 0, 1 }, { 0, 4, 0, 0, 40 }, { 0, 5, 0, 0, 1 },
        { 1, 2, 0, 1, 0 }, { 1, 0, 1, 5, 0 }, { 2, 1, 2, 5, 0 }, { 1, 3, 3, 5, 0 },
        { 3, 3, 4, 4, 0 }, { 9, 0, 0, 0, 0 },
    };
    printf("words %u\n", words());
    printf("run %lld %lld\n", (long long)run(prog, 300), (long long)run(prog, 1000));
    printf("nested %d %d\n", nested(3), nested(10));
    printf("two_ways_in %d %d %d %d\n", two_ways_in(1), two_ways_in(4), two_ways_in(6), two_ways_in(9));
    return 0;
}
