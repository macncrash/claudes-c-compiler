/* An inline asm memory operand naming a global ("m"(g)) was substituted
 * as the bare symbol (ldr x9, g), which the arm64 assembler rejects; it is
 * now the global's address in a register ([x9]). Found by an inline asm
 * fuzzer. */
#include <stdio.h>

long g0 = 5, g1 = -7;
long arr[4] = { 10, 20, 30, 40 };
static int si = 3;

__attribute__((noinline)) void f(long a)
{
    long t;
    asm("ldr %0, %1" : "=r"(t) : "m"(g1));
    printf("%ld\n", t);
    asm("ldr x9, %1\n\tadd x9, x9, %2\n\tstr x9, %0" : "=m"(g0) : "m"(g0), "r"(a) : "x9");
    asm("ldr x9, %0\n\tadd x9, x9, #1\n\tstr x9, %0" : "+m"(arr[2]) : : "x9");
    asm("ldr w9, %0\n\tlsl w9, w9, #2\n\tstr w9, %0" : "+m"(si) : : "x9");
    printf("%ld %ld %d\n", g0, arr[2], si);
}

int main(void)
{
    f(100);
    f(-3);
    return 0;
}
