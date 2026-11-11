/* Reduced from a random control-flow program (computed gotos, switches,
 * side-effecting conditions). Two bugs met in it:
 * - switch (0) with case 0x8000000000000000ULL: the case converts to int
 *   and is case 0, but a constant controlling expression was typed I64;
 * - (uint64_t)i / 3 is a 128-bit multiply-high, whose widening the arm
 *   backend emits as nothing. The 64-bit operand, read only by that
 *   widening, skipped its stack slot as if it would still be in x0 there,
 *   but it is read at the umulh after the multiplier's load: mov x0, #0
 *   took its place and s8 came out 0 instead of -1. */
#include <stdint.h>
#include <stdio.h>

static uint64_t tr;
__attribute__((noinline)) static uint64_t T(uint64_t v) { tr = tr * 0x100000001b3ULL ^ v; return v; }

__attribute__((noinline)) uint64_t f0(uint64_t a, uint64_t b, unsigned n) {
    uint64_t h = 0;
    uint8_t x8 = (uint8_t)b;
    uint16_t y16 = (uint16_t)(1);
    int8_t s8 = (int8_t)n;
    int16_t s16 = (int16_t)n;
    uint32_t z32 = (uint32_t)b;
    uint64_t w64 = (uint64_t)a;
    int i = (int)(a & 0xff) - 100;
    uint64_t q0 = (uint64_t)(0);
    uint8_t q1 = (uint8_t)(0);
    uint32_t q2 = (uint32_t)(0);
    uint16_t q3 = (uint16_t)(0);
    uint8_t q4 = (uint8_t)(0);
    uint8_t q5 = (uint8_t)(0);
    uint8_t q6 = (uint8_t)(0);
    uint32_t q7 = (uint32_t)(0);
    int8_t q8 = (int8_t)(0);
    uint64_t q9 = (uint64_t)(0);
    uint8_t q10 = (uint8_t)(0);
    uint64_t q11 = (uint64_t)(0);
    uint64_t q12 = (uint64_t)(0);
    uint8_t q13 = (uint8_t)(0);
    uint32_t q14 = (uint32_t)(0);
    uint32_t q15 = (uint32_t)(0);
    uint32_t q16 = (uint32_t)(0);
    uint8_t q17 = (uint8_t)(0);
    uint32_t q18 = (uint32_t)(0);
    uint16_t q19 = (uint16_t)(0);
    uint32_t q20 = (uint32_t)(0);
    uint32_t q21 = (uint32_t)(0);
    uint16_t q22 = (uint16_t)(0);
    uint16_t q23 = (uint16_t)(0);
    uint16_t q24 = (uint16_t)(0);
    uint8_t q25 = (uint8_t)(0);
    uint64_t q26 = (uint64_t)(0);
    uint8_t q27 = (uint8_t)(0);
    uint8_t q28 = (uint8_t)(0);
    uint32_t q29 = (uint32_t)(0);
    uint64_t q30 = (uint64_t)(0);
    int8_t q31 = (int8_t)(0);
    int8_t q32 = (int8_t)(a + 32);
    uint32_t q33 = (uint32_t)(0);
    uint64_t q34 = (uint64_t)(0);
    uint16_t q35 = (uint16_t)(0);
    uint8_t q36 = (uint8_t)(0);
    uint32_t q37 = (uint32_t)(0);
    uint16_t q38 = (uint16_t)(0);
    uint16_t q39 = (uint16_t)(0);
    uint32_t q40 = (uint32_t)(0);
    int8_t q41 = (int8_t)(0);
    uint32_t q42 = (uint32_t)(0);
    uint8_t q43 = (uint8_t)(0);
    uint16_t q44 = (uint16_t)(0);
    uint8_t q45 = (uint8_t)(0);
    int8_t q46 = (int8_t)(0);
    uint64_t q47 = (uint64_t)(0);
    uint32_t q48 = (uint32_t)(0);
    uint8_t q49 = (uint8_t)(0);
    uint16_t q50 = (uint16_t)(0);
    uint16_t q51 = (uint16_t)(0);
    uint16_t q52 = (uint16_t)(0);
    uint16_t q53 = (uint16_t)(0);
    for (unsigned l0_1 = 0; l0_1 < 2; l0_1++) {
        switch (0) {
        }
        if (((--q11) & 4) && (1)) {
        }
    }
    switch (0) {
        if (0) {
        }
    }
    { static void *const t0_8[] = { &&C0_8_0, &&C0_8_1, &&C0_8_2 };
    goto *t0_8[(uint64_t)(0) % 3];
    C0_8_0:
        { unsigned g0_9 = 0;
        B0_9: h = T(0);
            h = T(h + (((int64_t)y16 < (int64_t)((uint64_t)z32 + (uint64_t)(((int8_t)n > (int8_t)0) ? 2 : w64))) ? 7 : ((--q26) & 4) ? ((uint64_t)(0) ^ (uint64_t)i) : (0)));
        if (++g0_9 < 2 && ((((uint64_t)1000 / ((uint64_t)x8 | 1))) & 1)) goto B0_9; }
    C0_8_1:
        switch (0) {
        case 0x8000000000000000ULL:
            switch ((uint64_t)((uint64_t)((uint64_t)((uint64_t)y16 + (uint64_t)s16) / (1)) >> (0))) {
            case 0x8000000000000000ULL:
                s16 &= (((uint64_t)(q31 += (q30 -= 2, (uint64_t)q30), (uint64_t)q31) & (uint64_t)(0)));
            }
            h = T(h + (((++q32) & 4) ? ((uint64_t)((uint64_t)(0) + (uint64_t)(q33 += i, (uint64_t)q33)) | (uint64_t)(0)) : (1) ? ((uint64_t)(0) & (uint64_t)(((--q35) & 1) ? (q34 |= s16, (uint64_t)q34) : (0))) : 1));
            if (((--q36) & 2) && (0)) {
            }
        }
    C0_8_2:
        s8 = (((uint64_t)((uint64_t)x8 & (uint64_t)(1)) + (uint64_t)((uint64_t)(uint64_t)(uint16_t)0x7fffffffffffffffULL % ((uint64_t)((uint64_t)i / ((uint64_t)2 | 1)) | 1))));
        { unsigned g0_10 = 0;
            if (0) {
            }
        }
    C0_8_e: h = T(h + 11); }
    return T(h ^ x8 ^ y16 ^ (uint64_t)s8 ^ (uint64_t)s16 ^ z32 ^ w64 ^ (uint64_t)i ^ (uint64_t)q0 ^ (uint64_t)q1 ^ (uint64_t)q2 ^ (uint64_t)q3 ^ (uint64_t)q4 ^ (uint64_t)q5 ^ (uint64_t)q6 ^ (uint64_t)q7 ^ (uint64_t)q8 ^ (uint64_t)q9 ^ (uint64_t)q10 ^ (uint64_t)q11 ^ (uint64_t)q12 ^ (uint64_t)q13 ^ (uint64_t)q14 ^ (uint64_t)q15 ^ (uint64_t)q16 ^ (uint64_t)q17 ^ (uint64_t)q18 ^ (uint64_t)q19 ^ (uint64_t)q20 ^ (uint64_t)q21 ^ (uint64_t)q22 ^ (uint64_t)q23 ^ (uint64_t)q24 ^ (uint64_t)q25 ^ (uint64_t)q26 ^ (uint64_t)q27 ^ (uint64_t)q28 ^ (uint64_t)q29 ^ (uint64_t)q30 ^ (uint64_t)q31 ^ (uint64_t)q32 ^ (uint64_t)q33 ^ (uint64_t)q34 ^ (uint64_t)q35 ^ (uint64_t)q36 ^ (uint64_t)q37 ^ (uint64_t)q38 ^ (uint64_t)q39 ^ (uint64_t)q40 ^ (uint64_t)q41 ^ (uint64_t)q42 ^ (uint64_t)q43 ^ (uint64_t)q44 ^ (uint64_t)q45 ^ (uint64_t)q46 ^ (uint64_t)q47 ^ (uint64_t)q48 ^ (uint64_t)q49 ^ (uint64_t)q50 ^ (uint64_t)q51 ^ (uint64_t)q52 ^ (uint64_t)q53);
}




static const uint64_t vals[] = { 0ULL, 1ULL, 2ULL, 3ULL, 5ULL, 7ULL, 0xffULL, 0x80ULL, 0x7fULL, 0xffffULL, 0x8000ULL, 0x7fffffffULL, 0x80000000ULL, 0xffffffffULL, 0x100000000ULL, 0xfffffffffffffffeULL, 0x8000000000000000ULL, 0x123456789abcdefULL, 0xdeadbeefcafebabeULL, 42ULL, 100ULL, 0xfffffff0ULL };
int main(void) {
    unsigned nv = sizeof vals / sizeof vals[0];
    for (unsigned i = 0; i < nv; i++) for (unsigned j = 0; j < nv; j += 3) for (unsigned n = 0; n < 4; n++) {
        tr = 0; { uint64_t v = f0(vals[i], vals[j], n * 7);
          printf("0 %u %u %u %llx %llx\n", i, j, n, (unsigned long long)v, (unsigned long long)tr); }
    }
    return 0;
}


