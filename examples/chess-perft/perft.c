/*
 * perft.c -- chess move-generation benchmark and self-check.
 *
 * Counts the leaf nodes of the legal move tree ("perft") for six standard
 * test positions and compares them with the published counts, solves two
 * small mate puzzles, and runs fixed-depth alpha-beta searches. The work is
 * integer- and branch-heavy (64-bit bitboards, bit scans, deep recursion,
 * struct copies), complementing the float-heavy ocean demo.
 *
 * Perft counts are known exactly, so a miscompiled shift, bit scan, or
 * branch shows up as a wrong count without needing a reference compiler.
 * Exit status is 0 only if every check passes.
 *
 *   ./perft          standard suite (about 16M nodes)
 *   ./perft --deep   adds perft(6) of the start position and perft(5) of Kiwipete
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef COMPILER_NAME
#define COMPILER_NAME "unknown compiler"
#endif

typedef uint64_t u64;

enum { WHITE, BLACK };
enum { PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING };
enum { CASTLE_WK = 1, CASTLE_WQ = 2, CASTLE_BK = 4, CASTLE_BQ = 8 };
enum { FLAG_NONE, FLAG_DOUBLE, FLAG_EP, FLAG_CASTLE };

#define EMPTY (-1)
#define MAX_MOVES 256
#define MATE 100000
#define INF 1000000

/* Squares are a1 = 0 ... h8 = 63. */
typedef struct {
    u64 pieces[2][6];
    u64 occ[2];
    u64 all;
    int8_t board[64];   /* color * 6 + type, or EMPTY */
    int side;
    int ep;             /* en-passant target square, or -1 */
    int castle;
} Pos;

/* from | to << 6 | promotion piece << 12 | flag << 16 */
typedef uint32_t Move;
#define MOVE(from, to, promo, flag) ((Move)((from) | ((to) << 6) | ((promo) << 12) | ((flag) << 16)))
#define FROM(m)  ((int)((m) & 63))
#define TO(m)    ((int)(((m) >> 6) & 63))
#define PROMO(m) ((int)(((m) >> 12) & 7))
#define FLAG(m)  ((int)(((m) >> 16) & 3))

typedef struct {
    Move m[MAX_MOVES];
    int n;
} MoveList;

/* ------------------------------------------------------------------------ */
/* Attack tables                                                            */
/* ------------------------------------------------------------------------ */

static u64 knight_att[64], king_att[64], pawn_att[2][64];
static u64 rays[8][64];
static int castle_mask[64];

/* Ray directions: N, NE, E, SE, S, SW, W, NW. */
static const int ray_df[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
static const int ray_dr[8] = { 1, 1, 0, -1, -1, -1, 0, 1 };

static u64 bit(int sq) { return 1ULL << sq; }
static int lsb(u64 b) { return __builtin_ctzll(b); }
static int msb(u64 b) { return 63 - __builtin_clzll(b); }
static int popcount(u64 b) { return __builtin_popcountll(b); }
static int on_board(int f, int r) { return f >= 0 && f < 8 && r >= 0 && r < 8; }

/* N, NE, E, and NW rays run toward higher-numbered squares. */
static int ray_up(int d) { return d <= 2 || d == 7; }

static u64 step_targets(int sq, const int deltas[][2], int n)
{
    u64 b = 0;
    for (int i = 0; i < n; i++) {
        int f = (sq & 7) + deltas[i][0], r = (sq >> 3) + deltas[i][1];
        if (on_board(f, r))
            b |= bit(r * 8 + f);
    }
    return b;
}

static void init_tables(void)
{
    static const int knight_d[8][2] = {
        { 1, 2 }, { 2, 1 }, { 2, -1 }, { 1, -2 }, { -1, -2 }, { -2, -1 }, { -2, 1 }, { -1, 2 },
    };
    static const int king_d[8][2] = {
        { 0, 1 }, { 1, 1 }, { 1, 0 }, { 1, -1 }, { 0, -1 }, { -1, -1 }, { -1, 0 }, { -1, 1 },
    };
    static const int wpawn_d[2][2] = { { -1, 1 }, { 1, 1 } };
    static const int bpawn_d[2][2] = { { -1, -1 }, { 1, -1 } };

    for (int sq = 0; sq < 64; sq++) {
        knight_att[sq] = step_targets(sq, knight_d, 8);
        king_att[sq] = step_targets(sq, king_d, 8);
        pawn_att[WHITE][sq] = step_targets(sq, wpawn_d, 2);
        pawn_att[BLACK][sq] = step_targets(sq, bpawn_d, 2);
        for (int d = 0; d < 8; d++) {
            u64 b = 0;
            int f = (sq & 7) + ray_df[d], r = (sq >> 3) + ray_dr[d];
            for (; on_board(f, r); f += ray_df[d], r += ray_dr[d])
                b |= bit(r * 8 + f);
            rays[d][sq] = b;
        }
        castle_mask[sq] = CASTLE_WK | CASTLE_WQ | CASTLE_BK | CASTLE_BQ;
    }
    /* Moving from or capturing on these squares loses castling rights. */
    castle_mask[0] &= ~CASTLE_WQ;
    castle_mask[4] &= ~(CASTLE_WK | CASTLE_WQ);
    castle_mask[7] &= ~CASTLE_WK;
    castle_mask[56] &= ~CASTLE_BQ;
    castle_mask[60] &= ~(CASTLE_BK | CASTLE_BQ);
    castle_mask[63] &= ~CASTLE_BK;
}

/* Sliding attacks along one ray, stopping at (and including) the first blocker. */
static u64 ray_att(int d, int sq, u64 occ)
{
    u64 a = rays[d][sq], blockers = a & occ;
    if (blockers)
        a ^= rays[d][ray_up(d) ? lsb(blockers) : msb(blockers)];
    return a;
}

static u64 bishop_att(int sq, u64 occ)
{
    return ray_att(1, sq, occ) | ray_att(3, sq, occ) | ray_att(5, sq, occ) | ray_att(7, sq, occ);
}

static u64 rook_att(int sq, u64 occ)
{
    return ray_att(0, sq, occ) | ray_att(2, sq, occ) | ray_att(4, sq, occ) | ray_att(6, sq, occ);
}

/* ------------------------------------------------------------------------ */
/* Position                                                                 */
/* ------------------------------------------------------------------------ */

static void put(Pos *p, int color, int type, int sq)
{
    u64 b = bit(sq);
    p->pieces[color][type] |= b;
    p->occ[color] |= b;
    p->all |= b;
    p->board[sq] = (int8_t)(color * 6 + type);
}

static void take(Pos *p, int sq)
{
    int pc = p->board[sq];
    u64 keep = ~bit(sq);
    p->pieces[pc / 6][pc % 6] &= keep;
    p->occ[pc / 6] &= keep;
    p->all &= keep;
    p->board[sq] = EMPTY;
}

static int attacked(const Pos *p, int sq, int by)
{
    const u64 *pc = p->pieces[by];
    return (pawn_att[by ^ 1][sq] & pc[PAWN])
        || (knight_att[sq] & pc[KNIGHT])
        || (king_att[sq] & pc[KING])
        || (bishop_att(sq, p->all) & (pc[BISHOP] | pc[QUEEN]))
        || (rook_att(sq, p->all) & (pc[ROOK] | pc[QUEEN]));
}

static int in_check(const Pos *p, int color)
{
    return attacked(p, lsb(p->pieces[color][KING]), color ^ 1);
}

static int parse_fen(Pos *p, const char *fen)
{
    static const char piece_chars[] = "PNBRQKpnbrqk";
    memset(p, 0, sizeof *p);
    for (int i = 0; i < 64; i++)
        p->board[i] = EMPTY;

    int sq = 56;
    for (; *fen && *fen != ' '; fen++) {
        const char *c = strchr(piece_chars, *fen);
        if (*fen == '/')
            sq -= 16;
        else if (*fen >= '1' && *fen <= '8')
            sq += *fen - '0';
        else if (c && sq < 64)
            put(p, (int)(c - piece_chars) / 6, (int)(c - piece_chars) % 6, sq++);
        else
            return 0;
    }
    if (*fen++ != ' ')
        return 0;
    p->side = *fen++ == 'b' ? BLACK : WHITE;
    if (*fen++ != ' ')
        return 0;
    for (; *fen && *fen != ' '; fen++) {
        switch (*fen) {
        case 'K': p->castle |= CASTLE_WK; break;
        case 'Q': p->castle |= CASTLE_WQ; break;
        case 'k': p->castle |= CASTLE_BK; break;
        case 'q': p->castle |= CASTLE_BQ; break;
        }
    }
    p->ep = -1;
    if (*fen == ' ' && fen[1] >= 'a' && fen[1] <= 'h' && fen[2] >= '1' && fen[2] <= '8')
        p->ep = (fen[2] - '1') * 8 + (fen[1] - 'a');
    return 1;
}

static void move_str(Move m, char *out)
{
    out[0] = (char)('a' + (FROM(m) & 7));
    out[1] = (char)('1' + (FROM(m) >> 3));
    out[2] = (char)('a' + (TO(m) & 7));
    out[3] = (char)('1' + (TO(m) >> 3));
    out[4] = PROMO(m) ? "?nbrq"[PROMO(m)] : '\0';
    out[5] = '\0';
}

/* ------------------------------------------------------------------------ */
/* Move generation                                                          */
/* ------------------------------------------------------------------------ */

static void add_pawn_move(MoveList *l, int from, int to, int promo_rank)
{
    if ((to >> 3) == promo_rank) {
        for (int t = QUEEN; t >= KNIGHT; t--)
            l->m[l->n++] = MOVE(from, to, t, FLAG_NONE);
    } else {
        l->m[l->n++] = MOVE(from, to, 0, FLAG_NONE);
    }
}

static void add_targets(MoveList *l, int from, u64 targets)
{
    for (; targets; targets &= targets - 1)
        l->m[l->n++] = MOVE(from, lsb(targets), 0, FLAG_NONE);
}

/* Pseudo-legal moves; the caller rejects those that leave the king in check. */
static void generate(const Pos *p, MoveList *l)
{
    int us = p->side, them = us ^ 1;
    u64 own = p->occ[us], empty = ~p->all;
    int fwd = us == WHITE ? 8 : -8;
    int start_rank = us == WHITE ? 1 : 6, promo_rank = us == WHITE ? 7 : 0;

    l->n = 0;
    for (u64 b = p->pieces[us][PAWN]; b; b &= b - 1) {
        int from = lsb(b), to = from + fwd;
        if (empty & bit(to)) {
            add_pawn_move(l, from, to, promo_rank);
            if ((from >> 3) == start_rank && (empty & bit(to + fwd)))
                l->m[l->n++] = MOVE(from, to + fwd, 0, FLAG_DOUBLE);
        }
        for (u64 c = pawn_att[us][from] & p->occ[them]; c; c &= c - 1)
            add_pawn_move(l, from, lsb(c), promo_rank);
        if (p->ep >= 0 && (pawn_att[us][from] & bit(p->ep)))
            l->m[l->n++] = MOVE(from, p->ep, 0, FLAG_EP);
    }
    for (u64 b = p->pieces[us][KNIGHT]; b; b &= b - 1)
        add_targets(l, lsb(b), knight_att[lsb(b)] & ~own);
    for (u64 b = p->pieces[us][BISHOP]; b; b &= b - 1)
        add_targets(l, lsb(b), bishop_att(lsb(b), p->all) & ~own);
    for (u64 b = p->pieces[us][ROOK]; b; b &= b - 1)
        add_targets(l, lsb(b), rook_att(lsb(b), p->all) & ~own);
    for (u64 b = p->pieces[us][QUEEN]; b; b &= b - 1)
        add_targets(l, lsb(b), (bishop_att(lsb(b), p->all) | rook_att(lsb(b), p->all)) & ~own);
    int k = lsb(p->pieces[us][KING]);
    add_targets(l, k, king_att[k] & ~own);

    /* Castling: path empty, and the king neither starts, passes, nor lands in check. */
    int base = us == WHITE ? 0 : 56;
    int ks = us == WHITE ? CASTLE_WK : CASTLE_BK, qs = us == WHITE ? CASTLE_WQ : CASTLE_BQ;
    if ((p->castle & ks) && !(p->all & (bit(base + 5) | bit(base + 6)))
        && !attacked(p, base + 4, them) && !attacked(p, base + 5, them) && !attacked(p, base + 6, them))
        l->m[l->n++] = MOVE(base + 4, base + 6, 0, FLAG_CASTLE);
    if ((p->castle & qs) && !(p->all & (bit(base + 1) | bit(base + 2) | bit(base + 3)))
        && !attacked(p, base + 4, them) && !attacked(p, base + 3, them) && !attacked(p, base + 2, them))
        l->m[l->n++] = MOVE(base + 4, base + 2, 0, FLAG_CASTLE);
}

static void make_move(Pos *p, Move m)
{
    int from = FROM(m), to = TO(m), promo = PROMO(m), flag = FLAG(m);
    int us = p->side;
    int type = p->board[from] % 6;

    if (flag == FLAG_EP)
        take(p, to + (us == WHITE ? -8 : 8));
    else if (p->board[to] != EMPTY)
        take(p, to);
    take(p, from);
    put(p, us, promo ? promo : type, to);
    if (flag == FLAG_CASTLE) {
        int kingside = (to & 7) == 6;
        int rook_from = (to & 56) + (kingside ? 7 : 0);
        int rook_to = (to & 56) + (kingside ? 5 : 3);
        take(p, rook_from);
        put(p, us, ROOK, rook_to);
    }
    p->castle &= castle_mask[from] & castle_mask[to];
    p->ep = flag == FLAG_DOUBLE ? (from + to) / 2 : -1;
    p->side = us ^ 1;
}

/* ------------------------------------------------------------------------ */
/* Perft and search                                                         */
/* ------------------------------------------------------------------------ */

static u64 perft(const Pos *p, int depth)
{
    MoveList l;
    u64 n = 0;
    generate(p, &l);
    for (int i = 0; i < l.n; i++) {
        Pos next = *p;
        make_move(&next, l.m[i]);
        if (in_check(&next, p->side))
            continue;
        n += depth == 1 ? 1 : perft(&next, depth - 1);
    }
    return n;
}

static const int piece_value[6] = { 100, 320, 330, 500, 900, 0 };
static u64 search_nodes;

static int evaluate(const Pos *p)
{
    int s = 0;
    for (int t = PAWN; t < KING; t++)
        s += piece_value[t] * (popcount(p->pieces[WHITE][t]) - popcount(p->pieces[BLACK][t]));
    return p->side == WHITE ? s : -s;
}

/* Fail-hard negamax alpha-beta on material; mates score MATE - ply. */
static int search(const Pos *p, int depth, int ply, int alpha, int beta, Move *best)
{
    search_nodes++;
    if (depth == 0)
        return evaluate(p);

    MoveList l;
    int legal = 0;
    generate(p, &l);
    for (int i = 0; i < l.n; i++) {
        Pos next = *p;
        make_move(&next, l.m[i]);
        if (in_check(&next, p->side))
            continue;
        legal++;
        int score = -search(&next, depth - 1, ply + 1, -beta, -alpha, NULL);
        if (score > alpha) {
            alpha = score;
            if (best)
                *best = l.m[i];
            if (alpha >= beta)
                break;
        }
    }
    if (!legal)
        return in_check(p, p->side) ? -MATE + ply : 0;
    return alpha;
}

/* ------------------------------------------------------------------------ */
/* Test suite                                                               */
/* ------------------------------------------------------------------------ */

static double now_seconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/* Published counts: https://www.chessprogramming.org/Perft_Results */
static const struct {
    const char *name, *fen;
    int depth, deep_depth;
    u64 counts[7];  /* counts[d] for d = 1..6 */
} perft_tests[] = {
    { "start", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 5, 6,
      { 0, 20, 400, 8902, 197281, 4865609, 119060324 } },
    { "kiwipete", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 4, 5,
      { 0, 48, 2039, 97862, 4085603, 193690690, 0 } },
    { "position 3", "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 5, 5,
      { 0, 14, 191, 2812, 43238, 674624, 11030083 } },
    { "position 4", "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 4, 4,
      { 0, 6, 264, 9467, 422333, 15833292, 706045033 } },
    { "position 5", "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 4, 4,
      { 0, 44, 1486, 62379, 2103487, 89941194, 0 } },
    { "position 6", "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10", 4, 4,
      { 0, 46, 2079, 89890, 3894594, 164075551, 0 } },
};

static const struct {
    const char *name, *fen;
    int mate_in;
    const char *move;   /* expected first move when it is unique, else NULL */
} mate_tests[] = {
    { "back rank", "6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1", 1, "a1a8" },
    { "king and rook", "k7/8/2K5/8/8/8/8/1R6 w - - 0 1", 2, NULL },
};

static const struct {
    const char *name, *fen;
    int depth;
} search_tests[] = {
    { "start", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 5 },
    { "kiwipete", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 4 },
};

int main(int argc, char **argv)
{
    int deep = argc > 1 && strcmp(argv[1], "--deep") == 0;
    if (argc > 1 && !deep) {
        fprintf(stderr, "usage: perft [--deep]\n");
        return 2;
    }

    init_tables();
    printf("compiler: %s\n", COMPILER_NAME);

    int failures = 0;
    u64 total_nodes = 0;
    double total_time = 0;
    for (size_t i = 0; i < sizeof perft_tests / sizeof perft_tests[0]; i++) {
        Pos p;
        if (!parse_fen(&p, perft_tests[i].fen)) {
            printf("perft %-10s bad FEN\n", perft_tests[i].name);
            failures++;
            continue;
        }
        int depth = deep ? perft_tests[i].deep_depth : perft_tests[i].depth;
        double t0 = now_seconds();
        u64 n = perft(&p, depth);
        double dt = now_seconds() - t0;
        u64 want = perft_tests[i].counts[depth];
        int ok = n == want;
        failures += !ok;
        total_nodes += n;
        total_time += dt;
        printf("perft  %-13s depth %d: %10llu %s (expected %llu)  %.2f s\n", perft_tests[i].name, depth,
               (unsigned long long)n, ok ? "PASS" : "FAIL", (unsigned long long)want, dt);
    }

    for (size_t i = 0; i < sizeof mate_tests / sizeof mate_tests[0]; i++) {
        Pos p;
        parse_fen(&p, mate_tests[i].fen);
        int found = 0;
        Move best = 0;
        for (int depth = 1; depth <= 2 * mate_tests[i].mate_in && !found; depth++) {
            int score = search(&p, depth, 0, -INF, INF, &best);
            if (score >= MATE - 2 * mate_tests[i].mate_in)
                found = (MATE - score + 1) / 2;
        }
        char mv[6];
        move_str(best, mv);
        int ok = found == mate_tests[i].mate_in && (!mate_tests[i].move || strcmp(mv, mate_tests[i].move) == 0);
        failures += !ok;
        printf("mate   %-13s mate in %d: %s %s\n", mate_tests[i].name, found, mv, ok ? "PASS" : "FAIL");
    }

    for (size_t i = 0; i < sizeof search_tests / sizeof search_tests[0]; i++) {
        Pos p;
        Move best = 0;
        char mv[6];
        parse_fen(&p, search_tests[i].fen);
        search_nodes = 0;
        double t0 = now_seconds();
        int score = search(&p, search_tests[i].depth, 0, -INF, INF, &best);
        double dt = now_seconds() - t0;
        move_str(best, mv);
        total_time += dt;
        printf("search %-13s depth %d: best %s score %d nodes %llu  %.2f s\n", search_tests[i].name,
               search_tests[i].depth, mv, score, (unsigned long long)search_nodes, dt);
    }

    printf("speed: %.1f M nodes/s\n", (double)total_nodes / total_time * 1e-6);
    printf("total time: %.2f s\n", total_time);
    printf("%s\n", failures ? "SOME CHECKS FAILED" : "all checks passed");
    return failures ? 1 : 0;
}
