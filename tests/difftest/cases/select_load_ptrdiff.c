/* Two things from a binary search tree walk.
 *
 * `n = k < n->key ? n->left : n->right`: each arm loads a different field;
 * both become one load from a selected address. Also arms loading from
 * different pointers, narrow fields widened the same way on both sides
 * (signed and unsigned), widened differently (stays a branch), and one arm
 * loading volatile (stays a branch).
 *
 * `n - pool`: pointer differences divided by element sizes that are odd,
 * powers of two, and both (3, 12, 24, 40, 6), forward and backward. */
#include <stdint.h>
#include <stdio.h>

typedef struct Node {
    uint32_t key;
    struct Node *left, *right;
} Node;

static Node pool[2000];

__attribute__((noinline)) static Node *insert(Node *root, uint32_t key, int *used) {
    Node **p = &root;
    while (*p && (*p)->key != key)
        p = key < (*p)->key ? &(*p)->left : &(*p)->right;
    if (!*p) {
        Node *n = &pool[(*used)++];
        n->key = key;
        n->left = n->right = 0;
        *p = n;
    }
    return root;
}

__attribute__((noinline)) static const Node *find(const Node *n, uint32_t key) {
    while (n && n->key != key)
        n = key < n->key ? n->left : n->right;
    return n;
}

typedef struct { int16_t a; uint8_t u; int64_t b; } Mixed;

/* Arms loading from different pointers, with different field types. */
__attribute__((noinline)) static long pick(const Mixed *x, const Mixed *y, int c) {
    long s = 0;
    for (int i = 0; i < 8; i++)
        s += (c >> i) & 1 ? x[i].b : y[7 - i].b;
    s += c & 1 ? x->a : y->a;
    s += c & 2 ? x[1].u : y[2].u;
    s += c & 4 ? x[3].u : y[4].a;
    return s;
}

static volatile int vol = 7;
__attribute__((noinline)) static int maybe_volatile(const int *p, int c) {
    return c ? vol : p[c + 1];
}

typedef struct { uint8_t b[3]; } Three;
typedef struct { int32_t a, b, c; } Twelve;
typedef struct { int16_t s[3]; } Six;
typedef struct { char c[40]; } Forty;

__attribute__((noinline)) static long diffs(int i, int j) {
    static Three t3[50];
    static Twelve t12[50];
    static Node t24[50];
    static Forty t40[50];
    static Six t6[50];
    return (&t3[i] - &t3[j]) * 1000000 + (&t12[i] - &t12[j]) * 10000
        + (&t24[j] - &t24[i]) * 100 + (&t40[i] - &t40[j]) + (&t6[j] - &t6[i]) * 7;
}

int main(void) {
    uint32_t x = 99;
    int used = 0;
    Node *root = 0;
    for (int i = 0; i < 2000; i++) {
        x = x * 1664525u + 1013904223u;
        root = insert(root, (x >> 12) % 5000 * 2, &used);
    }
    long hits = 0, idx = 0;
    x = 99;
    for (int i = 0; i < 2000; i++) {
        x = x * 1664525u + 1013904223u;
        uint32_t k = (x >> 12) % 5000 * 2;
        const Node *n = find(root, k);
        hits += n != 0;
        idx += n ? n - pool : -1;
        hits += find(root, k + 1) != 0;
    }
    printf("tree %d %ld %ld\n", used, hits, idx);

    Mixed m[8], m2[8];
    for (int i = 0; i < 8; i++) {
        m[i].a = (int16_t)(i * 300 - 1000);
        m[i].u = (uint8_t)(200 + i);
        m[i].b = (int64_t)i * 0x123456789ll - 5;
        m2[i].a = (int16_t)(-i * 7);
        m2[i].u = (uint8_t)(i * 3);
        m2[i].b = (int64_t)i * -1000003;
    }
    printf("pick %ld %ld %ld %ld\n", pick(m, m2, 0xa5), pick(m, m2, 0x5a), pick(m, m2, 0), pick(m2, m, 0xff));
    int arr[4] = { 3, 1, 4, 1 };
    printf("volatile %d %d\n", maybe_volatile(arr, 0), maybe_volatile(arr, 1));
    printf("diffs %ld %ld %ld\n", diffs(0, 0), diffs(37, 5), diffs(2, 49));
    return 0;
}
