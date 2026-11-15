/* Driver for ring's portable C (X25519, Poly1305, AES without hardware
 * instructions, P-256 point arithmetic): known-answer
 * vectors, then deterministic stress loops whose outputs are hashed. The
 * runner compares everything printed with the reference compiler's build. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <ring-core/base.h>
#include <ring-core/aes.h>

typedef uint64_t Limb;

void x25519_scalar_mult_generic_masked(uint8_t out[32], const uint8_t scalar_masked[32], const uint8_t point[32]);
void x25519_public_from_private_generic_masked(uint8_t out[32], const uint8_t private_masked[32], int use_adx);
void CRYPTO_poly1305_init(void *state, const uint8_t key[32]);
void CRYPTO_poly1305_update(void *state, const uint8_t *in, size_t in_len);
void CRYPTO_poly1305_finish(void *state, uint8_t mac[16]);
int aes_nohw_set_encrypt_key(const uint8_t *key, unsigned bits, AES_KEY *aeskey);
void aes_nohw_encrypt(const uint8_t *in, uint8_t *out, const AES_KEY *key);
void aes_nohw_ctr32_encrypt_blocks(const uint8_t *in, uint8_t *out, size_t blocks, const AES_KEY *key, const uint8_t ivec[16]);
void p256_point_mul(Limb r[3][4], const Limb scalar[4], const Limb p_x[4], const Limb p_y[4]);
void p256_point_mul_base(Limb r[3][4], const Limb scalar[4]);
void p256_mul_mont(Limb r[4], const Limb a[4], const Limb b[4]);
void p256_point_double(Limb r[3][4], const Limb a[3][4]);
void p256_point_add(Limb r[3][4], const Limb a[3][4], const Limb b[3][4]);

static uint64_t H = 1469598103934665603ULL;
static void hb(const void *p, size_t n) { const uint8_t *c = p; while (n--) H = (H ^ *c++) * 1099511628211ULL; }
static uint64_t rng = 0x9e3779b97f4a7c15ULL;
static uint64_t next(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return rng; }
static void rand_bytes(uint8_t *p, size_t n) { for (size_t i = 0; i < n; i++) p[i] = (uint8_t)next(); }

static void unhex(uint8_t *out, const char *hex) {
    for (size_t i = 0; hex[2 * i]; i++) { unsigned v; sscanf(hex + 2 * i, "%2x", &v); out[i] = (uint8_t)v; }
}
static void phex(const char *tag, const uint8_t *p, size_t n) {
    printf("%s ", tag);
    for (size_t i = 0; i < n; i++) printf("%02x", p[i]);
    printf("\n");
}
static void kat(const char *tag, const uint8_t *got, const char *want_hex, size_t n) {
    uint8_t want[64];
    unhex(want, want_hex);
    printf("%s %s\n", tag, memcmp(got, want, n) ? "BAD" : "ok");
    phex(tag, got, n);
}
static void clamp(uint8_t k[32]) { k[0] &= 248; k[31] &= 127; k[31] |= 64; }

int main(void) {
    uint8_t k[32], u[32], out[32];

    /* RFC 7748 5.2 */
    unhex(k, "a546e36bf0527c9d3b16154b82465edd62144c0ac1fc5a18506a2244ba449ac4");
    unhex(u, "e6db6867583030db3594c1a424b15f7c726624ec26b3353b10a903a6d0ab1c4c");
    clamp(k);
    x25519_scalar_mult_generic_masked(out, k, u);
    kat("x25519-1", out, "c3da55379de9c6908e94ea4df28d084f32eccf03491c71f754b4075577a28552", 32);
    unhex(k, "4b66e9d4d1b4673c5ad22691957d6af5c11b6421e0ea01d42ca4169e7918ba0d");
    unhex(u, "e5210f12786811d3f4b7959d0538ae2c31dbe7106fc03c3efc4cd549c715a493");
    clamp(k);
    x25519_scalar_mult_generic_masked(out, k, u);
    phex("x25519-2", out, 32);

    /* RFC 7748 5.2, iterated 1000 times */
    memset(k, 0, 32); k[0] = 9;
    memset(u, 0, 32); u[0] = 9;
    for (int i = 0; i < 1000; i++) {
        uint8_t kc[32];
        memcpy(kc, k, 32);
        clamp(kc);
        x25519_scalar_mult_generic_masked(out, kc, u);
        memcpy(u, k, 32);
        memcpy(k, out, 32);
    }
    kat("x25519-1000", k, "684cf59ba83309552800ef566f2f4d3c1c3887c49360e3875f2eb94d99532c51", 32);

    /* RFC 7748 6.1: Alice's public key */
    unhex(k, "77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a");
    clamp(k);
    x25519_public_from_private_generic_masked(out, k, 0);
    kat("x25519-pub", out, "8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a", 32);

    /* RFC 8439 2.5.2 */
    _Alignas(64) uint8_t st[512];
    uint8_t key[32], mac[16];
    unhex(key, "85d6be7857556d337f4452fe42d506a80103808afb0db2fd4abff6af4149f51b");
    const char *msg = "Cryptographic Forum Research Group";
    CRYPTO_poly1305_init(st, key);
    CRYPTO_poly1305_update(st, (const uint8_t *)msg, strlen(msg));
    CRYPTO_poly1305_finish(st, mac);
    kat("poly1305", mac, "a8061dc1305136c6c22b8baf0c0127a9", 16);

    /* FIPS-197 C.1 and C.3 */
    AES_KEY ak;
    uint8_t pt[16], ct[16];
    unhex(key, "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f");
    unhex(pt, "00112233445566778899aabbccddeeff");
    aes_nohw_set_encrypt_key(key, 128, &ak);
    aes_nohw_encrypt(pt, ct, &ak);
    kat("aes128", ct, "69c4e0d86a7b0430d8cdb78070b4c55a", 16);
    aes_nohw_set_encrypt_key(key, 256, &ak);
    aes_nohw_encrypt(pt, ct, &ak);
    kat("aes256", ct, "8ea2b7ca516745bfeafc49904b496089", 16);

    /* Stress: random X25519, Poly1305 over random lengths, AES-CTR. */
    for (int i = 0; i < 200; i++) {
        rand_bytes(k, 32); rand_bytes(u, 32);
        clamp(k);
        x25519_scalar_mult_generic_masked(out, k, u);
        hb(out, 32);
        x25519_public_from_private_generic_masked(out, k, 0);
        hb(out, 32);
    }
    printf("x25519-stress %016llx\n", (unsigned long long)H);
    static uint8_t buf[4096], enc[4096];
    rand_bytes(buf, sizeof buf);
    for (int i = 0; i < 100; i++) {
        size_t n = (size_t)(next() % sizeof buf);
        rand_bytes(key, 32);
        CRYPTO_poly1305_init(st, key);
        CRYPTO_poly1305_update(st, buf, n / 2);
        CRYPTO_poly1305_update(st, buf + n / 2, n - n / 2);
        CRYPTO_poly1305_finish(st, mac);
        hb(mac, 16);
    }
    printf("poly1305-stress %016llx\n", (unsigned long long)H);
    for (int i = 0; i < 20; i++) {
        uint8_t iv[16];
        rand_bytes(key, 32); rand_bytes(iv, 16);
        aes_nohw_set_encrypt_key(key, i & 1 ? 256 : 128, &ak);
        size_t blocks = (size_t)(next() % 256);
        aes_nohw_ctr32_encrypt_blocks(buf, enc, blocks, &ak, iv);
        hb(enc, blocks * 16);
    }
    printf("aes-ctr-stress %016llx\n", (unsigned long long)H);

    /* P-256: base and variable point multiplication, doubling, addition,
     * Montgomery multiplication on random limbs; the outputs are hashed. */
    Limb r[3][4], r2[3][4], r3[3][4], s[4], a4[4], b4[4];
    for (int i = 0; i < 40; i++) {
        for (int j = 0; j < 4; j++) s[j] = next();
        s[3] &= 0x7fffffffffffffffULL;
        p256_point_mul_base(r, s);
        hb(r, sizeof r);
        p256_point_double(r2, r);
        hb(r2, sizeof r2);
        p256_point_add(r3, r, r2);
        hb(r3, sizeof r3);
        p256_point_mul(r2, s, r[0], r[1]);
        hb(r2, sizeof r2);
        for (int j = 0; j < 4; j++) { a4[j] = next(); b4[j] = next(); }
        a4[3] &= 0x0fffffffffffffffULL; b4[3] &= 0x0fffffffffffffffULL;
        p256_mul_mont(a4, a4, b4);
        hb(a4, sizeof a4);
    }
    printf("p256-stress %016llx\n", (unsigned long long)H);
    return 0;
}
