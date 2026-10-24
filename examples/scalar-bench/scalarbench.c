/* Scalar integer benchmark that checks itself: branchy code with memory
 * traffic, the kind of work most C programs spend their time on.
 *
 * Each kernel's result is checked by an independent route (a sieve for the
 * interpreter's prime count, a bitwise CRC for the table-driven one, a
 * round trip for the compressor, the generator's own tallies for the word
 * counts, ordering and checksums for the sort and the tree), so a
 * miscompiled branch or load shows up as FAIL without a reference compiler.
 * The check lines also print checksums that must match between compilers.
 * Then each kernel is timed.
 *
 *   ./scalarbench          quick run (checks plus short timings)
 *   ./scalarbench --long   five times as many timing repetitions (make bench)
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifndef COMPILER_NAME
#define COMPILER_NAME "unknown"
#endif

static int failures;

static double now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}

static void report(const char *name, int ok, uint64_t sum) {
    printf("check  %-8s %s  %016llx\n", name, ok ? "PASS" : "FAIL", (unsigned long long)sum);
    if (!ok)
        failures++;
}

/* splitmix64 */
static uint64_t rng_state;

static void seed(uint64_t s) {
    rng_state = s;
}

static uint64_t rng(void) {
    uint64_t z = (rng_state += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

/* --- hash: open addressing, linear probing, backward-shift deletion ----- */

#define HT_BITS 16
#define HT_SIZE (1u << HT_BITS)
#define HT_MASK (HT_SIZE - 1)
#define HT_KEYS 40000

static uint64_t ht_keys[HT_SIZE];
static uint32_t ht_vals[HT_SIZE];

static uint32_t ht_slot(uint64_t key) {
    return (uint32_t)((key * 0x9e3779b97f4a7c15ull) >> (64 - HT_BITS));
}

/* Returns 1 if the key was new. Keys are nonzero; zero marks an empty slot. */
static int ht_insert(uint64_t key, uint32_t val) {
    uint32_t i = ht_slot(key);
    while (ht_keys[i]) {
        if (ht_keys[i] == key) {
            ht_vals[i] = val;
            return 0;
        }
        i = (i + 1) & HT_MASK;
    }
    ht_keys[i] = key;
    ht_vals[i] = val;
    return 1;
}

static int ht_find(uint64_t key, uint32_t *val) {
    uint32_t i = ht_slot(key);
    while (ht_keys[i]) {
        if (ht_keys[i] == key) {
            *val = ht_vals[i];
            return 1;
        }
        i = (i + 1) & HT_MASK;
    }
    return 0;
}

static void ht_delete(uint64_t key) {
    uint32_t i = ht_slot(key);
    while (ht_keys[i] != key) {
        if (!ht_keys[i])
            return;
        i = (i + 1) & HT_MASK;
    }
    /* Shift later entries of the run back into the hole where their home
     * slot allows it. */
    for (uint32_t j = i;;) {
        j = (j + 1) & HT_MASK;
        if (!ht_keys[j])
            break;
        uint32_t home = ht_slot(ht_keys[j]);
        if (((j - home) & HT_MASK) >= ((j - i) & HT_MASK)) {
            ht_keys[i] = ht_keys[j];
            ht_vals[i] = ht_vals[j];
            i = j;
        }
    }
    ht_keys[i] = 0;
}

/* Inserted keys are odd, missing ones even. */
static uint64_t ht_key(void) {
    return rng() | 1;
}

static uint64_t ht_missing(void) {
    return (rng() | 2) & ~1ull;
}

typedef struct {
    uint32_t inserted, hits, misses, after_delete;
    uint64_t sum;
} HashResult;

static HashResult hash_kernel(void) {
    HashResult r = { 0, 0, 0, 0, 0 };
    memset(ht_keys, 0, sizeof ht_keys);
    seed(1);
    for (uint32_t i = 0; i < HT_KEYS; i++)
        r.inserted += ht_insert(ht_key(), i);
    seed(1);
    for (uint32_t i = 0; i < HT_KEYS; i++) {
        uint32_t v;
        if (ht_find(ht_key(), &v) && v == i) {
            r.hits++;
            r.sum += v;
        }
    }
    seed(2);
    for (uint32_t i = 0; i < HT_KEYS; i++) {
        uint32_t v;
        r.misses += !ht_find(ht_missing(), &v);
    }
    seed(1);
    for (uint32_t i = 0; i < HT_KEYS / 2; i++)
        ht_delete(ht_key());
    seed(1);
    for (uint32_t i = 0; i < HT_KEYS; i++) {
        uint32_t v;
        if (ht_find(ht_key(), &v)) {
            r.after_delete++;
            r.sum = r.sum * 31 + v;
        }
    }
    return r;
}

/* Operations per hash_kernel run: inserts, lookups, deletes, lookups. */
#define HT_OPS (HT_KEYS + HT_KEYS + HT_KEYS + HT_KEYS / 2 + HT_KEYS)

static void check_hash(void) {
    HashResult r = hash_kernel();
    /* The surviving keys are the second half, found with their values. */
    uint64_t sum = 0;
    for (uint32_t i = 0; i < HT_KEYS; i++)
        sum += i;
    for (uint32_t i = HT_KEYS / 2; i < HT_KEYS; i++)
        sum = sum * 31 + i;
    report("hash", r.inserted == HT_KEYS && r.hits == HT_KEYS && r.misses == HT_KEYS
                   && r.after_delete == HT_KEYS / 2 && r.sum == sum, r.sum);
}

/* --- sort: quicksort, median of three, insertion sort below 16 ---------- */

#define SORT_N 100000

static int32_t sort_src[SORT_N], sort_a[SORT_N];

static void insertion_sort(int32_t *a, int n) {
    for (int i = 1; i < n; i++) {
        int32_t x = a[i];
        int j = i - 1;
        while (j >= 0 && a[j] > x) {
            a[j + 1] = a[j];
            j--;
        }
        a[j + 1] = x;
    }
}

static void quicksort(int32_t *a, int n) {
    while (n > 16) {
        int32_t x = a[0], y = a[n / 2], z = a[n - 1];
        int32_t p = x < y ? (y < z ? y : x < z ? z : x) : (x < z ? x : y < z ? z : y);
        int i = -1, j = n;
        for (;;) {
            do i++; while (a[i] < p);
            do j--; while (a[j] > p);
            if (i >= j)
                break;
            int32_t t = a[i];
            a[i] = a[j];
            a[j] = t;
        }
        /* Recurse into the smaller side, loop on the larger. */
        int left = j + 1;
        if (left < n - left) {
            quicksort(a, left);
            a += left;
            n -= left;
        } else {
            quicksort(a + left, n - left);
            n = left;
        }
    }
    insertion_sort(a, n);
}

static void sort_kernel(void) {
    memcpy(sort_a, sort_src, sizeof sort_a);
    quicksort(sort_a, SORT_N);
}

static void check_sort(void) {
    seed(3);
    uint64_t sum = 0, xor = 0;
    for (int i = 0; i < SORT_N; i++) {
        sort_src[i] = (int32_t)(rng() % 2000001) - 1000000;
        sum += (uint32_t)sort_src[i];
        xor ^= (uint32_t)sort_src[i] * 0x9e3779b1u;
    }
    sort_kernel();
    int ok = 1;
    uint64_t sum2 = 0, xor2 = 0, h = 0;
    for (int i = 0; i < SORT_N; i++) {
        if (i && sort_a[i - 1] > sort_a[i])
            ok = 0;
        sum2 += (uint32_t)sort_a[i];
        xor2 ^= (uint32_t)sort_a[i] * 0x9e3779b1u;
        h = h * 1000003 + (uint32_t)sort_a[i];
    }
    report("sort", ok && sum == sum2 && xor == xor2, h);
}

/* --- interp: a register bytecode machine counting primes ---------------- */

enum { OP_LI, OP_ADD, OP_MUL, OP_REM, OP_BGE, OP_BLT, OP_BEQZ, OP_JMP, OP_HALT };

typedef struct {
    uint8_t op, a, b, c;
    int32_t imm;
} Insn;

#define PRIME_LIMIT 30000

/* Count primes below r2 by trial division. The limit is set at run time
 * (see main) so a compiler cannot run the program while compiling. */
static Insn prime_program[] = {
    /* 0 */ { OP_LI, 1, 0, 0, 2 },              /* n = 2 */
    /* 1 */ { OP_LI, 2, 0, 0, 0 },              /* limit */
    /* 2 */ { OP_LI, 3, 0, 0, 0 },              /* count = 0 */
    /* 3 */ { OP_LI, 6, 0, 0, 1 },              /* one */
    /* 4 */ { OP_BGE, 1, 2, 0, 15 },            /* while n < limit */
    /* 5 */ { OP_LI, 4, 0, 0, 2 },              /*   d = 2 */
    /* 6 */ { OP_MUL, 5, 4, 4, 0 },             /*   while d * d <= n */
    /* 7 */ { OP_BLT, 1, 5, 0, 12 },
    /* 8 */ { OP_REM, 5, 1, 4, 0 },             /*     if n % d == 0: next n */
    /* 9 */ { OP_BEQZ, 5, 0, 0, 13 },
    /* 10 */ { OP_ADD, 4, 4, 6, 0 },            /*     d++ */
    /* 11 */ { OP_JMP, 0, 0, 0, 6 },
    /* 12 */ { OP_ADD, 3, 3, 6, 0 },            /*   prime: count++ */
    /* 13 */ { OP_ADD, 1, 1, 6, 0 },            /*   n++ */
    /* 14 */ { OP_JMP, 0, 0, 0, 4 },
    /* 15 */ { OP_HALT, 0, 0, 0, 0 },
};

typedef struct {
    int64_t result;
    uint64_t steps;
} InterpResult;

static InterpResult run(const Insn *code) {
    int64_t r[8] = { 0 };
    uint64_t steps = 0;
    for (int pc = 0;;) {
        const Insn *in = &code[pc++];
        steps++;
        switch (in->op) {
        case OP_LI: r[in->a] = in->imm; break;
        case OP_ADD: r[in->a] = r[in->b] + r[in->c]; break;
        case OP_MUL: r[in->a] = r[in->b] * r[in->c]; break;
        case OP_REM: r[in->a] = r[in->b] % r[in->c]; break;
        case OP_BGE: if (r[in->a] >= r[in->b]) pc = in->imm; break;
        case OP_BLT: if (r[in->a] < r[in->b]) pc = in->imm; break;
        case OP_BEQZ: if (r[in->a] == 0) pc = in->imm; break;
        case OP_JMP: pc = in->imm; break;
        case OP_HALT: return (InterpResult){ r[3], steps };
        }
    }
}

static uint64_t interp_steps;

static void check_interp(void) {
    InterpResult r = run(prime_program);
    static uint8_t composite[PRIME_LIMIT];
    int64_t count = 0;
    for (int n = 2; n < PRIME_LIMIT; n++) {
        if (composite[n])
            continue;
        count++;
        for (int m = 2 * n; m < PRIME_LIMIT; m += n)
            composite[m] = 1;
    }
    interp_steps = r.steps;
    report("interp", r.result == count, r.steps);
}

/* --- tree: unbalanced binary search tree of random keys ----------------- */

#define TREE_KEYS 50000

typedef struct Node {
    uint32_t key;
    struct Node *left, *right;
} Node;

static Node tree_pool[TREE_KEYS];

typedef struct {
    uint32_t distinct, found, missing;
    uint64_t sum;
} TreeResult;

static TreeResult tree_kernel(void) {
    TreeResult r = { 0, 0, 0, 0 };
    Node *root = 0;
    seed(4);
    for (int i = 0; i < TREE_KEYS; i++) {
        uint32_t key = (uint32_t)(rng() % (1u << 20)) * 2;
        Node **p = &root;
        while (*p && (*p)->key != key)
            p = key < (*p)->key ? &(*p)->left : &(*p)->right;
        if (!*p) {
            Node *n = &tree_pool[r.distinct++];
            n->key = key;
            n->left = n->right = 0;
            *p = n;
        }
    }
    /* Every inserted key is found; the odd key just above it is not. */
    seed(4);
    for (int i = 0; i < TREE_KEYS; i++) {
        uint32_t key = (uint32_t)(rng() % (1u << 20)) * 2;
        for (int k = 0; k < 2; k++) {
            const Node *n = root;
            while (n && n->key != key + k)
                n = key + k < n->key ? n->left : n->right;
            if (n && k == 0) {
                r.found++;
                r.sum += n - tree_pool;
            }
            r.missing += !n && k == 1;
        }
    }
    /* In-order walk: ascending, every node once. */
    const Node *stack[64];
    int sp = 0;
    uint32_t last = 0, walked = 0;
    for (const Node *n = root; n || sp;) {
        while (n) {
            if (sp == 64)
                return r;
            stack[sp++] = n;
            n = n->left;
        }
        n = stack[--sp];
        if (walked && n->key <= last)
            return r;
        last = n->key;
        walked++;
        r.sum = r.sum * 31 + n->key;
        n = n->right;
    }
    if (walked != r.distinct)
        r.distinct = 0;
    return r;
}

/* Operations per tree_kernel run: inserts, hit and miss lookups, visits. */
#define TREE_OPS (4 * TREE_KEYS)

static void check_tree(void) {
    TreeResult r = tree_kernel();
    report("tree", r.distinct > TREE_KEYS * 9 / 10 && r.found == TREE_KEYS && r.missing == TREE_KEYS, r.sum);
}

/* --- words: tokenize text and count word frequencies -------------------- */

#define TEXT_SIZE (256 * 1024)
#define DICT 64

static const char *const syllables[16] = {
    "ka", "ri", "to", "me", "su", "no", "la", "pe", "di", "go", "ha", "yu", "be", "zo", "wi", "fa",
};

static char text[TEXT_SIZE];
static size_t text_len;
static uint32_t text_tally[DICT], text_words;

/* Word k: one syllable for k < 16, two above. */
static int dict_word(int k, char *out) {
    int n = 0;
    out[n++] = syllables[k % 16][0];
    out[n++] = syllables[k % 16][1];
    if (k >= 16) {
        out[n++] = syllables[k / 16][0];
        out[n++] = syllables[k / 16][1];
    }
    return n;
}

static void make_text(void) {
    static const char *const seps[4] = { " ", ", ", ". ", "\n" };
    seed(5);
    size_t n = 0;
    while (n + 8 < TEXT_SIZE) {
        uint64_t r = rng();
        int k = (int)((r & 63) & ((r >> 6) & 63));
        char w[4];
        int len = dict_word(k, w);
        if ((r >> 12) % 8 == 0)
            w[0] = (char)(w[0] - 'a' + 'A');
        memcpy(text + n, w, len);
        n += len;
        const char *s = seps[(r >> 16) % 4];
        memcpy(text + n, s, strlen(s));
        n += strlen(s);
        text_tally[k]++;
        text_words++;
    }
    text_len = n;
}

typedef struct {
    char word[8];
    uint32_t len, hash, count;
    int next;
} Entry;

typedef struct {
    Entry entries[DICT * 2];
    int buckets[128];
    int used;
    uint32_t tokens;
} WordTable;

static WordTable words;

static uint32_t fnv(const char *s, uint32_t n) {
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < n; i++)
        h = (h ^ (uint8_t)s[i]) * 16777619u;
    return h;
}

static Entry *word_lookup(WordTable *t, const char *w, uint32_t len, uint32_t h, int add) {
    int *link = &t->buckets[h & 127];
    for (int e = *link; e >= 0; e = t->entries[e].next) {
        Entry *x = &t->entries[e];
        if (x->hash == h && x->len == len && !memcmp(x->word, w, len))
            return x;
    }
    if (!add || t->used == DICT * 2 || len > sizeof t->entries[0].word)
        return 0;
    Entry *x = &t->entries[t->used];
    memcpy(x->word, w, len);
    x->len = len;
    x->hash = h;
    x->count = 0;
    x->next = *link;
    *link = t->used++;
    return x;
}

static void words_kernel(void) {
    WordTable *t = &words;
    memset(t->buckets, -1, sizeof t->buckets);
    t->used = 0;
    t->tokens = 0;
    char w[16];
    uint32_t len = 0;
    for (size_t i = 0; i <= text_len; i++) {
        char c = i < text_len ? text[i] : ' ';
        if (c >= 'A' && c <= 'Z')
            c = (char)(c - 'A' + 'a');
        if (c >= 'a' && c <= 'z') {
            if (len < sizeof w)
                w[len] = c;
            len++;
        } else if (len) {
            if (len <= sizeof w) {
                Entry *x = word_lookup(t, w, len, fnv(w, len), 1);
                if (x)
                    x->count++;
            }
            t->tokens++;
            len = 0;
        }
    }
}

static void check_words(void) {
    words_kernel();
    int ok = words.tokens == text_words;
    uint64_t h = words.used;
    for (int k = 0; k < DICT; k++) {
        char w[4];
        int len = dict_word(k, w);
        Entry *x = word_lookup(&words, w, len, fnv(w, len), 0);
        uint32_t count = x ? x->count : 0;
        ok &= count == text_tally[k];
        h = h * 31 + count;
    }
    report("words", ok, h);
}

/* --- lz: LZ77 compression with hash chains, then decompression ---------- */

#define LZ_N (64 * 1024)
#define LZ_HASH_BITS 14
#define LZ_DEPTH 8

static uint8_t lz_in[LZ_N], lz_comp[LZ_N + LZ_N / 127 + 16], lz_out[LZ_N];
static int32_t lz_head[1 << LZ_HASH_BITS], lz_prev[LZ_N];

static uint32_t lz_hash(const uint8_t *p) {
    uint32_t x = p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16;
    return (x * 2654435761u) >> (32 - LZ_HASH_BITS);
}

static size_t lz_literals(uint8_t *out, size_t o, const uint8_t *in, size_t from, size_t to) {
    while (from < to) {
        size_t n = to - from < 127 ? to - from : 127;
        out[o++] = (uint8_t)n;
        memcpy(out + o, in + from, n);
        o += n;
        from += n;
    }
    return o;
}

/* Tokens: 1-127 = that many literal bytes follow; 0x80 | (len - 3) = a
 * match of len bytes at the 16-bit little-endian distance that follows. */
static size_t lz_compress(const uint8_t *in, size_t n, uint8_t *out) {
    memset(lz_head, -1, sizeof lz_head);
    size_t i = 0, o = 0, lit = 0;
    while (i + 3 <= n) {
        uint32_t h = lz_hash(in + i);
        int32_t cand = lz_head[h];
        lz_prev[i] = cand;
        lz_head[h] = (int32_t)i;
        size_t best = 0, dist = 0;
        for (int depth = 0; cand >= 0 && depth < LZ_DEPTH && i - cand <= 65535; depth++) {
            size_t max = n - i < 130 ? n - i : 130, l = 0;
            while (l < max && in[cand + l] == in[i + l])
                l++;
            if (l > best) {
                best = l;
                dist = i - cand;
            }
            cand = lz_prev[cand];
        }
        if (best < 3) {
            i++;
            continue;
        }
        o = lz_literals(out, o, in, lit, i);
        out[o++] = (uint8_t)(0x80 | (best - 3));
        out[o++] = (uint8_t)dist;
        out[o++] = (uint8_t)(dist >> 8);
        for (size_t k = 1; k < best && i + k + 3 <= n; k++) {
            h = lz_hash(in + i + k);
            lz_prev[i + k] = lz_head[h];
            lz_head[h] = (int32_t)(i + k);
        }
        i += best;
        lit = i;
    }
    return lz_literals(out, o, in, lit, n);
}

static size_t lz_decompress(const uint8_t *in, size_t n, uint8_t *out) {
    size_t i = 0, o = 0;
    while (i < n) {
        uint8_t c = in[i++];
        if (c < 0x80) {
            memcpy(out + o, in + i, c);
            i += c;
            o += c;
        } else {
            size_t len = (size_t)(c & 0x7f) + 3, dist = in[i] | (size_t)in[i + 1] << 8;
            i += 2;
            for (size_t k = 0; k < len; k++, o++)
                out[o] = out[o - dist];
        }
    }
    return o;
}

static size_t lz_size;

static int lz_kernel(void) {
    lz_size = lz_compress(lz_in, LZ_N, lz_comp);
    return lz_decompress(lz_comp, lz_size, lz_out) == LZ_N && !memcmp(lz_in, lz_out, LZ_N);
}

static void check_lz(void) {
    /* Text with every eighth 512-byte block random. */
    memcpy(lz_in, text, LZ_N);
    seed(6);
    for (size_t b = 0; b < LZ_N; b += 4096)
        for (size_t i = 0; i < 512; i++)
            lz_in[b + i] = (uint8_t)rng();
    int ok = lz_kernel();
    report("lz", ok && lz_size < LZ_N * 3 / 4, lz_size);
}

/* --- crc: table-driven CRC-32 ------------------------------------------- */

#define CRC_N (256 * 1024)

static uint32_t crc_table[256];
static uint8_t crc_buf[CRC_N];

static uint32_t crc32(const uint8_t *p, size_t n) {
    uint32_t c = 0xffffffffu;
    for (size_t i = 0; i < n; i++)
        c = crc_table[(c ^ p[i]) & 0xff] ^ (c >> 8);
    return ~c;
}

static uint32_t crc32_bitwise(const uint8_t *p, size_t n) {
    uint32_t c = 0xffffffffu;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++)
            c = c & 1 ? (c >> 1) ^ 0xedb88320u : c >> 1;
    }
    return ~c;
}

static void check_crc(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++)
            c = c & 1 ? (c >> 1) ^ 0xedb88320u : c >> 1;
        crc_table[i] = c;
    }
    seed(7);
    for (size_t i = 0; i < CRC_N; i++)
        crc_buf[i] = (uint8_t)rng();
    uint32_t c = crc32(crc_buf, CRC_N);
    /* The standard check value, then the whole buffer both ways. */
    report("crc", crc32((const uint8_t *)"123456789", 9) == 0xcbf43926u && c == crc32_bitwise(crc_buf, CRC_N), c);
}

/* --- timing ------------------------------------------------------------- */

static volatile uint64_t sink;

#define TIME(name, reps, items, body)                                           \
    do {                                                                        \
        double t0_ = now();                                                     \
        for (int r_ = 0; r_ < (reps); r_++) { body; }                          \
        double t_ = now() - t0_;                                                \
        total += t_;                                                            \
        printf("time   %-8s %8.2f M items/s  %.3f s\n", name,                  \
               (double)(reps) * (double)(items) / t_ / 1e6, t_);                \
    } while (0)

int main(int argc, char **argv) {
    int scale = argc > 1 && !strcmp(argv[1], "--long") ? 5 : 1;
    static volatile int32_t prime_limit = PRIME_LIMIT;
    prime_program[1].imm = prime_limit;
    printf("scalarbench (%s)\n", COMPILER_NAME);
    make_text();
    check_hash();
    check_sort();
    check_interp();
    check_tree();
    check_words();
    check_lz();
    check_crc();

    double total = 0;
    TIME("hash", 100 * scale, HT_OPS, sink += hash_kernel().sum);
    TIME("sort", 25 * scale, SORT_N, sort_kernel(); sink += sort_a[SORT_N / 2]);
    TIME("interp", 20 * scale, interp_steps, sink += run(prime_program).result);
    TIME("tree", 15 * scale, TREE_OPS, sink += tree_kernel().sum);
    TIME("words", 120 * scale, text_len, words_kernel(); sink += words.tokens);
    TIME("lz", 300 * scale, LZ_N, sink += lz_kernel());
    TIME("crc", 200 * scale, CRC_N, sink += crc32(crc_buf, CRC_N));
    printf("total time: %.3f s\n", total);
    printf(failures ? "%d check(s) FAILED\n" : "all checks passed\n", failures);
    return failures != 0;
}
