#!/usr/bin/env python3
"""Compound literal and _Generic fuzzer: subscripted array literals with
runtime indexes, member access on struct literals, literals as lvalues
(assigned through pointers, ++, compound assignment), literals re-created
in loops, nested literals with designators, file-scope literals (static
storage, pointers into them), literals passed to and returned through
functions, char array literals from strings; _Generic over promoted,
converted, qualified and decayed controlling expressions, printing the
selected association.
usage: clfz.py CCC CLANG WORK SEED COUNT   (env OPT, default -O2)"""
import os
import random
import subprocess
import sys

CCC, CLANG, WORK, SEED, COUNT = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]), int(sys.argv[5])
OPT = os.environ.get('OPT', '-O2')
os.makedirs(WORK, exist_ok=True)

HDR = r'''#include <stdio.h>
#include <string.h>
struct P { int x, y; };
struct Q { char c; long l; short s[3]; };
struct R { struct P p; int a[4]; double d; };
struct B { unsigned u : 5; int i : 7; long long w : 40; };
union U { int i; float f; char b[6]; };
enum E { EA = 1, EB = 300 };
static volatile int V[8] = {3, -1, 7, 0, 2, 5, -9, 1};
static unsigned long H = 1469598103934665603UL;
static void h(long v) { H = (H ^ (unsigned long)v) * 1099511628211UL; }
#define G(e) _Generic((e), _Bool: 1, char: 2, signed char: 3, unsigned char: 4, short: 5, unsigned short: 6, int: 7, unsigned: 8, long: 9, unsigned long: 10, long long: 11, unsigned long long: 12, float: 13, double: 14, long double: 15, int *: 16, const int *: 17, char *: 18, const char *: 19, struct P: 20, struct P *: 21, void *: 22, int (*)[4]: 23, int (*)(int): 24, union U: 25, default: 99)
static int fn(int a) { return a * 3 + 1; }
static struct P mkp(struct P p, int d) { p.x += d; p.y -= d; return p; }
static long sumarr(const int *a, int n) { long s = 0; for (int i = 0; i < n; i++) s = s * 7 + a[i]; return s; }
static long qsum(struct Q q) { return q.c + q.l * 3 + q.s[0] - q.s[1] + q.s[2] * 5; }
'''

GEN_EXPRS = [
    'c', 'uc', 's', 'us', 'i', 'u', 'l', 'ul', 'll', 'ull', 'f', 'd', 'ld', 'bo',
    '+c', '-uc', '~s', 'c + uc', 's * us', 'u + i', 'l + u', 'ul - ll', 'i ? f : d', 'i ? c : s',
    'c, l', '(char)i', '(short)l', 'f + i', 'ld * 2', 'bo + bo', '!i', 'i < l', 'u << 1', 'c << ll',
    'arr', '&arr', 'carr', 'str', '"lit"', 'fn', '&fn', 'cip', 'ip', 'vp', '*ip', '1 ? ip : vp',
    '1 ? ip : 0', '1 ? cip : ip', 'pp', '*pp', 'pv', 'uv', 'uv.i', 'pp->x', 'bv.u', 'bv.i',
    'bv.w', 'bv.u + 0', 'en', 'EA', '(enum E)0', 'ci', '+ci', 'c++', 'i = l', 'sizeof(int)',
    '(struct P){1, 2}', '(int[]){1, 2}', '"ab"[1]', '0x7fffffff + 0u',
    '2147483648', '4294967296', '1.0f', '1.0', '1.0L', "'a'", '0x80000000', '10000000000u',
]


def val(r):
    return r.choice([0, 1, -1, 2, 7, -100, 255, r.randint(-5000, 5000)])


def vi(r):
    """a runtime int expression"""
    return r.choice(['V[%d]' % r.randint(0, 7), 'n', 'k', str(val(r)), '(V[%d] & 3)' % r.randint(0, 7)])


def idx(r, n):
    return r.choice(['%d' % r.randint(0, n - 1), '(V[%d] & %d) %% %d' % (r.randint(0, 7), 7, n), 'k %% %d' % n])


def plit(r):
    if r.random() < 0.5:
        return '(struct P){%s, %s}' % (vi(r), vi(r))
    return '(struct P){.y = %s, .x = %s}' % (vi(r), vi(r))


def qlit(r):
    parts = []
    if r.random() < 0.6:
        parts.append('.c = %s' % vi(r))
    parts.append('.l = %s' % vi(r))
    if r.random() < 0.7:
        parts.append('.s = {%s}' % ', '.join(vi(r) for _ in range(r.randint(1, 3))))
    elif r.random() < 0.5:
        parts.append('.s[%d] = %s' % (r.randint(0, 2), vi(r)))
    r.shuffle(parts)
    return '(struct Q){%s}' % ', '.join(parts)


def rlit(r):
    parts = ['.p = %s' % plit(r) if r.random() < 0.5 else '.p.y = %s' % vi(r)]
    if r.random() < 0.6:
        parts.append('.a = {%s}' % ', '.join(vi(r) for _ in range(r.randint(1, 4))))
    else:
        parts.append('.a[%d] = %s' % (r.randint(0, 3), vi(r)))
    if r.random() < 0.5:
        parts.append('.d = %s.5' % r.randint(-9, 9))
    r.shuffle(parts)
    return '(struct R){%s}' % ', '.join(parts)


def elided(r, dims, val_fn, full=False):
    """initializer items (no outer braces) for an array of dims: each
    subarray is braced (maybe partial) or elided (complete, so the items
    stay unambiguous)."""
    if not dims:
        return [val_fn()]
    out = []
    n = dims[0] if full else r.randint(1, dims[0])
    for _ in range(n):
        if len(dims) > 1 and r.random() < 0.5:
            out.append('{%s}' % ', '.join(elided(r, dims[1:], val_fn)))
        else:
            out.extend(elided(r, dims[1:], val_fn, full=True))
    return out


def md_case(r, vf):
    """(type suffix, dims, items) for an unsized array of arrays"""
    dims = [r.randint(1, 4)] + [r.randint(1, 3) for _ in range(r.randint(1, 2))]
    items = elided(r, dims, vf)
    return ''.join('[%d]' % d for d in dims[1:]), dims, items


def md_print(name, dims):
    loops = ''.join('for (int i%d = 0; i%d < %d; i%d++) ' % (j, j, d, j) for j, d in enumerate(dims[1:], 1))
    idx = ''.join('[i%d]' % j for j in range(1, len(dims)))
    return ('h(sizeof %s); for (int i0 = 0; i0 < (int)(sizeof %s / sizeof %s[0]); i0++) %sh(%s[i0]%s);'
            % (name, name, name, loops, name, idx))


def stmt(r, depth=0):
    c = r.randint(0, 21)
    if c == 18:
        suf, dims, items = md_case(r, lambda: vi(r))
        return '{ int ma[]%s = {%s}; %s }' % (suf, ', '.join(items), md_print('ma', dims))
    if c == 19:
        suf, dims, items = md_case(r, lambda: vi(r))
        return '{ int (*mp)%s = (int[]%s){%s}; h(sizeof((int[]%s){%s})); h(mp[0]%s); }' % (
            suf, suf, ', '.join(items), suf, ', '.join(items), '[0]' * (len(dims) - 1))
    if c == 20:
        n = r.randint(1, 7)
        vals = ', '.join(vi(r) for _ in range(n))
        return '{ struct P *fp = (struct P[]){%s}; h(sizeof((struct P[]){%s})); for (int j = 0; j < %d; j++) { h(fp[j].x); h(fp[j].y); } }' % (
            vals, vals, (n + 1) // 2)
    if c == 21:
        return 'for (int j = 0; j < (int)(sizeof GM / sizeof GM[0]); j++) h(GMP[j / GMR][j % GMR]), h(GM[j][0]);'
    if c == 0:
        n = r.randint(1, 6)
        return 'h((int[]){%s}[%s]);' % (', '.join(vi(r) for _ in range(n)), idx(r, n))
    if c == 1:
        n = r.randint(2, 6)
        return 'h((int[%d]){%s}[%s]);' % (n, ', '.join(vi(r) for _ in range(r.randint(1, n))), idx(r, n))
    if c == 2:
        return 'h(%s.%s);' % (plit(r), r.choice('xy'))
    if c == 3:
        return 'h(qsum(%s));' % qlit(r)
    if c == 4:
        f = r.choice(['p.x', 'p.y', 'a[0]', 'a[1]', 'a[2]', 'a[3]'])
        return 'h(%s.%s); h((long)(%s.d * 2));' % (rlit(r), f, rlit(r))
    if c == 5:
        n = r.randint(2, 5)
        return ('{ int *p = (int[%d]){%s}; p[%s] += %s; p[%s]++; h(sumarr(p, %d)); }'
                % (n, ', '.join(vi(r) for _ in range(r.randint(0, n))), idx(r, n), vi(r), idx(r, n), n))
    if c == 6:
        return ('{ struct P *pp = &%s; pp->%s = %s; pp->%s *= 3; h(pp->x); h(pp->y); }'
                % (plit(r), r.choice('xy'), vi(r), r.choice('xy')))
    if c == 7:
        # literal re-created each iteration
        return ('{ int *keep = 0; long acc = 0; for (int j = 0; j < 4; j++) { int *q = (int[3]){j, %s, j * %s}; if (j == 2) keep = q; acc += q[0] + q[1] * q[2]; q[1] = 99; } h(acc); }'
                % (vi(r), vi(r)))
    if c == 8:
        return 'h(mkp(%s, %s).%s);' % (plit(r), vi(r), r.choice('xy'))
    if c == 9:
        return '{ char *cs = (char[]){"%s"}; cs[%d] = %d; h(strlen(cs)); h(cs[0] + cs[%d]); }' % (
            r.choice(['hello', 'ab', 'xyzzy!']), r.randint(0, 1), r.randint(0, 120), r.randint(0, 1))
    if c == 10:
        return '{ union U uu = (union U){.%s}; h(uu.b[%d]); h(uu.i & 0xff); }' % (
            r.choice(['i = %s' % vi(r), 'b = {%d, %d, 3}' % (r.randint(0, 99), r.randint(0, 99))]), r.randint(0, 2))
    if c == 11:
        return '{ struct B bb = (struct B){%s, %s, %s}; h(bb.u); h(bb.i); h(bb.w); bb.u += %s; h(bb.u); }' % (
            r.randint(0, 31), r.randint(-64, 63), r.randint(-(1 << 39), (1 << 39) - 1), vi(r))
    if c == 12:
        e = r.choice(GEN_EXPRS)
        return 'h(G((%s)));' % e
    if c == 13:
        return 'h(gp[%s]); gp[%s] += %s; h(gq->s[%d]); gq->l ^= %s; h(gq->l); h(gr.a[%d]);' % (
            idx(r, 4), idx(r, 4), vi(r), r.randint(0, 2), vi(r), r.randint(0, 3))
    if c == 14:
        n = r.randint(2, 4)
        return 'h(((struct P[%d]){%s})[%s].%s);' % (
            n, ', '.join(plit(r) if r.random() < 0.5 else '{%s, %s}' % (vi(r), vi(r)) for _ in range(r.randint(1, n))),
            idx(r, n), r.choice('xy'))
    if c == 15:
        return 'h(sizeof((int[]){%s}) + sizeof((struct Q){0}) * 3 + sizeof((char[]){"abc"}));' % (
            ', '.join(vi(r) for _ in range(r.randint(1, 5))))
    if c == 16:
        return '{ const int *cq = (const int[]){%s}; h(cq[%s]); h(G(cq)); h(G(*cq)); }' % (
            ', '.join(vi(r) for _ in range(4)), idx(r, 4))
    return '{ struct P p2 = %s; p2 = (struct P){p2.y, p2.x + %s}; h(p2.x - p2.y); }' % (plit(r), vi(r))


def gen_file(r):
    out = [HDR]
    out.append('static int *gp = (int[4]){%s};' % ', '.join(str(val(r)) for _ in range(r.randint(1, 4))))
    out.append('static struct Q *gq = &(struct Q){.l = %d, .s = {%d, %d}};' % (val(r), val(r), val(r)))
    out.append('static struct R gr = {.a = {%d, %d}};' % (val(r), val(r)))
    out.append('static const char *gs = (const char[]){"gstr"};')
    dims = [r.randint(1, 4), r.randint(1, 3)]
    out.append('static int GM[][%d] = {%s};' % (dims[1], ', '.join(elided(r, dims, lambda: str(val(r))))))
    out.append('#define GMR %d' % dims[1])
    out.append('static int (*GMP)[%d] = (int[][%d]){%s};' % (dims[1], dims[1], ', '.join(elided(r, [dims[0], dims[1]], lambda: str(val(r)), full=True))))
    out.append('static struct P *GSP = (struct P[]){%s};' % ', '.join(str(val(r)) for _ in range(r.randint(1, 6))))
    out.append('static int *GDP = (int[]){%d, [%d] = %d, %d};' % (val(r), r.randint(1, 6), val(r), val(r)))
    out.append('int main(int argc, char **argv) {')
    out.append('  int n = argc + %d, k = V[%d] & 7;' % (r.randint(0, 5), r.randint(0, 7)))
    out.append('  char c = %d; unsigned char uc = %d; signed char sc = %d; short s = %d; unsigned short us = %d;'
               % (r.randint(-100, 100), r.randint(0, 255), r.randint(-100, 100), val(r), r.randint(0, 65535)))
    out.append('  int i = %d; unsigned u = %du; long l = %d; unsigned long ul = %d; long long ll = %d; unsigned long long ull = %d;'
               % (val(r), r.randint(0, 99999), val(r), r.randint(0, 9999), val(r), r.randint(0, 999)))
    out.append('  float f = %d.5f; double d = %d.25; long double ld = 3.0L; _Bool bo = %d; const int ci = 4;'
               % (r.randint(-9, 9), r.randint(-9, 9), r.randint(0, 1)))
    out.append('  int arr[4] = {1, 2, 3, 4}; const char carr[3] = "ab"; char *str = "s"; int *ip = arr; const int *cip = arr; void *vp = arr;')
    out.append('  struct P pv = {1, 2}, *pp = &pv; union U uv = {5}; struct B bv = {1, 2, 3}; enum E en = EB;')
    out.append('  (void)c; (void)uc; (void)sc; (void)s; (void)us; (void)i; (void)u; (void)l; (void)ul; (void)ll; (void)ull; (void)f; (void)d; (void)ld; (void)bo; (void)str; (void)ip; (void)cip; (void)vp; (void)carr; (void)en; (void)bv; (void)uv; (void)ci; (void)gs;')
    for _ in range(r.randint(8, 30)):
        out.append('  ' + stmt(r))
        if r.random() < 0.3:
            out.append('  printf("%lx\\n", H);')
    out.append('  h(gs[1]); h(gp[0] + gp[3]); h(gq->c); h(GSP[0].x + GSP[0].y); h(GDP[1] + GDP[2]); h(sizeof GM);')
    out.append('  printf("%lx\\n", H);')
    out.append('  return 0;')
    out.append('}')
    return '\n'.join(out) + '\n'


def run(cmd, timeout=60):
    try:
        return subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return None


bad = skipped = 0
for t in range(COUNT):
    r = random.Random(SEED * 100003 + t)
    f = os.path.join(WORK, 'c%d_%d.c' % (SEED, t))
    with open(f, 'w') as fh:
        fh.write(gen_file(r))
    ub = run([CLANG, '-O0', '-Wno-everything', '-Werror=excess-initializers', '-fsanitize=undefined,address', '-fno-sanitize-recover=all', f, '-o', f + '.ub'])
    if ub is None or ub.returncode != 0:
        skipped += 1
        print('clang-fail', f, (ub.stderr[:300] if ub else 'timeout'))
        continue
    u = run([f + '.ub'])
    os.remove(f + '.ub')
    if u is None or u.returncode != 0:
        skipped += 1
        print('skip', f, (u.stderr[:200] if u else 'timeout'))
        continue
    cc = run([CCC, OPT, '-w', f, '-o', f + '.ccc'])
    if cc is None or cc.returncode != 0:
        print('ccc-fail', f, (cc.stderr[:300] if cc else 'timeout'))
        bad += 1
        continue
    b = run([f + '.ccc'])
    if b is None or b.returncode != 0 or u.stdout != b.stdout:
        print('MISMATCH', f)
        bad += 1
    else:
        os.remove(f + '.ccc')
        os.remove(f)
print('done', COUNT, 'bad', bad, 'skipped', skipped)
