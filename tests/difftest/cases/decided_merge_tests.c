/* Tests at a merge that an incoming edge already decides (deflate_fast's
 * lookahead >= MIN_MATCH after lookahead >= MIN_LOOKAHEAD, and
 * hash_head != NIL after hash_head = NIL), with the merged value still read
 * on the other path; a compare and subtract of the same constant fused into
 * subs, also where the difference is live at the branch target; adjacent
 * field loads paired, also when the first one overwrites the base. */
#include <stdio.h>
#include <stdint.h>

struct state {
    unsigned lookahead, strstart, match_start, match_length, filled;
    unsigned short head[64];
};

__attribute__((noinline)) static void fill(struct state *s) {
    s->lookahead += 300;
    s->filled++;
}

__attribute__((noinline)) static unsigned step(struct state *s, int flush) {
    unsigned total = 0;
    for (int round = 0; round < 40; round++) {
        if (s->lookahead < 262) {
            fill(s);
            if (s->lookahead < 262 && flush == 0)
                return total;
            if (s->lookahead == 0)
                break;
        }
        unsigned head = 0;
        if (s->lookahead >= 3) {
            unsigned h = (s->strstart * 2654435761u) >> 26;
            head = s->head[h];
            s->head[h] = (unsigned short)s->strstart;
        }
        if (head != 0 && s->strstart - head < 200)
            s->match_length = (s->strstart - head) % 9;
        if (s->match_length >= 3) {
            unsigned lc = s->match_length - 3;
            total += lc * 7 + s->strstart - s->match_start;
            s->lookahead -= s->match_length;
            s->strstart += s->match_length;
            s->match_length = 0;
        } else {
            total += s->strstart & 15;
            s->lookahead--;
            s->strstart++;
        }
        s->match_start = s->strstart / 3;
    }
    return total;
}

/* The difference is read where the branch goes when it is small. */
__attribute__((noinline)) static int sub_live_at_target(int x, int *out) {
    int d = x;
    if (x >= 5) {
        d = x - 5;
        *out = d * 3;
        return d;
    }
    *out = d;
    return -d;
}

struct node { struct node *next; long a, b; };

__attribute__((noinline)) static long walk(struct node *n) {
    long s = 0;
    while (n) {
        s = s * 3 + n->a - n->b;
        n = n->next;
    }
    return s;
}

int main(void) {
    struct state s = {0};
    s.strstart = 1;
    unsigned t = step(&s, 0) ^ step(&s, 1) * 3;
    printf("step %u %u %u %u\n", t, s.strstart, s.lookahead, s.filled);
    struct state z = {0};
    printf("flush %u %u\n", step(&z, 0), z.filled);
    int o = 0, r = 0;
    for (int x = -4; x < 20; x += 3) {
        r = r * 5 + sub_live_at_target(x, &o);
        r += o;
    }
    printf("sub %d\n", r);
    struct node ns[5];
    for (int i = 0; i < 5; i++)
        ns[i] = (struct node){ i < 4 ? &ns[i + 1] : 0, i * 7 + 1, i - 3 };
    printf("walk %ld\n", walk(ns));
    return 0;
}
