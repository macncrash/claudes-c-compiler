/* C11 atomics and GCC __sync/__atomic builtins with real threads. */
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>

static atomic_int shared;
static long plain;

static void *bump(void *arg) {
    (void)arg;
    for (int i = 0; i < 100000; i++) {
        atomic_fetch_add(&shared, 1);
        __sync_fetch_and_add(&plain, 2);
    }
    return NULL;
}

int main(void) {
    pthread_t t[4];
    for (int i = 0; i < 4; i++) pthread_create(&t[i], NULL, bump, NULL);
    for (int i = 0; i < 4; i++) pthread_join(t[i], NULL);
    printf("%d %ld\n", atomic_load(&shared), plain);
    int expected = 400000;
    printf("cas %d\n", atomic_compare_exchange_strong(&shared, &expected, 7));
    printf("xchg %d now %d\n", atomic_exchange(&shared, 9), shared);
    long v = 5;
    printf("%ld %ld\n", __atomic_add_fetch(&v, 3, __ATOMIC_SEQ_CST), __atomic_load_n(&v, __ATOMIC_ACQUIRE));
    return 0;
}
