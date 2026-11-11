/* Case labels convert to the promoted type of the controlling expression
 * (C11 6.8.4.2p5). For a constant controlling expression the lowering took
 * its IR type (I64 for an integer constant), not int, so in switch (0),
 * case 0x8000000000000000ULL was not case 0. */
#include <stdio.h>

static int tr;
__attribute__((noinline)) static void T(int v) { tr = tr * 31 + v + 1; }

__attribute__((noinline)) int s1(int x)
{
    switch (x) {
    case 0x8000000000000000ULL:
        return 1;
    case 0x100000005ULL:
        return 2;
    }
    return 0;
}

__attribute__((noinline)) int s2(void)
{
    switch (0) {
    case 0x8000000000000000ULL:
        return 1;
    }
    return 0;
}

__attribute__((noinline)) int s3(void)
{
    switch (5) {
    case 0x100000005ULL:
        return 2;
    default:
        return 3;
    }
}

__attribute__((noinline)) int s4(void)
{
    switch ((short)-1) {
    case 0xffffffff:
        return 4;
    }
    return 0;
}

__attribute__((noinline)) int s5(void)
{
    switch ('a') {
    case 0x161:
        return 5;
    }
    return 0;
}

__attribute__((noinline)) int s6(void)
{
    switch (0u) {
    case 0x100000000ULL:
        return 6;
    }
    return 0;
}

__attribute__((noinline)) int s7(void)
{
    int r = 0;
    for (int i = 0; i < 2; i++) {
        switch (0) {
        case 0x8000000000000000ULL:
            T(i);
            r++;
        }
    }
    return r;
}

int main(void)
{
    printf("%d %d %d %d %d %d %d %d %d | %d\n", s1(0), s1(5), s1(7), s2(), s3(), s4(), s5(), s6(), s7(), tr);
    return 0;
}
