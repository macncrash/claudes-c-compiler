/* Non-zero exit status and output ordering through stdio buffers. */
#include <stdio.h>
#include <stdlib.h>
static void bye(void) { printf("atexit handler\n"); }
int main(void) {
    atexit(bye);
    printf("before exit\n");
    exit(3);
}
