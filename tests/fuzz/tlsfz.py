#!/usr/bin/env python3
"""Thread-local storage fuzzer: _Thread_local / __thread globals (static,
external, uninitialized, initialized), static locals, of scalar, int128,
float, struct (with bit-fields), union and array types; reads, writes,
compound assignments, ++/--, addresses taken and passed to functions,
pointers kept across calls, accesses in loops, and the same work run in
several pthreads, each of which must see its own copy (results joined and
printed in order).  Main's copies are checked after the threads too.
usage: tlsfz.py CCC CLANG WORK SEED COUNT   (env OPT, default -O2)"""
import os
import random
import subprocess
import sys

CCC, CLANG, WORK, SEED, COUNT = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]), int(sys.argv[5])
OPT = os.environ.get('OPT', '-O2')
os.makedirs(WORK, exist_ok=True)

HDR = r'''#include <stdio.h>
#include <pthread.h>
#include <string.h>
typedef unsigned long u64;
struct B { unsigned a : 3; int b : 9; u64 c : 33; };
struct T { char c; int i; long l; short s[3]; };
union U { int i; float f; char b[8]; };
static u64 mix(u64 h, u64 v) { return (h ^ v) * 1099511628211UL; }
__attribute__((noinline)) static void bump(int *p, int k) { *p += k; }
__attribute__((noinline)) static long rd(const long *p) { return *p; }
__attribute__((noinline)) static void tw(struct T *t, int k) { t->i += k; t->s[k & 1] ^= (short)k; }
'''

SCALARS = [('char', 'c', lambda r: str(r.randint(-100, 100))), ('unsigned char', 'uc', lambda r: str(r.randint(0, 255))),
           ('short', 's', lambda r: str(r.randint(-30000, 30000))), ('int', 'i', lambda r: str(r.randint(-10**9, 10**9))),
           ('unsigned', 'u', lambda r: '%du' % r.randint(0, 4 * 10**9)), ('long', 'l', lambda r: '%dL' % r.randint(-10**15, 10**15)),
           ('u64', 'ul', lambda r: '%dUL' % r.randint(0, 10**18)), ('float', 'f', lambda r: '%d.5f' % r.randint(-99, 99)),
           ('double', 'd', lambda r: '%d.25' % r.randint(-9999, 9999)), ('__int128', 'q', lambda r: '((__int128)%dL << 40)' % r.randint(-999, 999)),
           ('_Bool', 'bo', lambda r: str(r.randint(0, 1)))]


def hexpr(ty, e):
    if ty in ('float', 'double'):
        return '(u64)(long)((%s) * 4)' % e
    if ty == '__int128':
        return '(u64)(%s) ^ (u64)((%s) >> 64)' % (e, e)
    return '(u64)(%s)' % e


def gen(r):
    out = [HDR]
    tvars = []  # (name, kind, type)
    kw = lambda: r.choice(['_Thread_local', '__thread', 'static _Thread_local', 'static __thread'])
    for n in range(r.randint(3, 9)):
        kind = r.choice(['scalar'] * 4 + ['struct', 'bits', 'union', 'array'])
        nm = 't%d' % n
        if kind == 'scalar':
            ty, _, v = r.choice(SCALARS)
            init = '' if r.random() < 0.3 else ' = ' + v(r)
            out.append('%s %s %s%s;' % (kw(), ty, nm, init))
        elif kind == 'struct':
            ty = 'struct T'
            init = '' if r.random() < 0.3 else ' = {%d, %d, %dL, {%d, %d}}' % (r.randint(-9, 9), r.randint(-999, 999), r.randint(-99999, 99999), r.randint(-99, 99), r.randint(-99, 99))
            out.append('%s struct T %s%s;' % (kw(), nm, init))
        elif kind == 'bits':
            ty = 'struct B'
            init = '' if r.random() < 0.3 else ' = {%d, %d, %dUL}' % (r.randint(0, 7), r.randint(-255, 255), r.randint(0, 2**33 - 1))
            out.append('%s struct B %s%s;' % (kw(), nm, init))
        elif kind == 'union':
            ty = 'union U'
            init = '' if r.random() < 0.3 else ' = {%d}' % r.randint(-9999, 9999)
            out.append('%s union U %s%s;' % (kw(), nm, init))
        else:
            et = r.choice(['int', 'long', 'char', 'short'])
            ln = r.randint(1, 9)
            ty = (et, ln)
            init = '' if r.random() < 0.3 else ' = {%s}' % ', '.join(str(r.randint(-99, 99)) for _ in range(r.randint(1, ln)))
            out.append('%s %s %s[%d]%s;' % (kw(), et, nm, ln, init))
        tvars.append((nm, kind, ty))
    # static locals inside work()
    body = []
    locs = []
    for n in range(r.randint(0, 3)):
        ty, _, v = r.choice([s for s in SCALARS if s[0] not in ('__int128', '_Bool')])
        nm = 'sl%d' % n
        body.append('  static %s %s %s = %s;' % (r.choice(['_Thread_local', '__thread']), ty, nm, v(r)))
        locs.append((nm, 'scalar', ty))
    allv = tvars + locs

    def scal_lv(nm, kind, ty):
        if kind == 'scalar':
            return nm, ty
        if kind == 'struct':
            f, t = r.choice([('c', 'char'), ('i', 'int'), ('l', 'long'), ('s[%d]' % r.randint(0, 2), 'short')])
            return '%s.%s' % (nm, f), t
        if kind == 'bits':
            f, t = r.choice([('a', 'unsigned'), ('b', 'int'), ('c', 'u64')])
            return '%s.%s' % (nm, f), t + ':bf'
        if kind == 'union':
            return r.choice([('%s.i' % nm, 'int'), ('%s.b[%d]' % (nm, r.randint(0, 7)), 'char')])
        et, ln = ty
        return '%s[%s]' % (nm, r.choice([str(r.randint(0, ln - 1)), '(k %% %d)' % ln])), et

    def stmt(depth=0):
        nm, kind, ty = r.choice(allv)
        lv, t = scal_lv(nm, kind, ty)
        isfp = t in ('float', 'double')
        c = r.random()
        if c < 0.2:
            op = r.choice(['+=', '-=', '*='] + ([] if isfp or t == '_Bool' else ['^=', '|=', '&=', '<<= 1 + 0 *', '>>= 1 + 0 *']))
            return '%s %s %s;' % (lv, op, r.choice(['k', 'tid', '3', '(k * 7 + tid)']))
        if c < 0.3 and not isfp and ':bf' not in t and t != '_Bool':
            return '%s%s;' % (lv, r.choice(['++', '--'])) if r.random() < .5 else '%s%s;' % (r.choice(['++', '--']), lv)
        if c < 0.4:
            return '%s = %s + tid;' % (lv, r.choice(['k', '(k << 2)', str(r.randint(-50, 50))]))
        if c < 0.5 and t == 'int' and ':bf' not in t:
            return 'bump(&%s, k + tid);' % lv
        if c < 0.55 and t == 'long' and kind == 'scalar':
            return 'h = mix(h, (u64)rd(&%s));' % lv
        if c < 0.62 and kind == 'struct':
            return 'tw(&%s, k + tid);' % nm
        if c < 0.7 and t in ('int', 'long', 'char', 'short') and ':bf' not in t:
            return '{ %s *p = &%s; for (int j = 0; j < 3 + tid; j++) *p += j; }' % (t, lv)
        if c < 0.78 and kind == 'array':
            et, ln = ty
            return 'for (int j = 0; j < %d; j++) %s[j] += j * (tid + 1);' % (ln, nm)
        if c < 0.83 and depth < 2:
            return 'if ((k + tid) & 1) { %s } else { %s }' % (stmt(depth + 1), stmt(depth + 1))
        return 'h = mix(h, %s);' % hexpr(t.replace(':bf', ''), lv)

    out.append('__attribute__((noinline)) static u64 work(int tid, int k) {')
    out += body
    out.append('  u64 h = 1469598103934665603UL;')
    for _ in range(r.randint(6, 25)):
        out.append('  ' + stmt())
    for nm, kind, ty in allv:
        if kind == 'scalar':
            out.append('  h = mix(h, %s);' % hexpr(ty, nm))
        elif kind == 'struct':
            out.append('  h = mix(h, (u64)%s.c ^ (u64)%s.i ^ (u64)%s.l ^ (u64)%s.s[0] ^ (u64)%s.s[1] << 7 ^ (u64)%s.s[2] << 14);' % ((nm,) * 6))
        elif kind == 'bits':
            out.append('  h = mix(h, (u64)%s.a ^ (u64)%s.b << 3 ^ (u64)%s.c << 12);' % ((nm,) * 3))
        elif kind == 'union':
            out.append('  h = mix(h, (u64)%s.i);' % nm)
        else:
            out.append('  for (int j = 0; j < %d; j++) h = mix(h, (u64)%s[j]);' % (ty[1], nm))
    out.append('  return h;')
    out.append('}')
    nthr = r.randint(1, 4)
    rounds = r.randint(1, 3)
    out.append('static u64 res[8];')
    out.append('static void *thr(void *a) { int tid = (int)(long)a; u64 h = 0; for (int k = 0; k < %d; k++) h = mix(h, work(tid, k)); res[tid] = h; return 0; }' % rounds)
    out.append('int main(void) {')
    out.append('  u64 h0 = 0; for (int k = 0; k < %d; k++) h0 = mix(h0, work(0, k)); printf("main %%lx\\n", h0);' % rounds)
    out.append('  pthread_t th[%d];' % nthr)
    out.append('  for (long i = 0; i < %d; i++) pthread_create(&th[i], 0, thr, (void *)(i + 1));' % nthr)
    out.append('  for (int i = 0; i < %d; i++) { pthread_join(th[i], 0); printf("thr %%d %%lx\\n", i, res[i + 1]); }' % nthr)
    out.append('  u64 h1 = 0; for (int k = 0; k < %d; k++) h1 = mix(h1, work(0, k)); printf("main2 %%lx\\n", h1);' % rounds)
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
    f = os.path.join(WORK, 'tl%d_%d.c' % (SEED, t))
    with open(f, 'w') as fh:
        fh.write(gen(r))
    ub = run([CLANG, '-O0', '-w', '-fsanitize=undefined', '-fno-sanitize-recover=all', f, '-o', f + '.ub'])
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
