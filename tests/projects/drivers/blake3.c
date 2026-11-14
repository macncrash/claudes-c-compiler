/* BLAKE3 driver: hashes, keyed hashes and derived keys of the official
 * test-vector inputs (bytes i % 251) at the lengths the reference vectors
 * use, with 131 bytes of extended output, fed whole and in odd chunks. */
#include <stdio.h>
#include <string.h>
#include "blake3.h"

static unsigned char input[102400 + 1];

static void hex(const char *tag, size_t n, const unsigned char *out, size_t len) {
    printf("%s %zu ", tag, n);
    for (size_t i = 0; i < len; i++) printf("%02x", out[i]);
    printf("\n");
}

int main(void) {
    static const size_t lens[] = {0, 1, 1023, 1024, 1025, 2048, 2049, 3072, 3073, 4096, 4097, 5120, 5121,
                                  6144, 6145, 7168, 7169, 8192, 8193, 16384, 31744, 102400};
    const char *key = "whats the Elvish word for friend";
    const char *context = "BLAKE3 2019-12-27 16:29:52 test vectors context";
    unsigned char out[131];
    for (size_t i = 0; i < sizeof input; i++) input[i] = (unsigned char)(i % 251);
    for (size_t k = 0; k < sizeof lens / sizeof lens[0]; k++) {
        size_t n = lens[k];
        blake3_hasher h;
        blake3_hasher_init(&h);
        blake3_hasher_update(&h, input, n);
        blake3_hasher_finalize(&h, out, sizeof out);
        hex("hash", n, out, sizeof out);
        /* The same input fed in chunks of 1, 63, 1024 and 4097 bytes in turn. */
        blake3_hasher_init(&h);
        static const size_t chunks[] = {1, 63, 1024, 4097};
        for (size_t pos = 0, c = 0; pos < n; c++) {
            size_t take = chunks[c % 4] < n - pos ? chunks[c % 4] : n - pos;
            blake3_hasher_update(&h, input + pos, take);
            pos += take;
        }
        unsigned char out2[131];
        blake3_hasher_finalize(&h, out2, sizeof out2);
        printf("chunked %zu %s\n", n, memcmp(out, out2, sizeof out) ? "MISMATCH" : "same");
        blake3_hasher_init_keyed(&h, (const unsigned char *)key);
        blake3_hasher_update(&h, input, n);
        blake3_hasher_finalize(&h, out, sizeof out);
        hex("keyed", n, out, sizeof out);
        blake3_hasher_init_derive_key(&h, context);
        blake3_hasher_update(&h, input, n);
        blake3_hasher_finalize(&h, out, sizeof out);
        hex("derive", n, out, sizeof out);
        /* Output from an offset (seek into the extended output). */
        blake3_hasher_init(&h);
        blake3_hasher_update(&h, input, n);
        blake3_hasher_finalize_seek(&h, 1000, out, 40);
        hex("seek", n, out, 40);
    }
    printf("blake3 %s\n", blake3_version());
    return 0;
}
