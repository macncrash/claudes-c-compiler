/* Driver for aws-lc's FIPS module (bcm.c, built without assembly):
 * known-answer vectors for SHA-2, HMAC, AES and AES-GCM, then deterministic
 * work over bignum modular arithmetic and P-256 whose outputs are hashed.
 * The runner compares everything printed with the reference compiler's build. */
#include <stdio.h>
#include <string.h>
#include <openssl/aead.h>
#include <openssl/aes.h>
#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/hmac.h>
#include <openssl/digest.h>
#include <openssl/nid.h>
#include <openssl/sha.h>

static unsigned long long H = 1469598103934665603ULL;
static void hb(const void *p, size_t n) { const unsigned char *c = p; while (n--) H = (H ^ *c++) * 1099511628211ULL; }
static unsigned long long rng = 0x2545f4914f6cdd1dULL;
static unsigned long long next(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return rng; }
static void rand_bytes(unsigned char *p, size_t n) { for (size_t i = 0; i < n; i++) p[i] = (unsigned char)next(); }

static void unhex(unsigned char *out, const char *hex) {
    for (size_t i = 0; hex[2 * i]; i++) { unsigned v; sscanf(hex + 2 * i, "%2x", &v); out[i] = (unsigned char)v; }
}
static void kat(const char *tag, const unsigned char *got, const char *want_hex, size_t n) {
    unsigned char want[128];
    unhex(want, want_hex);
    printf("%s %s ", tag, memcmp(got, want, n) ? "BAD" : "ok");
    for (size_t i = 0; i < n; i++) printf("%02x", got[i]);
    printf("\n");
}

int main(void) {
    unsigned char md[64];
    SHA256((const unsigned char *)"abc", 3, md);
    kat("sha256", md, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", 32);
    SHA512((const unsigned char *)"abc", 3, md);
    kat("sha512", md, "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
                      "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f", 64);
    static unsigned char big[1000000];
    memset(big, 'a', sizeof big);
    SHA256(big, sizeof big, md);
    kat("sha256-million-a", md, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0", 32);

    /* RFC 4231 test case 2 */
    unsigned int mdlen = 0;
    HMAC(EVP_sha256(), "Jefe", 4, (const unsigned char *)"what do ya want for nothing?", 28, md, &mdlen);
    kat("hmac-sha256", md, "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843", 32);

    /* FIPS-197 C.1 */
    unsigned char key[32], pt[16], ct[16];
    unhex(key, "000102030405060708090a0b0c0d0e0f");
    unhex(pt, "00112233445566778899aabbccddeeff");
    AES_KEY ak;
    AES_set_encrypt_key(key, 128, &ak);
    AES_encrypt(pt, ct, &ak);
    kat("aes128", ct, "69c4e0d86a7b0430d8cdb78070b4c55a", 16);

    /* AES-128-GCM: the empty-plaintext vector (zero key and nonce). */
    unsigned char nonce[12] = { 0 }, out[4096 + 16];
    size_t out_len = 0;
    memset(key, 0, sizeof key);
    EVP_AEAD_CTX ctx;
    EVP_AEAD_CTX_init(&ctx, EVP_aead_aes_128_gcm(), key, 16, 16, NULL);
    EVP_AEAD_CTX_seal(&ctx, out, &out_len, sizeof out, nonce, 12, NULL, 0, NULL, 0);
    kat("aes128gcm-tag", out, "58e2fccefa7e3061367f1d57a4e7455a", 16);
    EVP_AEAD_CTX_cleanup(&ctx);

    /* Stress: SHA-2 over random lengths, HMAC, AES-GCM seal and open. */
    static unsigned char buf[4096], back[4096];
    rand_bytes(buf, sizeof buf);
    for (int i = 0; i < 100; i++) {
        size_t n = (size_t)(next() % sizeof buf);
        SHA256(buf, n, md); hb(md, 32);
        SHA512(buf, n, md); hb(md, 64);
        rand_bytes(key, 32);
        HMAC(EVP_sha512(), key, 32, buf, n, md, &mdlen); hb(md, mdlen);
        EVP_AEAD_CTX_init(&ctx, i & 1 ? EVP_aead_aes_256_gcm() : EVP_aead_aes_128_gcm(), key, i & 1 ? 32 : 16, 16, NULL);
        rand_bytes(nonce, 12);
        EVP_AEAD_CTX_seal(&ctx, out, &out_len, sizeof out, nonce, 12, buf, n, key, 13);
        hb(out, out_len);
        size_t back_len = 0;
        int ok = EVP_AEAD_CTX_open(&ctx, back, &back_len, sizeof back, nonce, 12, out, out_len, key, 13);
        hb(&ok, sizeof ok);
        hb(back, back_len);
        EVP_AEAD_CTX_cleanup(&ctx);
    }
    printf("digest-aead-stress %016llx\n", H);

    /* Bignum: modular exponentiation and inverses on random operands. */
    BN_CTX *bctx = BN_CTX_new();
    BIGNUM *a = BN_new(), *e = BN_new(), *m = BN_new(), *r = BN_new();
    unsigned char raw[256];
    for (int i = 0; i < 20; i++) {
        size_t bytes = 16 + (size_t)(next() % 112);
        rand_bytes(raw, bytes); BN_bin2bn(raw, bytes, a);
        rand_bytes(raw, bytes); BN_bin2bn(raw, bytes, e);
        rand_bytes(raw, bytes); raw[bytes - 1] |= 1; raw[0] |= 0x80; BN_bin2bn(raw, bytes, m);
        BN_mod_exp(r, a, e, m, bctx);
        size_t len = BN_bn2bin(r, raw); hb(raw, len);
        if (BN_mod_inverse(r, a, m, bctx)) { len = BN_bn2bin(r, raw); hb(raw, len); }
        BN_mul(r, a, e, bctx); len = BN_bn2bin(r, raw); hb(raw, len);
    }
    printf("bn-stress %016llx\n", H);

    /* P-256: multiply the generator and a derived point by random scalars. */
    EC_GROUP *g = EC_GROUP_new_by_curve_name(NID_X9_62_prime256v1);
    EC_POINT *p = EC_POINT_new(g), *q = EC_POINT_new(g);
    BIGNUM *k = BN_new();
    for (int i = 0; i < 20; i++) {
        rand_bytes(raw, 32);
        BN_bin2bn(raw, 32, k);
        EC_POINT_mul(g, p, k, NULL, NULL, bctx);
        size_t len = EC_POINT_point2oct(g, p, POINT_CONVERSION_UNCOMPRESSED, raw, sizeof raw, bctx);
        hb(raw, len);
        EC_POINT_mul(g, q, NULL, p, k, bctx);
        len = EC_POINT_point2oct(g, q, POINT_CONVERSION_COMPRESSED, raw, sizeof raw, bctx);
        hb(raw, len);
    }
    printf("p256-stress %016llx\n", H);
    return 0;
}
