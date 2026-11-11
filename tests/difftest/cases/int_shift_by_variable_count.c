/* A shift of an int or unsigned by a variable count is done in 32 bits
 * (lsl/lsr/asr w): the count is below 32, or C leaves the shift
 * undefined. The result must still be extended correctly where it is
 * used as a 64-bit value, and narrow operands must be promoted first. */
#include <stdio.h>
#include <stdint.h>

__attribute__((noinline)) int sl(int x, int n) { return x << n; }
__attribute__((noinline)) int sr(int x, int n) { return x >> n; }
__attribute__((noinline)) unsigned ul(unsigned x, int n) { return x << n; }
__attribute__((noinline)) unsigned ur(unsigned x, unsigned long n) { return x >> n; }
__attribute__((noinline)) long slw(int x, int n) { return x << n; }
__attribute__((noinline)) long srw(int x, long n) { return x >> n; }
__attribute__((noinline)) unsigned long ulw(unsigned x, int n) { return x << n; }
__attribute__((noinline)) unsigned long urw(unsigned x, int n) { return x >> n; }
__attribute__((noinline)) int cs(signed char c, int n) { return c >> n; }
__attribute__((noinline)) int us(unsigned short s, int n) { return s << n; }
__attribute__((noinline)) long mix(int x, unsigned y, int n) { return (long)(x >> n) + (long)(y >> n) + (x << n); }
__attribute__((noinline)) uint64_t chain(uint32_t x, int a, int b) { return (uint64_t)((x << a) >> b) * 3; }
__attribute__((noinline)) int64_t ldiv2(int x, int n) { return (int64_t)(x >> n) / 2; }

int main(void)
{
    static const int xs[] = { 0, 1, -1, 7, -7, 0x40000000, -0x40000000, 0x7fffffff, (int)0x80000000, 0x12345678 };
    uint64_t h = 0;
    for (int i = 0; i < 10; i++) {
        int x = xs[i];
        unsigned u = (unsigned)x;
        for (int n = 0; n < 32; n++) {
            uint64_t v[] = {
                (uint64_t)(unsigned)sl(x & 0xffff, n % 16), (uint64_t)sr(x, n), ul(u, n), ur(u, (unsigned long)n),
                (uint64_t)slw(x & 0xffff, n % 16), (uint64_t)srw(x, n), ulw(u, n), urw(u, n),
                (uint64_t)cs((signed char)x, n), (uint64_t)us((unsigned short)x, n % 16),
                (uint64_t)mix(x & 0x7fff, u, n % 16), chain(u, n, 31 - n), (uint64_t)ldiv2(x, n),
            };
            for (unsigned k = 0; k < sizeof v / sizeof v[0]; k++)
                h = (h ^ v[k]) * 0x100000001b3ULL;
        }
        printf("%d %llx\n", x, (unsigned long long)h);
    }
    printf("%lx %lx %ld %lu\n", (unsigned long)srw(-8, 1), urw(0x80000000u, 31), slw(0x4000, 16), ulw(3, 31));
    return 0;
}
