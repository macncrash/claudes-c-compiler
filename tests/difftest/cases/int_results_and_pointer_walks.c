/* Int results tested and returned, and pointers walked by a loop while a
 * copy of the start is still needed: getAndInitPage's
 * `rc = xGet(...); if (rc) return rc;`, vdbeRecordCompareString's memcmp
 * result tested twice, OP_VFilter filling apArg[] with post-index stores
 * and then passing the array, and an int index scaled by a struct size
 * next to a parameter that waits in a temporary. */
#include <stdio.h>
#include <string.h>

static int pages[8] = { 0, 3, -1, 0, 7, 0, -2147483647 - 1, 0 };
static long slot;

static int __attribute__((noinline)) get_page(int k) {
    return pages[k & 7];
}

static int __attribute__((noinline)) init_page(int (*get)(int), int k, long *out) {
    int rc = get(k);
    if (rc)
        return rc;
    *out = 0;
    return 0;
}

static int __attribute__((noinline)) compare_text(const char *a, int na, const char *b, int nb) {
    int n = na < nb ? na : nb;
    int rc = memcmp(a, b, (size_t)n);
    if (rc > 0)
        return 1;
    if (rc < 0)
        return -1;
    return na - nb;
}

static long __attribute__((noinline)) sum_args(long **args, int n) {
    long s = 0;
    for (int i = 0; i < n; i++)
        s = s * 7 + *args[i];
    return s;
}

static long __attribute__((noinline)) filter(long **ap, long *base, int n, long (*cb)(long **, int)) {
    long **p = ap;
    for (int i = 0; i < n; i++)
        *p++ = &base[i + 1];
    return cb(ap, n);
}

struct cell { long a, b, c; };

static long __attribute__((noinline)) pick(const struct cell *cells, const int *idx, long bias) {
    long x = cells[idx[0]].b * 3 + bias;
    long y = cells[idx[1]].c;
    return x - y + (long)idx[2] * 24;
}

int main(void) {
    for (int k = 0; k < 9; k++) {
        slot = 99;
        int rc = init_page(get_page, k, &slot);
        printf("init %d %d %ld\n", k, rc, slot);
    }
    const char *words[] = { "apple", "apples", "apricot", "", "banana", "apple" };
    for (int i = 0; i < 6; i++)
        for (int j = 0; j < 6; j++) {
            long r = compare_text(words[i], (int)strlen(words[i]), words[j], (int)strlen(words[j]));
            printf("%ld", r + 1);
        }
    printf("\n");
    long base[12];
    for (int i = 0; i < 12; i++)
        base[i] = i * i - 20;
    long *ap[11];
    for (int n = 0; n <= 10; n += 3)
        printf("filter %d %ld %ld\n", n, filter(ap, base, n, sum_args), n ? *ap[0] : 0L);
    struct cell cells[5];
    for (int i = 0; i < 5; i++)
        cells[i] = (struct cell){ i, i * 10 - 25, -i * 100 };
    int idx[3] = { 4, 2, -3 };
    printf("pick %ld\n", pick(cells, idx, -7));
    return 0;
}
