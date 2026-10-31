/* A callee of a few tests, each returning (SQLite's sqlite3PagerWrite:
 * five tests, five returns, three of them calls), inlined where its
 * returns become jumps to the caller's continuation; and a free wrapper
 * whose own call to a larger external function, arriving with the
 * wrapper's inlined body, stays a call. */
#include <stdio.h>
#include <stdlib.h>

typedef struct Pager { int dbSize; int nSavepoint; int errCode; unsigned sectorSize; long pageSize; int journaled; } Pager;
typedef struct Page { Pager *pPager; unsigned pgno; unsigned short flags; int writes; } Page;

static int __attribute__((noinline)) subjournal(Page *p) { p->writes += 10; return p->pPager->nSavepoint > 2 ? 7 : 0; }
static int __attribute__((noinline)) large_sector(Page *p) { p->writes += 100; p->flags |= 4; return 0; }
static int __attribute__((noinline)) plain_write(Page *p) {
    p->writes += 1000;
    p->flags |= 4;
    p->pPager->journaled++;
    return p->pgno > 60 ? 13 : 0;
}

int pager_write(Page *pPg) {
    Pager *pPager = pPg->pPager;
    if ((pPg->flags & 4) != 0 && pPager->dbSize >= (int)pPg->pgno) {
        if (pPager->nSavepoint)
            return subjournal(pPg);
        return 0;
    } else if (pPager->errCode) {
        return pPager->errCode;
    } else if (pPager->sectorSize > (unsigned)pPager->pageSize) {
        return large_sector(pPg);
    } else {
        return plain_write(pPg);
    }
}

static long freed_bytes;
static int frees;

static int memstat = 1;
static void (*x_free)(void *) = free;

/* Inlined where called directly; a call arriving with db_free's inlined
 * body stays a call. */
void mem_free(void *p) {
    if (p == 0)
        return;
    if (memstat) {
        frees++;
        freed_bytes += 16;
        x_free(p);
    } else {
        x_free(p);
    }
}

typedef struct Db { int lookaside; long saved; } Db;

void db_free(Db *db, void *p) {
    if (db && db->lookaside && ((long)p & 1) == 0) {
        db->saved += 8;
        if (db->saved > 64) {
            mem_free(p);
            return;
        }
    }
    mem_free(p);
}

static int __attribute__((noinline)) touch_pages(Page *pages, int n) {
    int rc = 0;
    for (int i = 0; i < n; i++) {
        int r = pager_write(&pages[i]);
        if (r)
            rc = (int)(((unsigned)rc * 3u + (unsigned)r) & 0xffffff);
        pages[i].writes++;
    }
    return rc;
}

static long __attribute__((noinline)) churn(Db *db, int n) {
    long t = 0;
    for (int i = 0; i < n; i++) {
        long *p = malloc(sizeof(long) * 2);
        p[0] = i;
        t += p[0];
        db_free(i % 3 ? db : 0, p);
        db_free(db, 0);
        if (i == 20)
            memstat = 0;
        mem_free(malloc(8));
    }
    return t;
}

int main(void) {
    Pager pagers[3] = { { 50, 0, 0, 4096, 4096, 0 }, { 50, 3, 0, 512, 4096, 0 }, { 50, 0, 0, 8192, 4096, 0 } };
    Page pages[64];
    for (int i = 0; i < 64; i++) {
        pages[i].pPager = &pagers[i % 3];
        pages[i].pgno = (unsigned)i;
        pages[i].flags = (unsigned short)(i % 5 == 0 ? 4 : 0);
        pages[i].writes = 0;
    }
    int rc1 = touch_pages(pages, 64);
    pagers[0].errCode = 5;
    int rc2 = touch_pages(pages, 64);
    unsigned long w = 0;
    for (int i = 0; i < 64; i++)
        w = w * 7 + (unsigned long)(pages[i].writes + pages[i].flags);
    printf("pages %d %d %lu journaled %d %d %d\n", rc1, rc2, w, pagers[0].journaled, pagers[1].journaled, pagers[2].journaled);
    Db db = { 1, 0 };
    printf("churn %ld frees %d bytes %ld saved %ld\n", churn(&db, 40), frees, freed_bytes, db.saved);
    return 0;
}
