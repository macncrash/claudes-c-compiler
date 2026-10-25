/* Locals read and written whole with memcpy (an unaligned word, a double's
 * bits, a value stored out again), and a deflate-style hash insertion that
 * reads one through memcpy and indexes two halfword tables with it. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

struct st {
    uint16_t *prev;
    uint16_t *head;
    unsigned ins_h;
    unsigned w_mask;
    unsigned hash_mask;
    const unsigned char *window;
};

static inline uint16_t insert(struct st *s, uint16_t str) {
    uint32_t value;
    memcpy(&value, &s->window[str], sizeof(value));
    s->ins_h = ((value * 66521 + 66521) >> 16) & s->hash_mask;
    uint16_t ret = s->prev[str & s->w_mask] = s->head[s->ins_h];
    s->head[s->ins_h] = str;
    return ret;
}

__attribute__((noinline)) static unsigned run(struct st *s, unsigned n) {
    unsigned sum = 0;
    for (unsigned i = 0; i < n; i++) {
        uint16_t h = insert(s, (uint16_t)i);
        if (h != 0 && (i & 3) == 0) {
            /* A run of insertions, like a match's. */
            for (unsigned k = 1; k < 4 && i + k < n; k++)
                insert(s, (uint16_t)(i + k));
            i += 3;
        }
        sum = sum * 31 + h;
    }
    return sum;
}

__attribute__((noinline)) static uint64_t read64(const unsigned char *p) {
    uint64_t v;
    memcpy(&v, p, 8);
    return v;
}

__attribute__((noinline)) static double as_double(const unsigned char *p) {
    double d;
    memcpy(&d, p, sizeof d);
    return d;
}

__attribute__((noinline)) static void put16(unsigned char *p, unsigned x) {
    uint16_t v = (uint16_t)(x * 3);
    memcpy(p, &v, 2);
}

__attribute__((noinline)) static float swap_float(float f) {
    uint32_t u;
    memcpy(&u, &f, 4);
    u ^= 0x80000000u;
    memcpy(&f, &u, 4);
    return f;
}

int main(void) {
    static unsigned char win[300];
    static uint16_t prev[64], head[256];
    for (int i = 0; i < 300; i++)
        win[i] = (unsigned char)(i * 37 + 11 + (i >> 3));
    struct st s = {prev, head, 0, 63, 255, win};
    unsigned a = run(&s, 200);
    unsigned b = 0;
    for (int i = 0; i < 64; i++)
        b = b * 7 + prev[i];
    for (int i = 0; i < 256; i++)
        b = b * 5 + head[i];
    printf("insert %u %u %u\n", a, b, s.ins_h);

    unsigned char bytes[16];
    double one = 1.5;
    memcpy(bytes + 3, &one, 8);
    printf("read64 %llx %g\n", (unsigned long long)read64(win + 5), as_double(bytes + 3));
    for (unsigned x = 0; x < 4; x++)
        put16(bytes + 1 + x * 3, x + 1000);
    printf("put16 %u %u %u\n", bytes[1] | bytes[2] << 8, bytes[4] | bytes[5] << 8, bytes[10] | bytes[11] << 8);
    printf("float %g %g\n", swap_float(2.25f), swap_float(-0.5f));
    return 0;
}
