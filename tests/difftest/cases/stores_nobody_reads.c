/* Locals written through their members and elements: stores nothing reads can
 * go, and these are the ways something can read them after all. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define NI __attribute__((noinline))

struct pair { int a, b, c, d; };

static int *kept;

/* Truly unread. */
static NI int unread(int x)
{
    struct pair p;
    int v[8];
    p.a = x;
    p.d = x + 1;
    for (int i = 0; i < 8; i++)
        v[i] = i * x;
    return x + 1;
}

/* Back from a member to the one before it. */
static NI int back_from_member(int x)
{
    struct pair p;
    int *q = &p.b;
    p.a = x;
    p.b = x + 1;
    return q[-1] + q[0];
}

/* A variable element of something written at constant ones. */
static NI int variable_index(int i)
{
    int v[4];
    v[0] = 10; v[1] = 20; v[2] = 30; v[3] = 40;
    return v[i];
}

/* One of two members, picked at run time. */
static NI int picked(int f)
{
    struct pair p;
    int *q = f ? &p.a : &p.c;
    p.a = 5;
    p.c = 7;
    return *q;
}

/* Through a loop's moving pointer. */
static NI int walked(void)
{
    int v[6], t = 0;
    for (int i = 0; i < 6; i++)
        v[i] = i + 1;
    for (int *q = v + 1; q < v + 6; q += 2)
        t += *q;
    return t;
}

/* The address as an integer and back. */
static NI int through_integer(int x)
{
    struct pair p;
    uintptr_t u = (uintptr_t)&p.b;
    p.b = x * 2;
    return *(int *)u;
}

/* The address kept in a global, read later by someone else. */
static NI int read_kept(void) { return *kept; }

static NI int through_global(int x)
{
    struct pair p;
    kept = &p.c;
    p.c = x + 9;
    return read_kept();
}

/* Handed to a call. */
static NI int sum2(const int *q) { return q[0] + q[1]; }

static NI int through_call(int x)
{
    struct pair p;
    p.b = x;
    p.c = x + 1;
    return sum2(&p.b);
}

/* Copied out whole, and from the middle. */
static NI int by_memcpy(int x)
{
    struct pair p, r;
    int two[2];
    p.a = x; p.b = x + 1; p.c = x + 2; p.d = x + 3;
    memcpy(&r, &p, sizeof r);
    memcpy(two, &p.b, sizeof two);
    return r.d + two[1];
}

/* A whole struct assigned and returned. */
static NI struct pair by_value(int x)
{
    struct pair p, q;
    p.a = x; p.b = 1; p.c = 2; p.d = 3;
    q = p;
    return q;
}

/* volatile: the stores themselves are the point. */
static NI int volatile_member(int x)
{
    volatile struct pair p;
    p.b = x;
    p.b = x + 1;
    return x;
}

/* Written in an inner scope, read through a pointer outliving it. */
static NI int a_pointer_to_a_pointer(int x)
{
    int v[2];
    int *q = &v[1];
    int **qq = &q;
    v[1] = x + 3;
    return **qq;
}

int main(void)
{
    printf("%d\n", unread(3));
    printf("%d\n", back_from_member(4));
    printf("%d %d\n", variable_index(1), variable_index(3));
    printf("%d %d\n", picked(1), picked(0));
    printf("%d\n", walked());
    printf("%d\n", through_integer(6));
    printf("%d\n", through_global(1));
    printf("%d\n", through_call(2));
    printf("%d\n", by_memcpy(1));
    printf("%d\n", by_value(8).a);
    printf("%d\n", volatile_member(1));
    printf("%d\n", a_pointer_to_a_pointer(1));
    return 0;
}
