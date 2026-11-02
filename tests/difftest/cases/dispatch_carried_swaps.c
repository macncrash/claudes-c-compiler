/* An interpreter loop carrying several values around its dispatch, with
 * more live than registers: most handlers leave them alone, some swap or
 * rotate them (phis that need temporaries), some change one, and a few
 * call out. */
#include <stdio.h>

static unsigned long __attribute__((noinline)) mix(unsigned long a, unsigned long b)
{
    return a * 0x9e3779b97f4a7c15UL ^ (b >> 3);
}

static unsigned long __attribute__((noinline)) run(const unsigned char *code, int n, unsigned long *mem)
{
    unsigned long a = 1, b = 2, c = 3, d = 4, e = 5, f = 6, g = 7, h = 8;
    unsigned long acc = 0;
    const unsigned char *pc = code, *end = code + n;
    while (pc < end) {
        unsigned op = *pc++;
        unsigned long t;
        switch (op % 12) {
        case 0: t = a; a = b; b = t; break;
        case 1: t = a; a = b; b = c; c = t; break;
        case 2: t = e; e = f; f = g; g = h; h = t; break;
        case 3: acc += a + b; break;
        case 4: acc ^= c * d; break;
        case 5: mem[op & 15] = e + acc; break;
        case 6: acc += mem[(op >> 4) & 15]; break;
        case 7: d = mix(d, acc); break;
        case 8: t = c; c = d; d = t; acc++; break;
        case 9: acc = mix(acc, g + h); break;
        case 10: h += op; break;
        default: acc -= f; break;
        }
    }
    return acc + a + 3 * b + 5 * c + 7 * d + 11 * e + 13 * f + 17 * g + 19 * h;
}

int main(void)
{
    unsigned char code[4096];
    unsigned x = 12345;
    for (int i = 0; i < 4096; i++) {
        x = x * 1103515245u + 12345u;
        code[i] = (unsigned char)(x >> 16);
    }
    unsigned long mem[16] = {0};
    for (int r = 0; r < 3; r++)
        printf("%lu\n", run(code, 4096 - r * 1000, mem));
    unsigned long s = 0;
    for (int i = 0; i < 16; i++)
        s = s * 31 + mem[i];
    printf("%lu\n", s);
    return 0;
}
