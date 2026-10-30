/* A pointer parameter whose value is spilled across calls shares a slot
 * with the parameter itself, which the prologue and the spill both wrote
 * (sqlite3BtreeIndexMoveto's pRes): stored once. The result pointer is
 * written after a binary search loop full of calls that keeps many other
 * values live (signed sums that stay far from overflow). */
#include <stdio.h>

struct key { long v; int n; };

static int __attribute__((noinline)) cmp_at(const long *arr, int i, const struct key *k) {
    return arr[i] < k->v ? -1 : arr[i] > k->v;
}

static int __attribute__((noinline)) search(const long *arr, int n, const struct key *k, int *res) {
    int lo = 0, hi = n - 1, steps = 0, last = 0;
    long seen = 0, s1 = 1, s2 = 2, s3 = 3, s4 = 4, s5 = 5, s6 = 6, s7 = 7, s8 = 8;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        int c = cmp_at(arr, mid, k);
        steps++;
        seen += arr[mid];
        s1 += seen ^ s8; s2 += s1 * 3; s3 ^= s2 >> 1; s4 += s3 + mid;
        s5 ^= s4 << 2; s6 += s5 & 1023; s7 -= s6 >> 3; s8 += s7 ^ c;
        last = c;
        if (c < 0)
            lo = mid + 1;
        else if (c > 0)
            hi = mid - 1;
        else {
            *res = 0;
            return mid * 1000 + steps;
        }
    }
    *res = last;
    return (int)((seen + s1 + s2 + s3 + s4 + s5 + s6 + s7 + s8) % 997) - steps;
}

int main(void) {
    long arr[33];
    for (int i = 0; i < 33; i++)
        arr[i] = (long)i * i * 3 - 200;
    for (int t = -2; t < 40; t += 3) {
        struct key k = { (long)t * t * 3 - 200 + (t & 1), t };
        int res = 99;
        int r = search(arr, 33, &k, &res);
        printf("%d %d %d\n", t, r, res);
    }
    return 0;
}
