/* _Generic sees the enum type of every kind of enum-typed expression, and an
 * enum is compatible with its underlying type: unsigned int when no value is
 * negative, int otherwise. Enum constants themselves have type int. */
#include <stdio.h>

enum pos { P0, P1, P2 };
enum neg { NM = -1, N0 };
typedef enum pos pos_t;
enum big { BIG = 0x80000000u };
enum wide { WIDE = 0x100000000 };
enum swide { SWIDE_NEG = -1, SWIDE = 0x100000000 };
enum __attribute__((packed)) small { SMALL = 200 };

#define KIND(x) _Generic((x), int: "int", unsigned: "unsigned", long: "long", \
                         unsigned long: "ulong", unsigned char: "uchar", \
                         default: "other")
#define PKIND(p) _Generic((p), int *: "int*", unsigned *: "unsigned*", \
                          default: "other*")
#define IS_POS(x) _Generic((x), enum pos: "pos", default: "not-pos")

struct holder { enum pos p; enum neg n; pos_t t; };

enum pos g_pos = P2;
static enum neg g_neg = NM;

static enum pos ret_pos(void) { return P1; }
static enum neg ret_neg(void) { return N0; }

static const char *param_kind(enum pos p) { return KIND(p); }
static const char *ptr_param_kind(enum pos *p) { return PKIND(p); }

int main(void) {
    enum pos lp = P1;
    enum neg ln = N0;
    pos_t tp = P0;
    const enum pos cp = P2;
    enum pos arr[2] = { P0, P1 };
    struct holder h = { P1, NM, P2 };
    struct holder *hp = &h;
    enum pos *pp = &lp;
    int i = 1;

    printf("local    %s %s %s %s\n", KIND(lp), KIND(ln), KIND(tp), KIND(cp));
    printf("global   %s %s\n", KIND(g_pos), KIND(g_neg));
    printf("member   %s %s %s %s\n", KIND(h.p), KIND(h.n), KIND(h.t), KIND(hp->p));
    printf("array    %s\n", KIND(arr[1]));
    printf("deref    %s\n", KIND(*pp));
    printf("call     %s %s\n", KIND(ret_pos()), KIND(ret_neg()));
    printf("cast     %s %s\n", KIND((enum pos)i), KIND((enum neg)i));
    printf("param    %s %s\n", param_kind(P1), ptr_param_kind(&lp));
    printf("pointer  %s %s\n", PKIND(&lp), PKIND(&ln));
    printf("const    %s %s\n", KIND(P1), KIND(NM));
    printf("arith    %s %s\n", KIND(lp + 0), KIND(+lp));
    printf("assign   %s\n", KIND(lp = P2));
    printf("comma    %s\n", KIND((i, lp)));
    printf("cond     %s\n", KIND(i ? lp : P0));
    printf("tag      %s %s %s %s\n", IS_POS(lp), IS_POS(tp), IS_POS(i), IS_POS(P1));

    enum big b = BIG;
    enum wide w = WIDE;
    enum swide sw = SWIDE;
    enum small sm = SMALL;
    printf("range    %s %s %s %s\n", KIND(b), KIND(w), KIND(sw), KIND(sm));
    printf("sizes    %d %d %d %d\n", (int)sizeof b, (int)sizeof w, (int)sizeof sw,
           (int)sizeof sm);
    printf("tsizes   %d %d %d %d\n", (int)sizeof(enum wide), (int)_Alignof(enum wide),
           (int)sizeof(enum swide), (int)sizeof(struct { char c; enum wide w; }));
    w = (enum wide)(w + i);
    sw = SWIDE_NEG;
    printf("wide     %llx %lld %d\n", (unsigned long long)w, (long long)sw, w > BIG);

    const enum pos *cpp = &lp;
    __typeof__(lp) tl = P1;
    printf("qual     %s %s\n", PKIND(cpp), KIND(tl));
    printf("values   %d %d %d %d\n", lp, ln, h.n, (int)sizeof(enum pos));
    return 0;
}
