/* Small structs passed by value from over-aligned locals. On AArch64 a
 * struct of 16 bytes or less, or an HFA, is loaded into argument registers
 * through x17, and the address of an over-aligned local (here 32- and
 * 64-byte aligned) once used x17 as its rounding scratch too, so the struct
 * was read from (x17 & -align) near address 0 and the program crashed. */
#include <stdio.h>

struct S14 { int a; char b[7]; _Bool c; int d; };          /* 16 bytes, GP */
struct Hfa { float x, y, z; };                              /* HFA */
struct Hd { double x, y; };                                 /* HFA of double */
struct Big { struct S14 s; double d1; struct Hfa h; int tail : 13; } __attribute__((aligned(32)));
struct A32 { long v; } __attribute__((aligned(32)));
struct A64 { float x, y; } __attribute__((aligned(64)));
struct Pair { struct Hd hd; struct A32 a; } __attribute__((aligned(16)));

__attribute__((noinline)) static void p14(struct S14 s)
{
    printf("s14 %d %d %d %d %d\n", s.a, s.b[0], s.b[6], s.c, s.d);
}

__attribute__((noinline)) static void phfa(struct Hfa h)
{
    printf("hfa %g %g %g\n", h.x, h.y, h.z);
}

__attribute__((noinline)) static void phd(struct Hd h)
{
    printf("hd %g %g\n", h.x, h.y);
}

__attribute__((noinline)) static void pa32(struct A32 a)
{
    printf("a32 %ld\n", a.v);
}

__attribute__((noinline)) static void pa64(struct A64 a)
{
    printf("a64 %g %g\n", a.x, a.y);
}

/* The struct arrives by reference and is copied into a 32-byte aligned
 * local; its members are then passed on by value. */
static void show(struct Big b)
{
    p14(b.s);
    phfa(b.h);
    printf("big %g %d\n", b.d1, b.tail);
}

__attribute__((noinline)) void through(struct Big b, struct Pair p)
{
    show(b);
    phd(p.hd);
    pa32(p.a);
}

int main(void)
{
    struct Big b = { { -5, { 1, 2, 3, 4, 5, 6, 7 }, 1, 99 }, 2.5, { 1.5f, -2.5f, 3.25f }, -100 };
    struct Pair p = { { 4.5, -6.75 }, { 1234567890123L } };
    struct A32 a = { -42 };
    struct A64 c = { 0.5f, 8.0f };
    through(b, p);
    show(b);
    pa32(a);
    pa64(c);
    struct A64 arr[3] = { { 1, 2 }, { 3, 4 }, { 5, 6 } };
    for (int i = 0; i < 3; i++)
        pa64(arr[i]);
    return 0;
}
