/* C23 enums with a fixed underlying type (enum E : T, also a clang extension
 * before C23): ccc did not parse them (the Accelerate framework headers use
 * them). The enum is T in size, alignment, signedness and _Generic selection;
 * every constant has type T; a forward declaration (enum E : T;) carries T to
 * later uses; a bit-field of such an enum has T's signedness, not the unsigned
 * enum bit-field rule; and a colon followed by a width is still an unnamed
 * bit-field. Also sizeof of an enumeration constant of a plain enum that int
 * cannot hold (BIG = 1L << 40), which ccc gave as 4. */
#include <stdio.h>
#include <stdint.h>

enum Small : unsigned char { S0, S1, S200 = 200, S255 = 255 };
enum Neg : signed char { NM = -128, N0 = 0, NP = 127 };
enum Wide : unsigned long long { W0 = 0, WBIG = 1ULL << 40, WMAX = 0xffffffffffffffffULL };
enum Sh : short;
enum Sh : short { H1 = -2, H2 = 30000 };
typedef enum : uint16_t { T_A = 1, T_B = 65535 } T16;
enum U32 : unsigned { UA = 0x80000000u, UB = 1 };

struct Pk { enum Small a; enum Neg b; enum Sh c; T16 d; enum Small e : 3; enum Neg f : 4; enum U32 g : 2; };
struct Bf { enum Small : 3; enum Small x : 2; };

enum Plain { PA, PB, PC };
struct Pl { enum Plain p : 2; };
enum Big { B0, BIG = 1L << 40, BNEG = -5 };
enum Un { U0, UHI = 0x80000000u };

__attribute__((noinline)) static int classify(enum Small s) {
    switch (s) {
    case S0: return 10;
    case S1: return 11;
    case S200: return 12;
    default: return 13;
    }
}

static const unsigned long sz_global = sizeof(S1);

int main(void) {
    printf("sizes %zu %zu %zu %zu %zu %zu\n", sizeof(enum Small), sizeof(enum Neg), sizeof(enum Wide), sizeof(enum Sh), sizeof(T16), sizeof(enum U32));
    printf("aligns %zu %zu %zu\n", _Alignof(enum Small), _Alignof(enum Wide), _Alignof(enum Sh));
    printf("struct %zu %zu %zu\n", sizeof(struct Pk), sizeof(struct Pl), sizeof(struct Bf));
    enum Small s = S255;
    s++;
    enum Neg n = NM;
    n--;
    printf("wrap %d %d\n", (int)s, (int)n);
    volatile enum Wide w = WMAX;
    printf("wide %llu %llu %d %llu\n", (unsigned long long)w, (unsigned long long)WBIG, w > WBIG, (unsigned long long)(WMAX >> 60));
    enum Sh h = H1;
    printf("short %d %d %d\n", (int)h, h < 0, (int)H2);
    T16 t = T_B;
    t += 1;
    printf("u16 %d %d\n", (int)t, (int)T_B);
    printf("cmp %d %d\n", (enum U32)UA > (enum U32)UB, UA > 0);
    printf("generic %d %d %d\n", _Generic((enum Small)0, unsigned char: 1, default: 0),
           _Generic((enum Sh)0, short: 2, default: 0), _Generic((T16)0, unsigned short: 3, default: 0));
    struct Pk p = { S200, NM, H2, T_A, 7, -8, 3 };
    p.e += 2; p.f -= 1; p.g += 1;
    printf("fields %d %d %d %d %d %d %d\n", (int)p.a, (int)p.b, (int)p.c, (int)p.d, (int)p.e, (int)p.f, (int)p.g);
    struct Pl pl = { PC };
    pl.p += 2;
    printf("plain-bit-field %d\n", (int)pl.p);
    enum Small arr[4] = { S0, S1, S200, S255 };
    int sum = 0;
    for (int i = 0; i < 4; i++) sum = sum * 3 + classify(arr[i]);
    char buf[sizeof(S1) * 10];
    printf("switch %d %zu %zu\n", sum, sizeof arr, sizeof buf);
    printf("constant sizes %zu %zu %zu %lu\n", sizeof(S1), sizeof(WBIG), sizeof(H1), sz_global);
    printf("plain %zu %zu %zu %zu %zu %zu\n", sizeof(B0), sizeof(BIG), sizeof(BNEG), sizeof(U0), sizeof(UHI), sizeof(enum Big));
    return 0;
}
