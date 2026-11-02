/* Small helpers on call-graph cycles, called from functions on none: a
 * self-recursive tree free (inlined once into each caller, its own call
 * staying a call), and a value-to-text conversion on a mutual cycle with a
 * formatter, whose fast path returns a cached string. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct node { struct node *kid[2]; int v; };

static int freed;

static void tree_free(struct node *n) {
    if (!n)
        return;
    tree_free(n->kid[0]);
    tree_free(n->kid[1]);
    freed += n->v;
    free(n);
}

static struct node *build(int depth, int v) {
    struct node *n = malloc(sizeof *n);
    n->v = v;
    n->kid[0] = depth ? build(depth - 1, v * 2) : 0;
    n->kid[1] = depth ? build(depth - 1, v * 2 + 1) : 0;
    return n;
}

struct val { int kind; long i; char text[32]; int has_text; struct val *inner; };

static const char *value_text(struct val *v);

static void format_val(struct val *v) {
    if (v->kind == 2 && v->inner)
        snprintf(v->text, sizeof v->text, "<%s>", value_text(v->inner));
    else
        snprintf(v->text, sizeof v->text, "%ld", v->i * 3 - 7);
    v->has_text = 1;
}

static const char *value_text(struct val *v) {
    if (v->has_text)
        return v->text;
    format_val(v);
    return v->text;
}

static int __attribute__((noinline)) total_len(struct val *vals, int n) {
    int len = 0;
    for (int r = 0; r < 3; r++)
        for (int i = 0; i < n; i++)
            len += (int)strlen(value_text(&vals[i])) + r;
    return len;
}

int main(void) {
    for (int t = 0; t < 3; t++)
        tree_free(build(4 + t, t + 1));
    struct node *one = build(0, 99);
    tree_free(one);
    tree_free(0);
    printf("freed %d\n", freed);
    struct val vals[6];
    memset(vals, 0, sizeof vals);
    for (int i = 0; i < 6; i++) {
        vals[i].kind = i % 3 == 2 ? 2 : 1;
        vals[i].i = i * 1000 - 2500;
        vals[i].inner = i ? &vals[i - 1] : 0;
    }
    printf("len %d %s %s\n", total_len(vals, 6), value_text(&vals[5]), value_text(&vals[2]));
    return 0;
}
