#!/usr/bin/env python3
"""Floating point on runtime values, special values included, ccc vs clang.

usage: fpfuzz.py CCC CLANG WORKDIR SEED COUNT [FUNCS]   (OPT=-O2 default)

Each function takes two doubles, two floats and a long from a table that
holds NaN, infinities, signed zeros, subnormals and values near overflow.
Expressions use + - * /, negation, fabs, copysign, float/double casts,
compares (also negated, and under && || !), ?: and if/else, and integer
conversions guarded by a range test that NaN fails.  Both compilers get
-ffp-contract=off.  Results print as bits, except that every NaN prints as
"nan": its sign and payload may differ when operands are commuted.
"""
import os
import random
import subprocess
import sys

CCC, CLANG, W = sys.argv[1], sys.argv[2], sys.argv[3]
seed, count = int(sys.argv[4]), int(sys.argv[5])
per = int(sys.argv[6]) if len(sys.argv) > 6 else 6

D, F, I = 'double', 'float', 'long'


class Gen:
    def __init__(self, r):
        self.r = r

    def leaf(self, ty):
        r = self.r
        if ty == D:
            return r.choice(['x', 'y', 'x', 'y', '(double)u', '(double)v', '0.0', '-0.0', '1.0', '-1.5', '0.1',
                             '1e308', '2.0', '__builtin_inf()', '0.5'])
        if ty == F:
            return r.choice(['u', 'v', 'u', 'v', '(float)x', '(float)y', '0.0f', '-0.0f', '1.0f', '3.0f',
                             '0.1f', '3e38f', '-2.5f', '__builtin_inff()'])
        return r.choice(['k', '(long)(k >> 3)', '7', '-1', '0'])

    def fexpr(self, ty, d):
        r = self.r
        if d <= 0:
            return self.leaf(ty)
        x = r.random()
        sfx = 'f' if ty == F else ''
        if x < 0.35:
            return '(%s %s %s)' % (self.fexpr(ty, d - 1), r.choice('+-*/'), self.fexpr(ty, d - 1))
        if x < 0.42:
            return '(-%s)' % self.fexpr(ty, d - 1)
        if x < 0.48:
            return '__builtin_fabs%s(%s)' % (sfx, self.fexpr(ty, d - 1))
        if x < 0.53:
            return '__builtin_copysign%s(%s, %s)' % (sfx, self.fexpr(ty, d - 1), self.fexpr(ty, d - 1))
        if x < 0.58:
            return '__builtin_sqrt%s(%s)' % (sfx, self.fexpr(ty, d - 1))
        if x < 0.7:
            return '(%s ? %s : %s)' % (self.cond(d - 1), self.fexpr(ty, d - 1), self.fexpr(ty, d - 1))
        if x < 0.8:
            other = F if ty == D else D
            return '((%s)%s)' % (ty, self.fexpr(other, d - 1))
        if x < 0.88:
            return '((%s)%s)' % (ty, self.iexpr(d - 1))
        if x < 0.94:
            # Identities a compiler may only use when special values allow.
            e = self.fexpr(ty, d - 1)
            return r.choice(['(%s - %s)', '(%s * 0.0)', '(%s + 0.0)', '(%s - 0.0)', '(%s * -1.0)', '(%s / 1.0)',
                             '(0.0 - %s)', '(%s * 1.0)']).replace('%s', e, 1).replace('%s', e)
        return self.leaf(ty)

    def cond(self, d):
        r = self.r
        x = r.random()
        if d > 0 and x < 0.2:
            return '(%s %s %s)' % (self.cond(d - 1), r.choice(['&&', '||']), self.cond(d - 1))
        if d > 0 and x < 0.3:
            return '(!%s)' % self.cond(d - 1)
        ty = r.choice([D, D, F])
        a = self.fexpr(ty, max(d - 1, 0))
        b = self.fexpr(ty, max(d - 1, 0)) if r.random() < 0.8 else a
        op = r.choice(['<', '<=', '>', '>=', '==', '!='])
        if r.random() < 0.3:
            return '(!(%s %s %s))' % (a, op, b)
        if r.random() < 0.15:
            return r.choice(['__builtin_isnan(%s)', '__builtin_isinf(%s)', '__builtin_isfinite(%s)',
                             '__builtin_signbit(%s)', '__builtin_isunordered(%s, %s)']).replace('%s', a, 1).replace('%s', b)
        return '(%s %s %s)' % (a, op, b)

    def iexpr(self, d):
        r = self.r
        if d <= 0 or r.random() < 0.3:
            return self.leaf(I)
        if r.random() < 0.5:
            # A conversion to an integer, in range or not done at all.
            ty = r.choice([D, F])
            e = self.fexpr(ty, d - 1)
            it, lo, hi = r.choice([('long', '-9.2e18', '9.2e18'), ('int', '-2.1e9', '2.1e9'),
                                   ('unsigned', '-0.99', '4.2e9'), ('unsigned long', '-0.99', '1.8e19'),
                                   ('short', '-32768.5', '32767.5')])
            t = self.r.randint(0, 999)
            return '({ %s c%d = %s; (c%d > %s && c%d < %s) ? (long)(%s)c%d : -77L; })' % (
                ty, t, e, t, lo, t, hi, it, t)
        return '(%s ? %s : %s)' % (self.cond(d - 1), self.iexpr(d - 1), self.iexpr(d - 1))


def function(r, k):
    g = Gen(r)
    lines = ['__attribute__((noinline)) static void f%d(double x, double y, float u, float v, long k) {' % k]
    for j in range(r.randint(1, 4)):
        x = r.random()
        if x < 0.4:
            lines.append('    pd(%d, %s);' % (j, g.fexpr(D, r.randint(1, 4))))
        elif x < 0.6:
            lines.append('    pf(%d, %s);' % (j, g.fexpr(F, r.randint(1, 4))))
        elif x < 0.8:
            lines.append('    pi(%d, %s);' % (j, g.iexpr(r.randint(1, 4))))
        else:
            lines.append('    if (%s) pi(%d, 1); else pd(%d, %s);' % (g.cond(r.randint(1, 3)), j, j, g.fexpr(D, 2)))
    lines.append('}')
    return '\n'.join(lines)


DV = ['0.0', '-0.0', '1.0', '-1.0', '0.5', '-2.5', '3.0', '1e-310', '-1e-310', '2.2250738585072014e-308',
      '1.7976931348623157e308', '-1.7976931348623157e308', '1e30', '4294967295.5', '-2147483648.5',
      '9.3e18', '1e10', '0.1', '__builtin_inf()', '-__builtin_inf()', '__builtin_nan("")', '-__builtin_nan("")',
      '65535.75', '-32768.0', '3.4028235e38', '1e-45']
FV = ['0.0f', '-0.0f', '1.0f', '-1.0f', '0.5f', '1e-40f', '-1e-40f', '3.4028235e38f', '-3.4028235e38f',
      '16777217.0f', '0.1f', '__builtin_inff()', '-__builtin_inff()', '__builtin_nanf("")', '2.5f', '-7.0f']
IV = ['0', '1', '-1', '7', '9007199254740993', '-9223372036854775807', '4294967296', '255']


def program(r, nf):
    out = ['#include <stdio.h>', '#include <string.h>', '#include <stdint.h>', '',
           'static unsigned long long bits(double d) { unsigned long long b; memcpy(&b, &d, 8); return b; }',
           'static unsigned fbits(float f) { unsigned b; memcpy(&b, &f, 4); return b; }',
           'static void pd(int j, double d) { if (d != d) printf("%d nan\\n", j); else printf("%d %llx\\n", j, bits(d)); }',
           'static void pf(int j, float f) { if (f != f) printf("%d nanf\\n", j); else printf("%d %x\\n", j, fbits(f)); }',
           'static void pi(int j, long i) { printf("%d i%ld\\n", j, i); }', '']
    for k in range(nf):
        out.append(function(r, k))
    out.append('static const double dv[] = { %s };' % ', '.join(DV))
    out.append('static const float fv[] = { %s };' % ', '.join(FV))
    out.append('static const long iv[] = { %s };' % ', '.join(v + 'L' for v in IV))
    out.append('int main(void) {')
    out.append('    unsigned nd = sizeof dv / sizeof dv[0], nfv = sizeof fv / sizeof fv[0];')
    out.append('    for (unsigned i = 0; i < nd; i++) for (unsigned j = 0; j < nd; j += 2) {')
    out.append('        double x = dv[i], y = dv[j]; float u = fv[(i + j) % nfv], v = fv[(i * 3 + j) % nfv]; long k = iv[(i + 2 * j) % 8];')
    out.append('        printf("== %u %u\\n", i, j);')
    for k in range(nf):
        out.append('        f%d(x, y, u, v, k);' % k)
    out.append('    }')
    out.append('    return 0;\n}')
    return '\n'.join(out) + '\n'


def run(cc, src, exe, opt):
    c = subprocess.run([cc, opt, '-w', '-ffp-contract=off', '-o', exe, src], capture_output=True, text=True)
    if c.returncode != 0:
        return 'COMPILE FAILED: ' + c.stderr[:400]
    try:
        p = subprocess.run([exe], capture_output=True, text=True, timeout=30)
    except subprocess.TimeoutExpired:
        return 'TIMEOUT'
    return p.stdout + ('' if p.returncode == 0 else 'EXIT %d' % p.returncode)


def main():
    rnd = random.Random(seed)
    opt = os.environ.get('OPT', '-O2')
    found = 0
    for k in range(count):
        src = os.path.join(W, 'p%d_%d.c' % (seed, k))
        open(src, 'w').write(program(rnd, per))
        if os.environ.get('SAN'):
            exe = os.path.abspath(os.path.join(W, 'xs'))
            c = subprocess.run([CLANG, '-O1', '-w', '-fsanitize=undefined,float-cast-overflow', '-fno-sanitize-recover=all',
                                '-o', exe, src], capture_output=True, text=True)
            p = subprocess.run([exe], capture_output=True, text=True, timeout=120) if c.returncode == 0 else c
            print('SAN', src, c.returncode, p.returncode, p.stderr[:600])
            continue
        ref = run(CLANG, src, os.path.abspath(os.path.join(W, 'xk')), opt)
        if ref.startswith('COMPILE') or ref == 'TIMEOUT':
            print('clang failed', src, ref[:300])
            continue
        got = run(CCC, src, os.path.abspath(os.path.join(W, 'xc')), opt)
        if got != ref:
            r0 = run(CLANG, src, os.path.abspath(os.path.join(W, 'x0')), '-O0')
            if got == r0:
                print('CLANG-WRONG', src)
                continue
            found += 1
            d = [(p, q) for p, q in zip(ref.split('\n'), got.split('\n')) if p != q]
            print('MISMATCH', src, d[:2] if d else got[:300])
        else:
            os.remove(src)
    print('seed %d: %d of %d files mismatched' % (seed, found, count))


main()
