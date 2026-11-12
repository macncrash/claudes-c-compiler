/* A 4-byte struct or union returned in x0 can carry junk in the register's
 * upper half (its 8-byte load read past the object). Assigned to a local and
 * reread as its unsigned member, the value was stored with str w0 and the
 * reload ldr w0 deleted as redundant, though ldr w0 also zeroes x0's upper
 * half: (l0 = (l3 = mk(x))).f0 widened to 64 bits kept the junk. Found by a
 * struct copy fuzzer. */
#include <stdio.h>

union U { unsigned f0; } __attribute__((packed));
struct P { unsigned f0; } __attribute__((packed));

union U gu[4] = { { 0xffffffffu }, { 0x12345678u }, { 0xffffffffu }, { 0xffffffffu } };
struct P gp[4] = { { 0xffffffffu }, { 0x9abcdef0u }, { 0xffffffffu }, { 0xffffffffu } };

static inline union U mku(int x)
{
    union U v = gu[1];
    v.f0 += (unsigned)x;
    if (x & 1)
        return gu[1];
    return v;
}

static inline struct P mkp(int x)
{
    if (x & 1)
        return gp[1];
    return gp[x & 2];
}

__attribute__((noinline)) unsigned long long fu(int x)
{
    union U l0, l3;
    return ((l0 = (l3 = mku(x + 1)))).f0;
}

__attribute__((noinline)) unsigned long long fp(int x)
{
    struct P l0, l3;
    return ((l0 = (l3 = mkp(x)))).f0;
}

__attribute__((noinline)) unsigned long long direct(int x)
{
    union U a = mku(x);
    return a.f0;
}

int main(void)
{
    printf("%llx %llx %llx\n", fu(0), fu(1), fu(4));
    printf("%llx %llx %llx\n", fp(1), fp(2), fp(4));
    printf("%llx %llx\n", direct(0), direct(3));
    return 0;
}
