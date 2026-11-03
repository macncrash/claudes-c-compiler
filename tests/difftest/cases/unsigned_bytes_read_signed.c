/* Bytes and halfwords of unsigned buffers read through a signed cast and
 * widened (SQLite's serial_type = (signed char)aKey1[1], and OP_Column's
 * 2-byte integer, 256*(i8)p[0] | p[1]), next to reads that keep the
 * unsigned value or byte-swap it. */
#include <stdio.h>

typedef unsigned char u8;
typedef unsigned short u16;

static int __attribute__((noinline)) serial_type(const u8 *a)
{
    int t = (signed char)a[1];
    if (t < 0)
        return -t * 3;
    return t < 12 ? t : t * 2;
}

static long long __attribute__((noinline)) two_byte_int(const u8 *p)
{
    return 256 * (signed char)p[0] | p[1];
}

static long long __attribute__((noinline)) two_byte_int_low_first(const u8 *p)
{
    int lo = p[1];
    return lo | 256 * (signed char)p[0];
}

static long long __attribute__((noinline)) three_byte_int(const u8 *p)
{
    return 65536 * (signed char)p[0] | p[1] << 8 | p[2];
}

static int __attribute__((noinline)) signed_half(const u16 *h, int i)
{
    return (short)h[i] * 2 + (short)h[i + 1];
}

static unsigned __attribute__((noinline)) swapped(const u16 *h, int i)
{
    return (u16)__builtin_bswap16((u16)(short)h[i]);
}

static int __attribute__((noinline)) both(const u8 *p)
{
    signed char s = (signed char)p[0];
    unsigned char u = p[0];
    return s * 1000 + u;
}

int main(void)
{
    u8 buf[64];
    u16 half[33];
    unsigned x = 2463534242u;
    for (int i = 0; i < 64; i++) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        buf[i] = (u8)x;
    }
    for (int i = 0; i < 33; i++)
        half[i] = (u16)(buf[i] << 8 | buf[63 - i]);
    buf[3] = 0x80;
    buf[4] = 0x7f;
    buf[5] = 0xff;
    long long s = 0;
    for (int i = 0; i < 61; i++) {
        s = s * 7 + serial_type(buf + i);
        s = s * 7 + two_byte_int(buf + i);
        s = s * 7 + two_byte_int_low_first(buf + i);
        s = s * 7 + three_byte_int(buf + i);
        s = s * 7 + both(buf + i);
        s &= 0xffffffffffffll;
    }
    for (int i = 0; i < 32; i++)
        s = (s * 5 + signed_half(half, i) + swapped(half, i)) & 0xffffffffffffll;
    printf("%lld\n", s);
    printf("%d %d %d\n", serial_type(buf + 2), serial_type(buf + 3), serial_type(buf + 4));
    printf("%lld %lld %lld\n", two_byte_int(buf + 3), two_byte_int(buf + 4), three_byte_int(buf + 3));
    return 0;
}
