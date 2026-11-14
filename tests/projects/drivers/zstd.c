/* zstd driver: compresses generated buffers of several kinds and sizes at
 * several levels, one-shot and streaming, prints each compressed size and a
 * hash of the compressed bytes, and checks that each decompresses back. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "zstd.h"

static unsigned long long fnv(const unsigned char *p, size_t n) {
    unsigned long long h = 1469598103934665603ULL;
    for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 1099511628211ULL;
    return h;
}

static unsigned rng = 12345;
static unsigned next(void) { rng = rng * 1103515245u + 12345u; return rng >> 8; }

static void fill(unsigned char *b, size_t n, int kind) {
    static const char *words[] = {"the ", "compiler ", "zstd ", "frame ", "block ", "entropy ", "huffman ", "match "};
    size_t i = 0;
    switch (kind) {
    case 0: /* text-like */
        while (i < n) {
            const char *w = words[next() % 8];
            for (size_t k = 0; w[k] && i < n; k++) b[i++] = (unsigned char)w[k];
        }
        break;
    case 1: /* random */
        for (; i < n; i++) b[i] = (unsigned char)next();
        break;
    case 2: /* mostly zeros */
        for (; i < n; i++) b[i] = (next() % 97 == 0) ? (unsigned char)next() : 0;
        break;
    default: /* structured records */
        for (; i < n; i++) b[i] = (unsigned char)((i * 7) ^ (i >> 5) ^ (i % 13 == 0 ? next() : 0));
        break;
    }
}

int main(void) {
    static const size_t sizes[] = {0, 1, 100, 4096, 65537, 300000};
    static const int levels[] = {-5, 1, 3, 9, 19};
    int bad = 0;
    for (int kind = 0; kind < 4; kind++) {
        for (size_t s = 0; s < sizeof sizes / sizeof sizes[0]; s++) {
            size_t n = sizes[s];
            unsigned char *src = malloc(n + 1);
            fill(src, n, kind);
            size_t cap = ZSTD_compressBound(n);
            unsigned char *dst = malloc(cap), *back = malloc(n + 1);
            for (size_t l = 0; l < sizeof levels / sizeof levels[0]; l++) {
                size_t c = ZSTD_compress(dst, cap, src, n, levels[l]);
                if (ZSTD_isError(c)) { printf("kind %d n %zu level %d error %s\n", kind, n, levels[l], ZSTD_getErrorName(c)); bad++; continue; }
                size_t d = ZSTD_decompress(back, n + 1, dst, c);
                int ok = !ZSTD_isError(d) && d == n && memcmp(back, src, n) == 0;
                bad += !ok;
                printf("kind %d n %zu level %d -> %zu %016llx %s\n", kind, n, levels[l], c, fnv(dst, c), ok ? "ok" : "MISMATCH");
            }
            /* Streaming in 1000-byte input chunks with a checksum. */
            ZSTD_CCtx *cc = ZSTD_createCCtx();
            ZSTD_CCtx_setParameter(cc, ZSTD_c_compressionLevel, 5);
            ZSTD_CCtx_setParameter(cc, ZSTD_c_checksumFlag, 1);
            size_t pos = 0, outpos = 0;
            while (1) {
                size_t chunk = n - pos < 1000 ? n - pos : 1000;
                ZSTD_inBuffer in = {src + pos, chunk, 0};
                ZSTD_outBuffer out = {dst + outpos, cap - outpos, 0};
                ZSTD_EndDirective e = pos + chunk == n ? ZSTD_e_end : ZSTD_e_continue;
                size_t r = ZSTD_compressStream2(cc, &out, &in, e);
                outpos += out.pos;
                pos += in.pos;
                if (ZSTD_isError(r)) { printf("stream error %s\n", ZSTD_getErrorName(r)); bad++; break; }
                if (e == ZSTD_e_end && r == 0) break;
            }
            ZSTD_freeCCtx(cc);
            size_t d = ZSTD_decompress(back, n + 1, dst, outpos);
            int ok = !ZSTD_isError(d) && d == n && memcmp(back, src, n) == 0;
            bad += !ok;
            printf("kind %d n %zu stream -> %zu %016llx %s\n", kind, n, outpos, fnv(dst, outpos), ok ? "ok" : "MISMATCH");
            free(src); free(dst); free(back);
        }
    }
    printf("zstd %s, %d bad\n", ZSTD_versionString(), bad);
    return bad != 0;
}
