/* A JSON-style switch on a byte, cases spread over 9..239 (about a sixth
 * of the range used: a jump table), and compare chains whose case values
 * sit at the edges of a 12-bit immediate, are negative, or are 64-bit. */
#include <stdio.h>

static int __attribute__((noinline)) classify(unsigned char c) {
    switch (c) {
    case '{': return 1;
    case '[': return 2;
    case '\'': case '"': return 3;
    case 't': return 4;
    case 'f': return 5;
    case 'n': return 6;
    case '+': case '-': case '.': return 7;
    case '0': case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9': return 8;
    case '}': case ']': return 9;
    case ',': case ':': return 10;
    case ' ': case '\t': case '\n': case '\r': case 0x0b: case 0x0c: return 11;
    case '/': return 12;
    case 0xc2: case 0xe1: case 0xe2: case 0xe3: case 0xef: return 13;
    default: return 0;
    }
}

static int __attribute__((noinline)) edges(int v) {
    switch (v) {
    case 0: return 1;
    case 4095: return 2;
    case 4096: return 3;
    case -1: return 4;
    case -4095: return 5;
    case -4096: return 6;
    case 100000: return 7;
    default: return 0;
    }
}

static int __attribute__((noinline)) wide(long long v) {
    switch (v) {
    case 7: return 1;
    case -7: return 2;
    case 0x100000000ll: return 3;
    case -0x100000001ll: return 4;
    case 4095: return 5;
    default: return 0;
    }
}

int main(void) {
    unsigned h = 0;
    for (int c = 0; c < 256; c++)
        h = h * 17 + (unsigned)classify((unsigned char)c);
    printf("classify %u\n", h);
    int ev[] = { 0, 4095, 4096, 4097, -1, -4095, -4096, -4097, 100000, 1, 4094 };
    for (unsigned i = 0; i < sizeof ev / sizeof ev[0]; i++)
        printf("%d ", edges(ev[i]));
    printf("\n");
    long long wv[] = { 7, -7, 0x100000000ll, -0x100000001ll, 4095, 0x100000007ll, -0x100000007ll };
    for (unsigned i = 0; i < sizeof wv / sizeof wv[0]; i++)
        printf("%d ", wide(wv[i]));
    printf("\n");
    return 0;
}
