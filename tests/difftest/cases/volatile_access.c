/* Volatile objects reached every way the front end tracks: globals,
 * pointer parameters, casts, struct fields and bitfields, volatile
 * typedefs, static and plain locals, parameters, and volatile pointers.
 * A timer signal writes each object while a loop polls it, so a read
 * hoisted out of the loop or merged with an earlier one never sees the
 * write and the test hangs. */
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <sys/time.h>

typedef volatile unsigned vu32;
struct regs { vu32 ctrl; unsigned data; volatile unsigned stat : 3; unsigned rest : 5; };
struct dev { volatile int *regs; int n; };

static volatile int flag;
static volatile int ticks;
static int plain_word;
static volatile int *volatile target;
static struct regs hw;
static volatile int bank[4];

static void on_alarm(int sig) {
    (void)sig;
    ticks++;
    if (target)
        *target += 1;
    hw.ctrl = 5;
    hw.stat = 6;
    bank[2] = 9;
    plain_word = 7;
}

static void arm(volatile int *t) {
    struct itimerval it = { { 0, 0 }, { 0, 2000 } };
    target = t;
    setitimer(ITIMER_REAL, &it, 0);
}

static void arm_repeating(void) {
    struct itimerval it = { { 0, 1000 }, { 0, 1000 } };
    target = 0;
    setitimer(ITIMER_REAL, &it, 0);
}

static void disarm(void) {
    struct itimerval it = { { 0, 0 }, { 0, 0 } };
    setitimer(ITIMER_REAL, &it, 0);
}

__attribute__((noinline)) static int spin_global(void) {
    int n = 0;
    while (!flag)
        n++;
    return n > 0;
}

__attribute__((noinline)) static int spin_ptr(volatile int *p) {
    while (!*p)
        ;
    return *p;
}

#define READ_ONCE(x) (*(const volatile __typeof__(x) *)&(x))

__attribute__((noinline)) static int spin_cast(void) {
    while (READ_ONCE(plain_word) != 7)
        ;
    return plain_word;
}

__attribute__((noinline)) static unsigned spin_field(struct regs *r) {
    while (r->ctrl != 5)
        ;
    return r->ctrl;
}

__attribute__((noinline)) static unsigned spin_bitfield(struct regs *r) {
    while (r->stat != 6)
        ;
    return r->stat + r->rest;
}

__attribute__((noinline)) static int spin_member_ptr(struct dev *d) {
    while (d->regs[2] != 9)
        ;
    return d->regs[2];
}

__attribute__((noinline)) static int spin_static(void) {
    static volatile int seen;
    arm(&seen);
    while (!seen)
        ;
    return seen;
}

__attribute__((noinline)) static int spin_local(void) {
    volatile int mine = 0;
    arm(&mine);
    while (!mine)
        ;
    return mine;
}

__attribute__((noinline)) static int spin_param(volatile int n) {
    arm(&n);
    while (!n)
        ;
    return n;
}

__attribute__((noinline)) static int count_ticks(int want) {
    int polls = 0;
    while (ticks < want)
        polls++;
    return polls >= 0;
}

/* Reads and writes that must each happen, with ordinary arithmetic. */
__attribute__((noinline)) static int twice(volatile int *p) { return *p + *p; }
__attribute__((noinline)) static void bump(volatile int *p) { (*p)++; *p += 3; *p <<= 1; }
__attribute__((noinline)) static unsigned bits(struct regs *r) {
    r->stat = 3;
    r->rest = 17;
    r->stat += 2;
    return r->stat * 100 + r->rest;
}
__attribute__((noinline)) static int ptr_self(int *volatile p) { return *p + *p; }

static jmp_buf env;

__attribute__((noinline)) static void jump(void) { longjmp(env, 1); }

__attribute__((noinline)) static int after_longjmp(void) {
    volatile int kept = 1;
    int *volatile also = 0;
    int local = 4;
    if (setjmp(env) == 0) {
        kept = 2;
        also = &local;
        jump();
    }
    return kept * 10 + (also == &local);
}

int main(void) {
    signal(SIGALRM, on_alarm);

    arm(&flag);
    printf("global %d\n", spin_global());
    volatile int word = 0;
    arm(&word);
    printf("ptr %d\n", spin_ptr(&word));
    arm(0);
    printf("cast %d\n", spin_cast());
    arm(0);
    printf("field %u\n", spin_field(&hw));
    hw.rest = 4;
    arm(0);
    printf("bitfield %u\n", spin_bitfield(&hw));
    struct dev d = { bank, 4 };
    arm(0);
    printf("member ptr %d\n", spin_member_ptr(&d));
    printf("static %d\n", spin_static());
    printf("local %d\n", spin_local());
    printf("param %d\n", spin_param(0));
    arm_repeating();
    printf("ticks %d\n", count_ticks(ticks + 3));
    disarm();

    int v = 5;
    printf("twice %d\n", twice(&v));
    bump(&v);
    printf("bump %d\n", v);
    struct regs r = { 0 };
    printf("bits %u\n", bits(&r));
    printf("ptr self %d\n", ptr_self(&v));
    printf("longjmp %d\n", after_longjmp());
    return 0;
}
