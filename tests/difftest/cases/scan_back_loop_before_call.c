/* A scan backwards whose loop is entered both before and after the checks
 * that lead to a call, in the shape of SQLite's sqlite3_free_filename
 * (databaseName's four-byte terminator search, then sqlite3_free): the
 * frame the call needs must not be built again when a later check goes
 * back into the loop. */
#include <stdio.h>
#include <string.h>

static int freed;
static long freed_sum;
static long freed_at;
static const char *base;

static int __attribute__((noinline)) release(const char *p)
{
    freed++;
    return (int)strlen(p + 4);
}

static int (*volatile release_fn)(const char *) = release;

static const char *database_name(const char *z)
{
    while (z[-1] != 0 || z[-2] != 0 || z[-3] != 0 || z[-4] != 0)
        z--;
    return z;
}

static void __attribute__((noinline)) free_filename(const char *p)
{
    if (p == 0)
        return;
    p = database_name(p);
    freed_sum += release_fn(p - 4);
    freed_at = freed_at * 7 + (p - base);
}

int main(void)
{
    /* Four zero bytes, then names separated by single zeros, as
     * sqlite3_create_filename lays them out. */
    static char buf[256];
    int n = 4;
    base = buf;
    const char *parts[] = {"main.db", "journal", "a", "", "bb", "xyz"};
    const char *starts[6];
    for (int i = 0; i < 6; i++) {
        starts[i] = buf + n;
        memcpy(buf + n, parts[i], strlen(parts[i]) + 1);
        n += (int)strlen(parts[i]) + 1;
    }
    memcpy(buf + n, "\0\0\0", 4);
    for (int i = 0; i < 6; i++)
        free_filename(starts[i]);
    free_filename(0);
    printf("%d %ld %ld\n", freed, freed_sum, freed_at);
    return 0;
}
