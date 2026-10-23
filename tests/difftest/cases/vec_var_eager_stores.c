/* Vector variables kept in registers are stored to their save slots
 * early, after the last write in a block outside any loop, only where a
 * call that needs them may follow before they are written again, and a
 * function where no call needs any of them stores none around its calls.
 * These check the values stay right on every path: after a loop, across
 * one branch's call, a loop that only reads, and parameters rewritten on
 * some paths or all. */
#include <stdio.h>

typedef float v4 __attribute__((vector_size(16)));

__attribute__((noinline)) static void tick(int *n) { ++*n; }

/* v is live across tick() in the loop; the writes after the loop are
   followed by no call, so storing v to its slot there is wasted. */
__attribute__((noinline)) static float after_loop(v4 x, int n, int *count) {
    v4 v = x;
    for (int i = 0; i < n; i++) {
        v = v + x;
        tick(count);
    }
    v = v * x;
    v = v + x;
    return v[0] + v[3];
}

/* The write before the call does need its store; the write after it
   does not. */
__attribute__((noinline)) static float before_and_after(v4 x, int *count) {
    v4 v = x * x;
    tick(count);
    v = v + x;
    return v[1];
}

/* Only one branch calls: the store after the write is needed on that
   path, so it stays. */
__attribute__((noinline)) static float one_branch(v4 x, int c, int *count) {
    v4 v = x + x;
    if (c)
        tick(count);
    v = v * x;
    return v[2];
}

/* The loop calls but does not write v: v is stored once, before the
   loop, and not on every iteration. */
__attribute__((noinline)) static float loop_reads(v4 x, int n, int *count) {
    v4 v = x * x;
    float s = 0;
    for (int i = 0; i < n; i++) {
        tick(count);
        s += v[i & 3];
    }
    return s + v[0];
}

/* A parameter live across a call in a later block, written in between
   on one path only. */
__attribute__((noinline)) static float param_paths(v4 p, int c, int *count) {
    if (c)
        p = p + p;
    tick(count);
    return p[0] + p[3];
}

/* A parameter rewritten before every call is never stored on entry. */
__attribute__((noinline)) static float param_rewritten(v4 p, int *count) {
    p = p * p;
    tick(count);
    return p[2];
}

int main(void) {
    v4 x = {1, 2, 3, 4};
    int count = 0;
    printf("%g %g %g %g %d\n", after_loop(x, 3, &count), before_and_after(x, &count),
           one_branch(x, 1, &count), one_branch(x, 0, &count), count);
    printf("%g %g %g %g %d\n", loop_reads(x, 5, &count), param_paths(x, 1, &count),
           param_paths(x, 0, &count), param_rewritten(x, &count), count);
    return 0;
}
