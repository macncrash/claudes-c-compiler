/* constructor(N) and destructor(N) priorities: constructors from the lowest
 * N, destructors to it, those without one as 65535 in declaration order;
 * given on a prototype too. ccc skipped the priority and ran them as declared.
 * (gcc on Darwin rejects priorities; clang orders them within a file.) */
#include <stdio.h>
#define PRIO_BASE 100
static void late_proto(void) __attribute__((constructor(300)));
static void dtor_proto(void) __attribute__((destructor(150)));
__attribute__((constructor(200))) static void c200(void) { puts("c200"); }
__attribute__((constructor)) static void cnone_a(void) { puts("c-none-a"); }
__attribute__((constructor(PRIO_BASE + 5))) static void c105(void) { puts("c105"); }
__attribute__((constructor(65535))) static void cmax(void) { puts("c65535"); }
static void after(void) __attribute__((constructor(101)));
static void after(void) { puts("c101"); }
__attribute__((constructor)) static void cnone_b(void) { puts("c-none-b"); }
static void late_proto(void) { puts("c300"); }
__attribute__((destructor(200))) static void d200(void) { puts("d200"); }
__attribute__((destructor)) static void dnone(void) { puts("d-none"); }
__attribute__((destructor(101))) static void d101(void) { puts("d101"); }
static void dtor_proto(void) { puts("d150"); }
__attribute__((destructor(400))) static void d400(void) { puts("d400"); }
int main(void) { puts("main"); return 0; }
