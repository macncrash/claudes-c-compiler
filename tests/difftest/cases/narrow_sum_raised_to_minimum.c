/* 16- and 8-bit sums compared below a power of two and raised to a
 * minimum, in the shape of SQLite's btreeParseCellPtr:
 * pInfo->nSize = (u16)nPayload + (u16)(pIter - pCell);
 * if( pInfo->nSize<4 ) pInfo->nSize = 4;
 * where the sum's high bits beyond its width must not count. */
#include <stdio.h>

typedef unsigned char u8;
typedef unsigned short u16;

struct info {
    u16 local;
    u16 size;
    u8 small;
};

static void __attribute__((noinline)) parse(struct info *p, unsigned payload, const u8 *iter, const u8 *cell)
{
    p->size = (u16)payload + (u16)(iter - cell);
    if (p->size < 4)
        p->size = 4;
    p->local = (u16)payload;
}

static void __attribute__((noinline)) parse_byte(struct info *p, unsigned v, unsigned w)
{
    p->small = (u8)(v + w);
    if (p->small < 16)
        p->small = 16;
}

static int __attribute__((noinline)) at_least(unsigned v, unsigned w)
{
    u16 s = (u16)(v + w);
    if (s >= 64)
        return s;
    return -1;
}

int main(void)
{
    static u8 buf[64];
    struct info in;
    unsigned long h = 0;
    unsigned payloads[] = {0, 1, 2, 3, 4, 65532, 65533, 65534, 65535, 65536, 65537, 131071, 100000, 0xfffffffeu};
    for (unsigned i = 0; i < sizeof payloads / sizeof payloads[0]; i++) {
        for (int d = 0; d < 6; d++) {
            parse(&in, payloads[i], buf + d, buf);
            h = h * 31 + in.size * 7 + in.local;
            parse_byte(&in, payloads[i], (unsigned)d * 50);
            h = h * 31 + in.small;
            h = h * 31 + (unsigned)at_least(payloads[i], (unsigned)d * 13);
        }
    }
    printf("%lu\n", h);
    return 0;
}
