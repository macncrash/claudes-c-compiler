/* Struct copies of every size from 1 to 300 bytes (inline memcpy codegen:
 * 16-byte chunks, unrolled or looped, plus 8/4/2/1-byte tails), at
 * unaligned addresses, self-assignment, and copies through pointers. */
#include <stdio.h>
#include <string.h>

#define DEFINE_SIZE(n) struct s##n { unsigned char b[n]; };
#define SIZES(X) \
    X(1) X(2) X(3) X(4) X(5) X(6) X(7) X(8) X(9) X(12) X(15) X(16) X(17) \
    X(23) X(24) X(31) X(32) X(33) X(47) X(63) X(64) X(65) X(100) X(127) \
    X(128) X(129) X(200) X(255) X(256) X(257) X(271) X(272) X(273) X(300)
SIZES(DEFINE_SIZE)

static unsigned checksum(const unsigned char *p, size_t n)
{
    unsigned h = 2166136261u;
    for (size_t i = 0; i < n; i++)
        h = (h ^ p[i]) * 16777619u;
    return h;
}

static void fill(unsigned char *p, size_t n, unsigned seed)
{
    for (size_t i = 0; i < n; i++)
        p[i] = (unsigned char)(seed * 31 + i * 7 + (i >> 3));
}

/* Copy into a guarded buffer at every misalignment and check the guards. */
#define TEST_SIZE(n)                                                         \
    {                                                                        \
        static unsigned char src_buf[n + 32], dst_buf[n + 32];               \
        int ok = 1;                                                          \
        for (int off = 0; off < 8; off++) {                                  \
            memset(dst_buf, 0xAA, sizeof dst_buf);                           \
            fill(src_buf, sizeof src_buf, n + off);                          \
            struct s##n *s = (struct s##n *)(src_buf + off);                 \
            struct s##n *d = (struct s##n *)(dst_buf + 16 - off);            \
            *d = *s;                                                         \
            ok &= memcmp(d, s, n) == 0;                                      \
            for (int g = 0; g < 16 - off; g++)                               \
                ok &= dst_buf[g] == 0xAA;                                    \
            for (size_t g = 16 - off + n; g < sizeof dst_buf; g++)           \
                ok &= dst_buf[g] == 0xAA;                                    \
        }                                                                    \
        struct s##n local;                                                   \
        fill(local.b, n, n);                                                 \
        local = *(volatile struct s##n *)&local;                             \
        printf("size %3d: %s %08x\n", n, ok ? "ok" : "BAD", checksum(local.b, n)); \
    }

struct mixed {
    char tag;
    double d;
    short s[3];
    int arr[17];
    char name[11];
    long long big;
};

static struct mixed make_mixed(int k)
{
    struct mixed m;
    memset(&m, 0, sizeof m);
    m.tag = (char)('A' + k);
    m.d = k * 1.25;
    for (int i = 0; i < 3; i++) m.s[i] = (short)(k * 100 + i);
    for (int i = 0; i < 17; i++) m.arr[i] = k * i - 5;
    snprintf(m.name, sizeof m.name, "item%d", k);
    m.big = (long long)k << 40;
    return m;
}

int main(void)
{
    SIZES(TEST_SIZE)

    struct mixed a[5];
    for (int i = 0; i < 5; i++)
        a[i] = make_mixed(i);
    struct mixed t = a[0];
    for (int i = 0; i < 4; i++)
        a[i] = a[i + 1];
    a[4] = t;
    for (int i = 0; i < 5; i++)
        printf("%c %.2f %d %d %s %lld\n", a[i].tag, a[i].d, a[i].s[2], a[i].arr[16], a[i].name, a[i].big);
    printf("sizeof mixed %zu\n", sizeof(struct mixed));
    return 0;
}
