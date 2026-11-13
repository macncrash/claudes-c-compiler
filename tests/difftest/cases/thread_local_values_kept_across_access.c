// A value computed before a thread-local access and passed to a call after
// it. On Mach-O each access calls the TLV thunk (ldr x16, [x0]; blr x16),
// which changes only x0, x16 and x17, so the value stays in a temporary
// across it; a peephole pass took the thunk call for one changing x0-x18
// and dropped the copy of the value into the argument register.
#include <stdio.h>
#include <pthread.h>

typedef unsigned long u64;
struct T { char c; int i; long l; short s[3]; };
static u64 mix(u64 h, u64 v) { return (h ^ v) * 1099511628211UL; }
__attribute__((noinline)) static void tw(struct T *t, int k) { t->i += k; t->s[k & 1] ^= (short)k; }
__attribute__((noinline)) static void add2(int *p, int a, int b) { *p += a * 3 + b; }
static _Thread_local struct T t0 = {-7, 63, -85976L, {66, -39}};
_Thread_local int ti = 5;

__attribute__((noinline)) static u64 work(int tid, int k) {
  static _Thread_local unsigned sl0 = 973322951u;
  u64 h = 1469598103934665603UL;
  int m = k + tid, n = k * 5 - tid;
  if (m & 1) h = mix(h, sl0); else h = mix(h, sl0 + 1);
  tw(&t0, m);
  add2(&ti, m, n);
  h = mix(h, (u64)t0.c ^ (u64)t0.i ^ (u64)t0.l ^ (u64)t0.s[0] ^ (u64)t0.s[1] << 7 ^ (u64)t0.s[2] << 14);
  return mix(h, (u64)ti);
}

static _Thread_local struct T t1 = {-7, 63, -85976L, {66, -39}};

__attribute__((noinline)) static u64 work1(int tid, int k) {
  static _Thread_local unsigned sl1 = 973322951u;
  u64 h = 1469598103934665603UL;
  if ((k + tid) & 1) { h = mix(h, (u64)(sl1)); } else { h = mix(h, (u64)(sl1)); }
  tw(&t1, k + tid);
  h = mix(h, (u64)t1.c ^ (u64)t1.i ^ (u64)t1.l ^ (u64)t1.s[0] ^ (u64)t1.s[1] << 7 ^ (u64)t1.s[2] << 14);
  return h;
}

static u64 res[4];
static void *thr(void *a) {
  int tid = (int)(long)a;
  u64 h = 0;
  for (int k = 0; k < 3; k++) h = mix(h, work(tid, k));
  for (int k = 0; k < 3; k++) h = mix(h, work1(tid, k));
  res[tid] = h;
  return 0;
}

int main(void) {
  u64 h = 0;
  for (int k = 0; k < 3; k++) h = mix(h, work(0, k));
  printf("main %lx %d %d\n", h, t0.i, ti);
  pthread_t th[3];
  for (long i = 0; i < 3; i++) pthread_create(&th[i], 0, thr, (void *)(i + 1));
  for (int i = 0; i < 3; i++) { pthread_join(th[i], 0); printf("thr %d %lx\n", i, res[i + 1]); }
  for (int k = 0; k < 2; k++) h = mix(h, work(1, k));
  for (int k = 0; k < 2; k++) h = mix(h, work1(1, k));
  printf("main %lx %d %d %d %d %d\n", h, t0.i, t0.s[1], ti, t1.i, t1.s[1]);
  return 0;
}
