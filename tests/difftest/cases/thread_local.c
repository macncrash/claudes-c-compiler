/* Thread-local storage (Mach-O uses TLV descriptors, not ELF TLS) and pthreads. */
#include <pthread.h>
#include <stdio.h>

_Thread_local int tls_counter = 5;
static _Thread_local long tls_zero;

static void *worker(void *arg) {
    tls_counter += (int)(long)arg;
    tls_zero = 100;
    return (void *)(long)tls_counter;
}

int main(void) {
    pthread_t t[4];
    for (long i = 0; i < 4; i++) pthread_create(&t[i], NULL, worker, (void *)i);
    long total = 0;
    for (int i = 0; i < 4; i++) { void *r; pthread_join(t[i], &r); total += (long)r; }
    printf("threads %ld main %d zero %ld\n", total, tls_counter, tls_zero);
    return 0;
}
