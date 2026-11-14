/* SQLite driver: runs a fixed script in an in-memory database (generated
 * tables, indexes, joins, aggregates, window functions, recursive CTEs, the
 * Mandelbrot query, string, date, JSON and printf functions, floating
 * point, FTS5) and prints every result row. */
#include <stdio.h>
#include "sqlite3.h"

static int rows;

static int print_row(void *tag, int n, char **vals, char **names) {
    (void)names;
    printf("%s:", (const char *)tag);
    for (int i = 0; i < n; i++) printf(" %s", vals[i] ? vals[i] : "NULL");
    printf("\n");
    rows++;
    return 0;
}

static const char *script[] = {
    "CREATE TABLE t(id INTEGER PRIMARY KEY, a INT, b REAL, c TEXT, d BLOB)",
    "WITH RECURSIVE r(i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM r WHERE i < 3000) "
    "INSERT INTO t SELECT i, (i * 7919) % 1009 - 500, (i * 0.37) / 3.0 + (i % 17) * 1e-3, "
    "printf('row-%05d-%s', i, substr('abcdefghijklmnop', 1 + i % 13, 3)), zeroblob(i % 5) FROM r",
    "CREATE INDEX ta ON t(a)",
    "CREATE INDEX tc ON t(c COLLATE NOCASE)",
    "CREATE TABLE u(k INT, v TEXT)",
    "WITH RECURSIVE r(i) AS (SELECT 0 UNION ALL SELECT i + 1 FROM r WHERE i < 400) "
    "INSERT INTO u SELECT (i * 31) % 1009 - 500, hex(i * i) FROM r",
    "SELECT count(*), sum(a), total(b), min(c), max(c), avg(a) FROM t",
    "SELECT a % 10 AS g, count(*), sum(a), printf('%.6f', avg(b)) FROM t GROUP BY g ORDER BY g",
    "SELECT t.id, t.a, u.v FROM t JOIN u ON u.k = t.a WHERE t.id % 97 = 0 ORDER BY t.id LIMIT 20",
    "SELECT id, a, row_number() OVER (PARTITION BY a % 3 ORDER BY id), "
    "sum(a) OVER (ORDER BY id ROWS BETWEEN 2 PRECEDING AND CURRENT ROW), "
    "lag(c) OVER (ORDER BY id) FROM t WHERE id < 25",
    "SELECT id, rank() OVER w, dense_rank() OVER w, ntile(4) OVER w FROM t WHERE id < 30 "
    "WINDOW w AS (ORDER BY a)",
    "SELECT c FROM t WHERE c LIKE 'ROW-0012%' ORDER BY c COLLATE NOCASE DESC",
    "SELECT c FROM t WHERE c GLOB '*-0099?-*' ORDER BY id",
    "SELECT upper(c), lower('MiXeD'), length(c), instr(c, 'def'), replace(c, 'row', 'R'), "
    "trim('  x  '), quote(c), unicode('é'), char(65, 66, 67) FROM t WHERE id IN (1, 77, 2999)",
    "SELECT printf('%d|%5.2f|%e|%x|%s|%q|%-6s|%08.3f', id, b, b * 1e10, id, c, 'it''s', 'l', -b) "
    "FROM t WHERE id % 500 = 1",
    "SELECT 1e308 * 10, -1e308 * 10, 9223372036854775807 + 1, 9223372036854775807 * 2, "
    "CAST(9223372036854775807 AS REAL), 0.1 + 0.2, 1.0 / 3, round(2.675, 2), 7 / 2, -7 % 3, 7.5 % 2",
    "SELECT abs(-3), max(1, 2.5, 'a'), min(3, 2), coalesce(NULL, 4), nullif(5, 5), iif(1 < 2, 'y', 'n'), "
    "typeof(1), typeof(1.0), typeof('x'), typeof(x'00'), typeof(NULL)",
    "SELECT date('2024-02-28', '+1 day'), date('2023-02-28', '+1 day'), datetime(1700000000, 'unixepoch'), "
    "strftime('%Y-%j %W %w %H:%M:%f', '2026-11-14 05:06:07.891'), julianday('2000-01-01'), "
    "date('2024-01-31', '+1 month'), time('12:00', '-90 minutes')",
    "SELECT json_object('id', id, 'a', a, 'tags', json_array(c, b)) FROM t WHERE id <= 3",
    "SELECT json_extract('{\"x\":[1,2,{\"y\":\"z\"}]}', '$.x[2].y'), json_group_array(a) FROM "
    "(SELECT a FROM t WHERE id <= 8 ORDER BY a)",
    "SELECT key, value FROM json_each('[10, 20.5, \"s\", null, true]')",
    "WITH RECURSIVE fib(n, a, b) AS (SELECT 0, 0, 1 UNION ALL SELECT n + 1, b, a + b FROM fib WHERE n < 90) "
    "SELECT n, a FROM fib WHERE n % 15 = 0",
    "WITH RECURSIVE xaxis(x) AS (VALUES(-2.0) UNION ALL SELECT x + 0.05 FROM xaxis WHERE x < 1.2), "
    "yaxis(y) AS (VALUES(-1.0) UNION ALL SELECT y + 0.1 FROM yaxis WHERE y < 1.0), "
    "m(iter, cx, cy, x, y) AS (SELECT 0, x, y, 0.0, 0.0 FROM xaxis, yaxis UNION ALL "
    "SELECT iter + 1, cx, cy, x * x - y * y + cx, 2.0 * x * y + cy FROM m WHERE (x * x + y * y) < 4.0 AND iter < 28), "
    "m2(iter, cx, cy) AS (SELECT max(iter), cx, cy FROM m GROUP BY cx, cy), "
    "a(t) AS (SELECT group_concat(substr(' .+*#', 1 + min(iter / 7, 4), 1), '') FROM m2 GROUP BY cy) "
    "SELECT group_concat(rtrim(t), char(10)) FROM a",
    "SELECT a, count(*) FROM t GROUP BY a HAVING count(*) > 4 ORDER BY count(*) DESC, a LIMIT 10",
    "SELECT id FROM t WHERE a BETWEEN -3 AND 3 AND b > 100 ORDER BY b DESC LIMIT 5",
    "SELECT count(DISTINCT a), group_concat(DISTINCT a % 7) FROM t",
    "UPDATE t SET b = b * 2, c = c || '!' WHERE id % 1000 = 0",
    "DELETE FROM t WHERE id % 3 = 0",
    "SELECT count(*), sum(b), max(length(c)), sum(length(d)) FROM t",
    "SELECT id, c, b FROM t WHERE id % 1000 = 0 OR id IN (1, 2) ORDER BY id",
    "CREATE VIRTUAL TABLE f USING fts5(body)",
    "INSERT INTO f SELECT c || ' ' || (CASE WHEN id % 2 THEN 'odd numbers' ELSE 'even things' END) FROM t WHERE id < 200",
    "SELECT count(*) FROM f WHERE f MATCH 'odd'",
    "SELECT body FROM f WHERE f MATCH 'row NEAR even' ORDER BY rank LIMIT 3",
    "SELECT sqlite_version() IS NOT NULL, total_changes()",
    NULL,
};

int main(void) {
    sqlite3 *db;
    if (sqlite3_open(":memory:", &db) != SQLITE_OK) { printf("open failed\n"); return 1; }
    int bad = 0;
    for (int i = 0; script[i]; i++) {
        char tag[16];
        snprintf(tag, sizeof tag, "q%d", i);
        char *err = NULL;
        if (sqlite3_exec(db, script[i], print_row, tag, &err) != SQLITE_OK) {
            printf("%s error: %s\n", tag, err);
            sqlite3_free(err);
            bad++;
        }
    }
    sqlite3_close(db);
    printf("sqlite %s, %d rows, %d errors\n", sqlite3_libversion(), rows, bad);
    return bad != 0;
}
