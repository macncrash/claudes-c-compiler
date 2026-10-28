/* A format loop that calls out on every conversion and uses many string
 * literals and tables, each on one conversion's path only, beside a loop
 * that uses two tables on most iterations. */
#include <stdio.h>
#include <string.h>

static const char digits[] = "0123456789abcdef";
static const char upper[] = "0123456789ABCDEF";
static const char *const names[] = { "zero", "one", "two", "three" };
static const unsigned char weights[16] = { 3, 1, 4, 1, 5, 9, 2, 6, 5, 3, 5, 8, 9, 7, 9, 3 };
static int calls;

static void __attribute__((noinline)) put(char *out, int *n, const char *s) {
    size_t len = strlen(s);
    memcpy(out + *n, s, len);
    *n += (int)len;
    calls++;
}

static void __attribute__((noinline)) put_num(char *out, int *n, unsigned long v, const char *set, int base) {
    char tmp[32];
    int i = 0;
    do {
        tmp[i++] = set[v % base];
        v /= base;
    } while (v);
    while (i)
        out[(*n)++] = tmp[--i];
    calls++;
}

static int __attribute__((noinline)) format(char *out, const char *fmt, const unsigned long *args) {
    int n = 0;
    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            out[n++] = *fmt;
            continue;
        }
        switch (*++fmt) {
        case 'd': put_num(out, &n, *args++, digits, 10); break;
        case 'x': put_num(out, &n, *args++, digits, 16); break;
        case 'X': put_num(out, &n, *args++, upper, 16); break;
        case 'o': put_num(out, &n, *args++, digits, 8); break;
        case 'b': put_num(out, &n, *args++, digits, 2); break;
        case 'n': put(out, &n, names[*args++ & 3]); break;
        case 'B': put(out, &n, *args++ ? "true" : "false"); break;
        case 'N': put(out, &n, "(null)"); break;
        case 'I': put(out, &n, "Inf"); break;
        case 'Q': put(out, &n, "NaN"); break;
        case 'w': put_num(out, &n, weights[*args++ & 15], digits, 10); break;
        case 'q': put(out, &n, "'"); put(out, &n, "quoted"); put(out, &n, "'"); break;
        case '%': out[n++] = '%'; break;
        default: put(out, &n, "<?>"); break;
        }
    }
    out[n] = 0;
    return n;
}

/* Both tables on most iterations, with a call on each. */
static unsigned __attribute__((noinline)) checksum(const char *s) {
    unsigned h = 0;
    for (; *s; s++) {
        int c = (unsigned char)*s;
        h = h * 31 + (c < 128 ? weights[c & 15] : (unsigned)upper[c & 15]);
        if (c & 1)
            h ^= (unsigned)strlen(digits);
    }
    return h;
}

int main(void) {
    char buf[512];
    unsigned long args[] = { 42, 255, 255, 8, 5, 2, 1, 0, 7, 3 };
    int n = format(buf, "%d %x %X %o %b %n %B %B %N %I %Q %w %q %% %z|", args);
    printf("%d [%s] calls %d\n", n, buf, calls);
    unsigned total = 0;
    for (unsigned long i = 0; i < 200; i++) {
        unsigned long a[] = { i, i * 7, i * 13, i, i, i, i & 1, 0, i, i };
        format(buf, "%d:%x:%X:%o:%b:%n:%B:%w", a);
        total = total * 33 + checksum(buf);
    }
    printf("total %u calls %d\n", total, calls);
    return 0;
}
