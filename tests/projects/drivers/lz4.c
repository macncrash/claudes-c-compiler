/* lz4 driver: compresses generated buffers with the fast, accelerated, HC
 * and frame APIs, prints each compressed size and a hash of the compressed
 * bytes, and checks that each decompresses back. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lz4.h"
#include "lz4hc.h"
#include "lz4frame.h"

static unsigned long long fnv(const unsigned char *p, size_t n) {
    unsigned long long h = 1469598103934665603ULL;
    for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 1099511628211ULL;
    return h;
}

static unsigned rng = 777;
static unsigned next(void) { rng = rng * 1664525u + 1013904223u; return rng >> 8; }

static void fill(char *b, int n, int kind) {
    for (int i = 0; i < n; i++) {
        switch (kind) {
        case 0: b[i] = "abcdefgh  lz4 compression "[(next() % 7) + (i % 13 == 0 ? 10 : 0)]; break;
        case 1: b[i] = (char)next(); break;
        case 2: b[i] = (char)(i % 251 == 0 ? next() : i / 300); break;
        default: b[i] = (char)((i * 31) ^ (i >> 3)); break;
        }
    }
}

int main(void) {
    static const int sizes[] = {0, 5, 1000, 65536, 200001};
    int bad = 0;
    for (int kind = 0; kind < 4; kind++) {
        for (unsigned s = 0; s < sizeof sizes / sizeof sizes[0]; s++) {
            int n = sizes[s];
            char *src = malloc(n + 1), *back = malloc(n + 1);
            fill(src, n, kind);
            int cap = LZ4_compressBound(n);
            char *dst = malloc(cap);
            int results[8], nr = 0;
            results[nr++] = LZ4_compress_default(src, dst, n, cap);
            int c = results[0];
            int d = LZ4_decompress_safe(dst, back, c, n + 1);
            int ok = d == n && memcmp(src, back, n) == 0;
            bad += !ok;
            printf("kind %d n %d default -> %d %016llx %s\n", kind, n, c, fnv((unsigned char *)dst, c), ok ? "ok" : "MISMATCH");
            for (int acc = 2; acc <= 16; acc *= 2) {
                c = LZ4_compress_fast(src, dst, n, cap, acc);
                d = LZ4_decompress_safe(dst, back, c, n + 1);
                ok = d == n && memcmp(src, back, n) == 0;
                bad += !ok;
                printf("kind %d n %d fast %d -> %d %016llx %s\n", kind, n, acc, c, fnv((unsigned char *)dst, c), ok ? "ok" : "MISMATCH");
            }
            static const int hc[] = {1, 4, 9, 12};
            for (int k = 0; k < 4; k++) {
                c = LZ4_compress_HC(src, dst, n, cap, hc[k]);
                d = LZ4_decompress_safe(dst, back, c, n + 1);
                ok = d == n && memcmp(src, back, n) == 0;
                bad += !ok;
                printf("kind %d n %d hc %d -> %d %016llx %s\n", kind, n, hc[k], c, fnv((unsigned char *)dst, c), ok ? "ok" : "MISMATCH");
            }
            /* Frame format with block and content checksums. */
            LZ4F_preferences_t prefs;
            memset(&prefs, 0, sizeof prefs);
            prefs.frameInfo.contentChecksumFlag = LZ4F_contentChecksumEnabled;
            prefs.frameInfo.blockChecksumFlag = LZ4F_blockChecksumEnabled;
            prefs.frameInfo.blockSizeID = LZ4F_max64KB;
            prefs.compressionLevel = 3;
            size_t fcap = LZ4F_compressFrameBound(n, &prefs);
            char *fr = malloc(fcap);
            size_t fc = LZ4F_compressFrame(fr, fcap, src, n, &prefs);
            if (LZ4F_isError(fc)) { printf("frame error %s\n", LZ4F_getErrorName(fc)); bad++; }
            else {
                LZ4F_dctx *dc;
                LZ4F_createDecompressionContext(&dc, LZ4F_VERSION);
                size_t outsz = n + 1, insz = fc;
                size_t r = LZ4F_decompress(dc, back, &outsz, fr, &insz, NULL);
                ok = !LZ4F_isError(r) && outsz == (size_t)n && memcmp(src, back, n) == 0;
                bad += !ok;
                LZ4F_freeDecompressionContext(dc);
                printf("kind %d n %d frame -> %zu %016llx %s\n", kind, n, fc, fnv((unsigned char *)fr, fc), ok ? "ok" : "MISMATCH");
            }
            free(fr); free(src); free(dst); free(back);
        }
    }
    printf("lz4 %s, %d bad\n", LZ4_versionString(), bad);
    return bad != 0;
}
