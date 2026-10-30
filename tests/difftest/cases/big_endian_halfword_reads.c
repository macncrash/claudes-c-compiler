/* SQLite's get2byte, `(p[0] << 8) | p[1]`, one halfword load and a byte
 * swap instead of two byte loads and a merge: at even and odd offsets,
 * walking a page's free-block list as freeSpace and pageFindSlot do, and
 * signed as OP_Column reads a 2-byte integer (SQLite's TWO_BYTE_INT,
 * 256*(i8)p[0] | p[1]).
 * Every byte value appears in both positions. */
#include <stdio.h>

#define get2byte(p) (((p)[0] << 8) | (p)[1])

static unsigned __attribute__((noinline)) at(const unsigned char *p, int i) {
    return (unsigned)get2byte(p + i);
}

static long __attribute__((noinline)) signed16(const unsigned char *p) {
    return (long)(256 * (signed char)p[0] | p[1]);
}

static long __attribute__((noinline)) signed24(const unsigned char *p) {
    return (long)(65536 * (signed char)p[0] | (p[1] << 8) | p[2]);
}

/* A free-block list: each block starts with the 2-byte offset of the next
 * and its 2-byte size; walk it summing sizes below `limit`. */
static int __attribute__((noinline)) walk(const unsigned char *page, int first, int limit) {
    int total = 0, steps = 0;
    int pc = first;
    while (pc != 0 && pc < 252 && steps < 64) {
        int next = get2byte(&page[pc]);
        int size = get2byte(&page[pc + 2]);
        if (pc + size <= limit)
            total += size;
        pc = next;
        steps++;
    }
    return total * 100 + steps;
}

int main(void) {
    unsigned char buf[300];
    for (int i = 0; i < 300; i++)
        buf[i] = (unsigned char)(i * 7 + 3);
    unsigned long h = 0;
    for (int i = 0; i < 298; i++)
        h = h * 31 + at(buf, i);
    printf("at %lu %u %u\n", h, at(buf, 0), at(buf, 1));
    unsigned long sh = 0;
    for (int hi = 0; hi < 256; hi += 5)
        for (int lo = 0; lo < 256; lo += 51) {
            unsigned char p[3] = { (unsigned char)hi, (unsigned char)lo, (unsigned char)(hi ^ lo) };
            sh = sh * 3 + (unsigned long)(signed16(p) + signed24(p) % 1000);
        }
    printf("signed %lu %ld %ld\n", sh, signed16((const unsigned char *)"\x80\x01"), signed24((const unsigned char *)"\xff\xfe\x02"));
    unsigned char page[256] = { 0 };
    int blocks[][3] = { { 10, 40, 12 }, { 40, 101, 30 }, { 101, 160, 255 }, { 160, 0, 4 } };
    for (int b = 0; b < 4; b++) {
        page[blocks[b][0]] = (unsigned char)(blocks[b][1] >> 8);
        page[blocks[b][0] + 1] = (unsigned char)blocks[b][1];
        page[blocks[b][0] + 2] = (unsigned char)(blocks[b][2] >> 8);
        page[blocks[b][0] + 3] = (unsigned char)blocks[b][2];
    }
    printf("walk %d %d\n", walk(page, 10, 200), walk(page, 40, 140));
    return 0;
}
