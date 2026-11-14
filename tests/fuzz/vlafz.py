#!/usr/bin/env python3
"""alloca / VLA fuzzer.  Random functions built from fragments: VLAs of
scalar, struct and over-aligned types in nested scopes and loops (stack
must be released each iteration: loops run long enough to overflow the
stack if it is not), goto/break/continue/return out of VLA scopes,
backward goto over a VLA declaration, multi-dimensional VLAs, typedef'd
VLA types whose size is fixed at the typedef, pointers to VLAs and their
arithmetic, VLA parameters, sizeof of VLAs with side effects, alloca in
loops (kept until return), alloca alignment, calls with stack arguments
and recursion while VLAs are live, and alignment of everything.
usage: vlafz.py CCC CLANG WORK SEED COUNT   (env OPT, default -O2)"""
import os
import random
import subprocess
import sys

CCC, CLANG, WORK, SEED, COUNT = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]), int(sys.argv[5])
OPT = os.environ.get('OPT', '-O2')
os.makedirs(WORK, exist_ok=True)

HDR = r'''#include <stdio.h>
#include <string.h>
#include <alloca.h>
#include <stdint.h>
typedef unsigned long u64;
struct S { char c; int i; double d; short s; };
struct __attribute__((aligned(32))) A32 { int x; };
static u64 mix(u64 h, u64 v) { return (h ^ v) * 1099511628211UL; }
static int misal, ncalls;
__attribute__((noinline)) static int cnt(int x) { ncalls++; return x; }
#define AL(p, a) (misal |= (((uintptr_t)(p)) & ((a) - 1)) != 0)
__attribute__((noinline)) static u64 many(long a, long b, long c, long d, long e, long f, long g, long h, long i, long j, int k, double x, char *p) {
  return mix(mix(a + b * 3 + c * 5 + d * 7 + e * 11 + f * 13 + g * 17 + h * 19, i * 23 + j * 29 + k), (u64)(long)(x * 4) + (u64)p[0]);
}
__attribute__((noinline)) static u64 sumv(int n, int m, int a[n][m]) {
  u64 h = 0; for (int i = 0; i < n; i++) for (int j = 0; j < m; j++) h = mix(h, (u64)a[i][j]); return h;
}
__attribute__((noinline)) static u64 sump(int n, long (*p)[n], int rows) {
  u64 h = 0; for (int i = 0; i < rows; i++) for (int j = 0; j < n; j++) h = mix(h, (u64)p[i][j]); return h;
}
__attribute__((noinline)) static u64 rec(int d, int n) {
  if (d == 0) return 1;
  int v[n + d]; for (int i = 0; i < n + d; i++) v[i] = i * d;
  char *q = alloca(d * 16 + 1); q[0] = (char)d; AL(v, 4);
  u64 h = rec(d - 1, n);
  for (int i = 0; i < n + d; i++) h = mix(h, (u64)v[i]);
  return mix(h, (u64)q[0]);
}
'''

ETYS = [('int', 4), ('long', 8), ('char', 1), ('short', 2), ('double', 8), ('struct S', 8), ('struct A32', 32), ('float', 4), ('__int128', 16), ('long double', 8)]


def setx(ty, nm, idx, val):
    if ty == 'struct S':
        return '%s[%s].c = (char)(%s); %s[%s].i = %s; %s[%s].d = (%s) * .5; %s[%s].s = (short)(%s);' % (nm, idx, val, nm, idx, val, nm, idx, val, nm, idx, val)
    if ty == 'struct A32':
        return '%s[%s].x = %s;' % (nm, idx, val)
    return '%s[%s] = (%s)(%s);' % (nm, idx, ty, val)


def getx(ty, e):
    if ty == 'struct S':
        return '((u64)%s.c ^ (u64)%s.i << 8 ^ (u64)(long)(%s.d * 2) << 20 ^ (u64)%s.s << 40)' % (e, e, e, e)
    if ty == 'struct A32':
        return '(u64)%s.x' % e
    if ty in ('double', 'float', 'long double'):
        return '(u64)(long)(%s * 2)' % e
    if ty == '__int128':
        return '((u64)%s ^ (u64)(%s >> 64))' % (e, e)
    return '(u64)%s' % e


class G:
    def __init__(self, r):
        self.r = r
        self.n = 0

    def nm(self, p):
        self.n += 1
        return '%s%d' % (p, self.n)

    def frag(self, depth):
        r = self.r
        c = int(os.environ['FRAG']) if 'FRAG' in os.environ else r.randrange(16)
        if depth > 2:
            c = r.choice([0, 1, 5, 9])
        if c == 0:  # plain 1-d VLA
            ty, al = r.choice(ETYS)
            a = self.nm('a')
            ln = r.choice(['n', 'm', '(n + 1)', '(m * 2 + 1)', '(n %% 5 + 1)', '((n ^ m) & 7) + 1'])
            return ['{ int L = %s; %s %s[L]; AL(%s, %d); for (int i = 0; i < L; i++) { %s }' % (ln, ty, a, a, al, setx(ty, a, 'i', 'i * 3 + n')),
                    '  h = mix(h, sizeof %s); for (int i = 0; i < L; i++) h = mix(h, %s); }' % (a, getx(ty, '%s[i]' % a))]
        if c == 1:  # VLA in a long loop: must be freed each iteration
            ty, al = r.choice(ETYS[:5])
            a = self.nm('a')
            return ['for (int it = 0; it < 20000; it++) { %s %s[256 + (it & 15) + n]; AL(%s, %d); %s h += %s; }' % (ty, a, a, al, setx(ty, a, 'it & 255', 'it'), getx(ty, '%s[it & 255]' % a))]
        if c == 2:  # 2-d VLA, passed as VLA parameter
            a = self.nm('m')
            return ['{ int R = n %% 4 + 1, C = m %% 5 + 1; int %s[R][C]; for (int i = 0; i < R; i++) for (int j = 0; j < C; j++) %s[i][j] = i * 10 + j + n;' % (a, a),
                    '  h = mix(h, sizeof %s); h = mix(h, sizeof %s[0]); h = mix(h, sumv(R, C, %s)); h = mix(h, (u64)(&%s[R - 1][C - 1] - &%s[0][0])); }' % (a, a, a, a, a)]
        if c == 3:  # typedef VLA type: size fixed at typedef even if n changes
            t = self.nm('T')
            return ['{ int k = n %% 6 + 2; typedef int %s[k]; k += 5; %s x; h = mix(h, sizeof x); h = mix(h, sizeof(%s)); for (int i = 0; i < (int)(sizeof x / sizeof x[0]); i++) x[i] = i * k; for (int i = 0; i < (int)(sizeof x / sizeof x[0]); i++) h = mix(h, (u64)x[i]); }' % (t, t, t)]
        if c == 4:  # pointer to VLA, arithmetic and passing
            p = self.nm('p')
            return ['{ int W = m %% 7 + 1; long buf[4 * 8]; long (*%s)[W] = (long (*)[W])buf; for (int i = 0; i < 4; i++) for (int j = 0; j < W; j++) %s[i][j] = i * 100 + j;' % (p, p),
                    '  h = mix(h, (u64)((char *)(%s + 3) - (char *)%s)); h = mix(h, sizeof *%s); h = mix(h, sump(W, %s, 4)); %s++; h = mix(h, (u64)(*%s)[0]); }' % (p, p, p, p, p, p)]
        if c == 5:  # alloca, alignment, in a loop (kept)
            a = self.nm('q')
            k = r.randint(1, 6)
            return ['{ char *%s[%d]; for (int i = 0; i < %d; i++) { %s[i] = alloca((n + i) %% 40 + 1); AL(%s[i], 16); memset(%s[i], i + 1, (n + i) %% 40 + 1); }' % (a, k, k, a, a, a),
                    '  for (int i = 0; i < %d; i++) h = mix(h, (u64)%s[i][(n + i) %% 40]); }' % (k, a)]
        if c == 6:  # goto out of / backward over VLA scope, loop many times
            lb = self.nm('L')
            return ['{ int cnt = 0; %s: { double v[300 + (cnt & 7)]; v[cnt %% 300] = cnt; h = mix(h, (u64)(long)v[cnt %% 300]); if (++cnt < 9000) goto %s; } }' % (lb, lb)]
        if c == 7:  # break / continue out of nested VLA scopes
            return ['for (int it = 0; it < 6000; it++) { int a[n %% 9 + 100]; a[0] = it; { long b[m %% 5 + 200]; b[1] = a[0] * 2; if (it %% 3 == 0) continue; if (it == 5990 - (n & 7)) break; h += (u64)b[1]; } }']
        if c == 8:  # sizeof with side effects
            return ["{ int k = n %% 5 + 1; h = mix(h, sizeof(int[k++])); h = mix(h, (u64)k); int z[k][cnt(m %% 4 + 1)]; h = mix(h, sizeof z[k++]); h = mix(h, (u64)k); h = mix(h, sizeof(z[cnt(1)])); long (*pz)[cnt(k)] = (long (*)[cnt(k + 1)])z; h = mix(h, sizeof *(pz + k++)); h = mix(h, (u64)k); h = mix(h, sizeof(char[cnt(k)][3])); h = mix(h, (u64)ncalls); (void)pz; }"]
        if c == 9:  # calls with stack args while VLA live
            a = self.nm('c')
            return ['{ char %s[n %% 13 + 3]; memset(%s, 7, sizeof %s); long w[m %% 3 + 2]; w[0] = n; w[1] = m; h = mix(h, many(w[0], w[1], 3, 4, 5, 6, 7, 8, 9, 10, (int)sizeof %s, 1.5, %s)); }' % (a, a, a, a, a)]
        if c == 10:  # recursion with VLAs
            return ['h = mix(h, rec(n %% 6 + 1, m %% 9 + 1));']
        if c == 11:  # return from inside VLA scope via helper flag
            return ['{ int k = 0; while (1) { int v[n %% 11 + 1]; v[0] = k; h = mix(h, (u64)v[0]); if (++k > m %% 7) break; } }']
        if c == 12:  # VLA of VLA rows through pointer arith, 3-d
            a = self.nm('t')
            return ['{ int X = n %% 3 + 1, Y = m %% 3 + 2, Z = (n + m) %% 4 + 1; short %s[X][Y][Z]; for (int i = 0; i < X; i++) for (int j = 0; j < Y; j++) for (int k = 0; k < Z; k++) %s[i][j][k] = (short)(i * 100 + j * 10 + k);' % (a, a),
                    '  h = mix(h, sizeof %s[0]); h = mix(h, sizeof %s[0][0]); short (*pp)[Y][Z] = %s + (X - 1); h = mix(h, (u64)pp[0][Y - 1][Z - 1]); h = mix(h, (u64)((char *)&%s[X - 1] - (char *)%s)); }' % (a, a, a, a, a)]
        if c == 13:  # nested block with frags inside, in a loop
            body = self.frag(depth + 1) + self.frag(depth + 1)
            return ['for (int it2 = 0; it2 < %d; it2++) { int pad[it2 + 1]; pad[it2] = it2; h = mix(h, (u64)pad[it2]);' % r.randint(1, 3)] + ['  ' + x for x in body] + ['}']
        if c == 14:  # VLA declared after a computed size, size var changed after
            return ['{ int k = m %% 8 + 1; int v[k]; k = 1000; h = mix(h, sizeof v); for (int i = 0; i < (int)(sizeof v / sizeof *v); i++) v[i] = i; h = mix(h, (u64)v[sizeof v / sizeof *v - 1]); h = mix(h, (u64)k); }']
        # 15: alloca in a loop of a callee-like block plus VLA
        return ['{ for (int it = 0; it < 4; it++) { long *p = alloca(sizeof(long) * (it + 1)); AL(p, 8); p[it] = it * n; int v[it + m %% 3 + 1]; v[it] = (int)p[it]; h = mix(h, (u64)v[it]); } }']


def gen(r):
    out = [HDR]
    g = G(r)
    fns = []
    for fi in range(r.randint(2, 5)):
        fn = 'f%d' % fi
        out.append('%sstatic u64 %s(int n, int m) {' % (r.choice(['__attribute__((noinline)) ', '']), fn))
        out.append('  u64 h = 0;')
        for _ in range(r.randint(2, 7)):
            out += ['  ' + x for x in g.frag(0)]
        out.append('  return h;')
        out.append('}')
        fns.append(fn)
    out.append('int main(void) {')
    for fn in fns:
        for _ in range(r.randint(1, 3)):
            out.append('  printf("%s %%lx\\n", %s(%d, %d));' % (fn, fn, r.randint(1, 60), r.randint(1, 60)))
    out.append('  printf("misal %d\\n", misal);')
    out.append('  return 0;')
    out.append('}')
    return ('\n'.join(out) + '\n').replace('%%', '%')


def run(cmd, timeout=60):
    try:
        return subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return None


bad = skipped = 0
for t in range(COUNT):
    r = random.Random(SEED * 100003 + t)
    f = os.path.join(WORK, 'va%d_%d.c' % (SEED, t))
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
        print('MISMATCH', f, 'rc', None if b is None else b.returncode)
        bad += 1
    else:
        os.remove(f + '.ccc')
        os.remove(f)
print('done', COUNT, 'bad', bad, 'skipped', skipped)
