/* Common libc entry points, including callbacks into ccc code and globals
 * defined in libSystem (stdout/stderr/errno go through the GOT on Darwin). */
#include <errno.h>
#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static int cmp_int(const void *a, const void *b) { return *(const int *)a - *(const int *)b; }
static jmp_buf env;
static void jump(int v) { longjmp(env, v); }

int main(void) {
    int arr[] = {5, 3, 9, 1, 7, 2, 8};
    qsort(arr, 7, sizeof(int), cmp_int);
    for (int i = 0; i < 7; i++) printf("%d ", arr[i]);
    printf("\n");
    int key = 7;
    int *found = bsearch(&key, arr, 7, sizeof(int), cmp_int);
    printf("bsearch %ld\n", found ? (long)(found - arr) : -1L);

    char *s = strdup("Hello, World");
    for (char *p = s; *p; p++) *p = toupper((unsigned char)*p);
    printf("%s %zu %d\n", s, strlen(s), strcmp(s, "HELLO, WORLD"));
    free(s);

    errno = 0;
    strtol("99999999999999999999", NULL, 10);
    printf("errno ERANGE %d\n", errno == ERANGE);
    fprintf(stdout, "stdout ok\n");
    fflush(stdout);

    printf("%.4f %.4f %.4f %.4f\n", sqrt(2.0), sin(1.0), pow(2.0, 0.5), atan2(1.0, 2.0));
    printf("%.3f %.3f\n", floor(-2.5), ceil(-2.5));

    int r = setjmp(env);
    if (r == 0) jump(42);
    printf("longjmp %d\n", r);

    char buf[32];
    int n = sscanf("12 abc 3.5", "%d %s", &key, buf);
    printf("sscanf %d %d %s\n", n, key, buf);
    long long ll = atoll("-1234567890123");
    printf("%lld\n", ll);
    return 0;
}
