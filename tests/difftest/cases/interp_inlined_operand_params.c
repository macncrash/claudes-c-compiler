/* An interpreter whose cases call small inline helpers (SQLite's
 * out2Prerelease(p, pOp)): each inlined call leaves parameter locals used
 * only inside its case. Enough of them to put the dispatch loop's phi
 * placement past mem2reg's cost limit unless phis go only where a local is
 * live. Plus locals read before written on some paths, written on one arm
 * and read after a merge, and carried around the loop. */
#include <stdio.h>

typedef struct { unsigned op, a, b, c; } Op;
typedef struct { long r[16]; unsigned long acc; } Vm;

static inline long *reg_out(Vm *vm, const Op *op) { return &vm->r[op->c & 15]; }
static inline long reg_in(Vm *vm, const Op *op, int which) { return vm->r[(which ? op->b : op->a) & 15]; }
static inline void mix(Vm *vm, unsigned long v, unsigned k) { vm->acc = (vm->acc ^ v) * 1099511628211ul + k; }

#define CASE(n)                                                            \
    case n: {                                                              \
        long x = reg_in(vm, op, 0), y = reg_in(vm, op, 1);                 \
        *reg_out(vm, op) = (x * (n + 3) + (y ^ n)) & 0xffffffl;            \
        mix(vm, (unsigned long)*reg_out(vm, op), n);                       \
        break;                                                             \
    }
#define CASE8(n) CASE(n) CASE(n + 1) CASE(n + 2) CASE(n + 3) CASE(n + 4) CASE(n + 5) CASE(n + 6) CASE(n + 7)

static unsigned long __attribute__((noinline)) run(Vm *vm, const Op *prog, int n) {
    int carried;          /* written in some cases, read in others */
    int last = -1;        /* carried around the loop */
    for (int pc = 0; pc < n; pc++) {
        const Op *op = &prog[pc];
        int t;
        switch (op->op % 136) {
            CASE8(0) CASE8(8) CASE8(16) CASE8(24) CASE8(32) CASE8(40) CASE8(48) CASE8(56)
            CASE8(64) CASE8(72) CASE8(80) CASE8(88) CASE8(96) CASE8(104) CASE8(112) CASE8(120)
        case 128:
            carried = (int)op->a;
            break;
        case 129:
            if (last >= 0)
                mix(vm, (unsigned long)(carried + last), 129);
            break;
        case 130:
            if (op->a & 1)
                t = (int)op->b;
            else
                t = -(int)op->c;
            mix(vm, (unsigned long)(long)t, 130);
            break;
        default:
            last = (int)(op->op & 255);
            break;
        }
    }
    return vm->acc ^ (unsigned long)(long)last;
}

int main(void) {
    static Op prog[4000];
    unsigned s = 12345;
    for (int i = 0; i < 4000; i++) {
        s = s * 1103515245u + 12345u;
        prog[i].op = s >> 8;
        prog[i].a = s >> 3;
        prog[i].b = s >> 11;
        prog[i].c = s >> 17;
    }
    prog[0].op = 128; /* carried is set before any 129 reads it */
    Vm vm = { { 0 }, 14695981039346656037ul };
    for (int i = 0; i < 16; i++)
        vm.r[i] = i * 7 + 1;
    unsigned long h = run(&vm, prog, 4000);
    printf("acc %lx\n", h);
    long sum = 0;
    for (int i = 0; i < 16; i++)
        sum += vm.r[i];
    printf("regs %ld\n", sum);
    return 0;
}
