/* A bytecode loop with 140 opcodes, each case jumping back to the byte
 * dispatch: more cases than the 64 dispatch copies the peephole used to
 * make, so every case gets its own copy of the dispatch. Opcodes read
 * operands that follow them, branch, and stop at a halt opcode.
 * Generated. */
#include <stdio.h>

static unsigned long __attribute__((noinline)) run(const unsigned char *code, unsigned long acc, int *steps) {
    const unsigned char *pc = code;
    unsigned long b = 3;
    int n = 0;
    for (;;) {
        n++;
        switch (*pc++) {
        case 0: acc = acc * 3 + b; break;
        case 1: acc ^= (unsigned long)*pc++ << 1; break;
        case 2: b += acc >> 3; break;
        case 3: if (acc & 8) pc += 1; break;
        case 4: acc = (acc << 5) - acc + 29; break;
        case 5: b = b * 3 + *pc++; break;
        case 6: acc += b ^ 43; b = acc >> 3; break;
        case 7: acc = acc * 21 + b; break;
        case 8: acc ^= (unsigned long)*pc++ << 8; break;
        case 9: b += acc >> 10; break;
        case 10: if (acc & 2) pc += 1; break;
        case 11: acc = (acc << 5) - acc + 78; break;
        case 12: b = b * 3 + *pc++; break;
        case 13: acc += b ^ 92; b = acc >> 3; break;
        case 14: acc = acc * 8 + b; break;
        case 15: acc ^= (unsigned long)*pc++ << 15; break;
        case 16: b += acc >> 17; break;
        case 17: if (acc & 256) pc += 1; break;
        case 18: acc = (acc << 5) - acc + 127; break;
        case 19: b = b * 3 + *pc++; break;
        case 20: acc += b ^ 141; b = acc >> 3; break;
        case 21: acc = acc * 26 + b; break;
        case 22: acc ^= (unsigned long)*pc++ << 22; break;
        case 23: b += acc >> 7; break;
        case 24: if (acc & 64) pc += 1; break;
        case 25: acc = (acc << 5) - acc + 176; break;
        case 26: b = b * 3 + *pc++; break;
        case 27: acc += b ^ 190; b = acc >> 3; break;
        case 28: acc = acc * 13 + b; break;
        case 29: acc ^= (unsigned long)*pc++ << 29; break;
        case 30: b += acc >> 14; break;
        case 31: if (acc & 16) pc += 1; break;
        case 32: acc = (acc << 5) - acc + 225; break;
        case 33: b = b * 3 + *pc++; break;
        case 34: acc += b ^ 239; b = acc >> 3; break;
        case 35: acc = acc * 31 + b; break;
        case 36: acc ^= (unsigned long)*pc++ << 36; break;
        case 37: b += acc >> 4; break;
        case 38: if (acc & 4) pc += 1; break;
        case 39: acc = (acc << 5) - acc + 274; break;
        case 40: b = b * 3 + *pc++; break;
        case 41: acc += b ^ 288; b = acc >> 3; break;
        case 42: acc = acc * 18 + b; break;
        case 43: acc ^= (unsigned long)*pc++ << 3; break;
        case 44: b += acc >> 11; break;
        case 45: if (acc & 1) pc += 1; break;
        case 46: acc = (acc << 5) - acc + 323; break;
        case 47: b = b * 3 + *pc++; break;
        case 48: acc += b ^ 337; b = acc >> 3; break;
        case 49: acc = acc * 5 + b; break;
        case 50: acc ^= (unsigned long)*pc++ << 10; break;
        case 51: b += acc >> 1; break;
        case 52: if (acc & 128) pc += 1; break;
        case 53: acc = (acc << 5) - acc + 372; break;
        case 54: b = b * 3 + *pc++; break;
        case 55: acc += b ^ 386; b = acc >> 3; break;
        case 56: acc = acc * 23 + b; break;
        case 57: acc ^= (unsigned long)*pc++ << 17; break;
        case 58: b += acc >> 8; break;
        case 59: if (acc & 32) pc += 1; break;
        case 60: acc = (acc << 5) - acc + 421; break;
        case 61: b = b * 3 + *pc++; break;
        case 62: acc += b ^ 435; b = acc >> 3; break;
        case 63: acc = acc * 10 + b; break;
        case 64: acc ^= (unsigned long)*pc++ << 24; break;
        case 65: b += acc >> 15; break;
        case 66: if (acc & 8) pc += 1; break;
        case 67: acc = (acc << 5) - acc + 470; break;
        case 68: b = b * 3 + *pc++; break;
        case 69: acc += b ^ 484; b = acc >> 3; break;
        case 70: acc = acc * 28 + b; break;
        case 71: acc ^= (unsigned long)*pc++ << 31; break;
        case 72: b += acc >> 5; break;
        case 73: if (acc & 2) pc += 1; break;
        case 74: acc = (acc << 5) - acc + 519; break;
        case 75: b = b * 3 + *pc++; break;
        case 76: acc += b ^ 533; b = acc >> 3; break;
        case 77: acc = acc * 15 + b; break;
        case 78: acc ^= (unsigned long)*pc++ << 38; break;
        case 79: b += acc >> 12; break;
        case 80: if (acc & 256) pc += 1; break;
        case 81: acc = (acc << 5) - acc + 568; break;
        case 82: b = b * 3 + *pc++; break;
        case 83: acc += b ^ 582; b = acc >> 3; break;
        case 84: acc = acc * 2 + b; break;
        case 85: acc ^= (unsigned long)*pc++ << 5; break;
        case 86: b += acc >> 2; break;
        case 87: if (acc & 64) pc += 1; break;
        case 88: acc = (acc << 5) - acc + 617; break;
        case 89: b = b * 3 + *pc++; break;
        case 90: acc += b ^ 631; b = acc >> 3; break;
        case 91: acc = acc * 20 + b; break;
        case 92: acc ^= (unsigned long)*pc++ << 12; break;
        case 93: b += acc >> 9; break;
        case 94: if (acc & 16) pc += 1; break;
        case 95: acc = (acc << 5) - acc + 666; break;
        case 96: b = b * 3 + *pc++; break;
        case 97: acc += b ^ 680; b = acc >> 3; break;
        case 98: acc = acc * 7 + b; break;
        case 99: acc ^= (unsigned long)*pc++ << 19; break;
        case 100: b += acc >> 16; break;
        case 101: if (acc & 4) pc += 1; break;
        case 102: acc = (acc << 5) - acc + 715; break;
        case 103: b = b * 3 + *pc++; break;
        case 104: acc += b ^ 729; b = acc >> 3; break;
        case 105: acc = acc * 25 + b; break;
        case 106: acc ^= (unsigned long)*pc++ << 26; break;
        case 107: b += acc >> 6; break;
        case 108: if (acc & 1) pc += 1; break;
        case 109: acc = (acc << 5) - acc + 764; break;
        case 110: b = b * 3 + *pc++; break;
        case 111: acc += b ^ 778; b = acc >> 3; break;
        case 112: acc = acc * 12 + b; break;
        case 113: acc ^= (unsigned long)*pc++ << 33; break;
        case 114: b += acc >> 13; break;
        case 115: if (acc & 128) pc += 1; break;
        case 116: acc = (acc << 5) - acc + 813; break;
        case 117: b = b * 3 + *pc++; break;
        case 118: acc += b ^ 827; b = acc >> 3; break;
        case 119: acc = acc * 30 + b; break;
        case 120: acc ^= (unsigned long)*pc++ << 0; break;
        case 121: b += acc >> 3; break;
        case 122: if (acc & 32) pc += 1; break;
        case 123: acc = (acc << 5) - acc + 862; break;
        case 124: b = b * 3 + *pc++; break;
        case 125: acc += b ^ 876; b = acc >> 3; break;
        case 126: acc = acc * 17 + b; break;
        case 127: acc ^= (unsigned long)*pc++ << 7; break;
        case 128: b += acc >> 10; break;
        case 129: if (acc & 8) pc += 1; break;
        case 130: acc = (acc << 5) - acc + 911; break;
        case 131: b = b * 3 + *pc++; break;
        case 132: acc += b ^ 925; b = acc >> 3; break;
        case 133: acc = acc * 4 + b; break;
        case 134: acc ^= (unsigned long)*pc++ << 14; break;
        case 135: b += acc >> 17; break;
        case 136: if (acc & 2) pc += 1; break;
        case 137: acc = (acc << 5) - acc + 960; break;
        case 138: b = b * 3 + *pc++; break;
        case 139: acc += b ^ 974; b = acc >> 3; break;
        case 255: *steps = n; return acc ^ b;
        default: acc += 1000003; break;
        }
    }
}

int main(void) {
    unsigned char code[4096];
    unsigned s = 12345;
    for (int i = 0; i < 4095; i++) {
        s = s * 1103515245u + 12345u;
        code[i] = (unsigned char)((s >> 16) % 160);
    }
    code[4095] = 255;
    for (int r = 0; r < 4; r++) {
        int steps = 0;
        unsigned long v = run(code + r * 37, (unsigned long)r * 99991u, &steps);
        printf("run %d: %lu in %d steps\n", r, v, steps);
    }
    return 0;
}
