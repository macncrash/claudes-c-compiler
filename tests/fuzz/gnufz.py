#!/usr/bin/env python3
"""GNU extension fuzzer: integer builtins (clz/ctz/popcount/parity/ffs/clrsb/
bswap, l/ll forms) on runtime and constant operands, overflow builtins
(add/sub/mul into every result type), __builtin_expect, choose_expr,
types_compatible_p, offsetof, ?: with omitted middle, statement expressions,
typeof/__auto_type, range designators in static and automatic arrays and
struct members, vector_size vectors (element ops, broadcast, compares,
subscripts), the cleanup attribute and zero-length arrays.
Every result is hashed. Reference: clang -O0 with UBSan+ASan.
usage: gnufz.py CCC CLANG WORK SEED COUNT   (env OPT, default -O2)"""
import os
import random
import subprocess
import sys

CCC, CLANG, WORK, SEED, COUNT = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]), int(sys.argv[5])
OPT = os.environ.get('OPT', '-O2')
os.makedirs(WORK, exist_ok=True)

ITYPES = ['signed char', 'unsigned char', 'short', 'unsigned short', 'int', 'unsigned',
          'long', 'unsigned long', 'long long', 'unsigned long long']
SFX = {'': 'unsigned', 'l': 'unsigned long', 'll': 'unsigned long long'}
SSFX = {'': 'int', 'l': 'long', 'll': 'long long'}


def leaf(r):
    c = r.random()
    if c < 0.35:
        return r.choice(['a', 'b', 'c', 'd'])
    if c < 0.55:
        return '(%s)%s' % (r.choice(ITYPES), r.choice(['a', 'b', 'c', 'd']))
    if c < 0.8:
        return str(r.choice([0, 1, 2, 3, 7, 8, 15, 16, 31, 32, 63, 64, 127, 128, 255, 256, 0x7fff, 0x8000,
                             0xffff, 0x7fffffff, 0x80000000, 0xffffffff]) ) + r.choice(['', 'u', 'll', 'ull'])
    return r.choice(['-1', '-2', '-128', '(-0x7fffffff - 1)', '0x8000000000000000ull', '0x7fffffffffffffffll',
                     '0xffffffffffffffffull', '0x123456789abcdefull'])


def iexpr(r, depth=0):
    """An integer expression, UB-free (unsigned arithmetic, guarded builtins)."""
    if depth > 2 or r.random() < 0.3:
        return leaf(r)
    c = r.randint(0, 14)
    x = iexpr(r, depth + 1)
    s = r.choice(['', 'l', 'll'])
    if c == 0:
        return '__builtin_popcount%s((%s)(%s))' % (s, SFX[s], x)
    if c == 1:
        return '__builtin_clz%s((%s)(%s) | 1)' % (s, SFX[s], x)
    if c == 2:
        return '__builtin_ctz%s((%s)(%s) | ((%s)1 << %d))' % (s, SFX[s], x, SFX[s], r.randint(0, 31))
    if c == 3:
        return '__builtin_parity%s((%s)(%s))' % (s, SFX[s], x)
    if c == 4:
        return '__builtin_ffs%s((%s)(%s))' % (s, SSFX[s], x)
    if c == 5:
        return '__builtin_clrsb%s((%s)(%s))' % (s, SSFX[s], x)
    if c == 6:
        n = r.choice([16, 32, 64])
        t = {16: 'unsigned short', 32: 'unsigned', 64: 'unsigned long long'}[n]
        return '__builtin_bswap%d((%s)(%s))' % (n, t, x)
    if c == 7:
        return '__builtin_expect(%s, %s)' % (x, leaf(r))
    if c == 8:
        return '((%s) ?: (%s))' % (x, iexpr(r, depth + 1))
    if c == 9:
        return '({ unsigned long long t_%d = %s; t_%d ^ (t_%d >> 3); })' % (depth, x, depth, depth)
    if c == 10:
        return '__builtin_choose_expr(%d, (%s), (%s))' % (r.randint(0, 1), x, iexpr(r, depth + 1))
    if c == 11:
        return '({ __typeof__(%s) v_%d = %s; v_%d + (__typeof__(v_%d))1; })' % (x, depth, x, depth, depth)
    if c == 12:
        op = r.choice(['+', '-', '*', '&', '|', '^'])
        return '((unsigned long long)(%s) %s (unsigned long long)(%s))' % (x, op, iexpr(r, depth + 1))
    if c == 13:
        return '((unsigned long long)(%s) >> %d)' % (x, r.randint(0, 63))
    return '__builtin_types_compatible_p(__typeof__(%s), %s)' % (x, r.choice(ITYPES))


def overflow(r, k):
    op = r.choice(['add', 'sub', 'mul'])
    rt = r.choice(ITYPES)
    x, y = iexpr(r, 1), iexpr(r, 1)
    tx, ty = r.choice(ITYPES), r.choice(ITYPES)
    return ['{ %s r_; _Bool o_ = __builtin_%s_overflow((%s)(%s), (%s)(%s), &r_); H(o_); H(r_); }' % (
        rt, op, tx, x, ty, y)]


def ranges(r, k, static):
    n = r.randint(4, 24)
    t = r.choice(['int', 'unsigned char', 'short', 'long long', 'double'])
    # Disjoint, in-bounds ranges in any order (clang and GCC differ on
    # overriding an initialized element and on excess positional items).
    items = []
    used = set()
    for _ in range(r.randint(1, 5)):
        a = r.randint(0, n - 1)
        b = r.randint(a, min(n - 1, a + 6))
        if any(i in used for i in range(a, b + 2)):
            continue
        used.update(range(a, b + 1))
        v = r.randint(-300, 300)
        if r.random() < 0.6:
            items.append('[%d ... %d] = %d' % (a, b, v))
        else:
            items.append('[%d] = %d' % (a, v))
            used.difference_update(range(a + 1, b + 1))
            b = a
        if b + 1 < n and b + 1 not in used and r.random() < 0.3:
            used.add(b + 1)
            items.append('%d' % r.randint(-9, 9))  # positional after a designator
    decl = '%s%s q%d[%d] = { %s };' % ('static ' if static else '', t, k, n, ', '.join(items))
    use = 'for (int i = 0; i < %d; i++) H((long long)q%d[i]);' % (n, k)
    return decl, use


def structrange(r, k):
    n = r.randint(2, 8)
    a = r.randint(0, n - 1)
    b = r.randint(a, n - 1)
    decl = 'struct { char c; int v[%d]; short s; } sr%d = { %d, { [%d ... %d] = %d }, %d };' % (
        n, k, r.randint(-5, 5), a, b, r.randint(-99, 99), r.randint(-99, 99))
    use = 'H(sr%d.c); H(sr%d.s); for (int i = 0; i < %d; i++) H(sr%d.v[i]); H(__builtin_offsetof(__typeof__(sr%d), s));' % (
        k, k, n, k, k)
    return decl, use


VT = [('int', 4, 16), ('unsigned', 4, 16), ('short', 8, 16), ('unsigned char', 16, 16),
      ('long long', 2, 16), ('int', 2, 8), ('float', 4, 16), ('double', 2, 16)]


def vectors(r, k):
    et, n, size = r.choice(VT)
    vt = 'v%d_t' % k
    td = 'typedef %s %s __attribute__((vector_size(%d)));' % (et, vt, size)
    flt = et in ('float', 'double')
    def vinit():
        if flt:
            return '(%s){ %s }' % (vt, ', '.join('%d.5' % r.randint(-9, 9) for _ in range(n)))
        return '(%s){ %s }' % (vt, ', '.join('(%s)(c * %d + %d)' % (et, r.randint(-5, 5), r.randint(-99, 99)) for _ in range(n)))
    lines = ['{ %s x = %s, y = %s, z;' % (vt, vinit(), vinit())]
    ops = ['+', '-', '*'] + ([] if flt else ['&', '|', '^'])
    for _ in range(r.randint(1, 4)):
        c = r.randint(0, 4)
        if c == 0:
            lines.append('z = x %s y;' % r.choice(ops))
        elif c == 1:
            lines.append('z = x %s (%s)%d;' % (r.choice(ops), et, r.randint(-3, 3)))
        elif c == 2 and not flt:
            lines.append('z = (%s)(x %s y);' % (vt, r.choice(['<', '>', '==', '!=', '<=', '>='])))
        elif c == 3:
            lines.append('z = -x; z[%d] = (%s)c;' % (r.randint(0, n - 1), et))
        else:
            lines.append('z = x; z %s= y;' % r.choice(ops))
        lines.append('for (int i = 0; i < %d; i++) H((long long)(z[i] * %s));' % (n, '4' if flt else '1'))
        lines.append('x = z;')
    lines.append('H(sizeof(%s)); }' % vt)
    return td, ' '.join(lines)


def cleanup(r, k):
    n = r.randint(1, 3)
    body = ['{']
    for j in range(n):
        body.append('int cu%d_%d __attribute__((cleanup(cufn))) = c + %d;' % (k, j, j * 10 + k))
    if r.random() < 0.5:
        body.append('if (c & 1) { int inner __attribute__((cleanup(cufn))) = 99; }')
    body.append('}')
    return ' '.join(body)


def gen(r):
    top = ['#include <stdio.h>', '#include <stddef.h>',
           '#define H(...) (h = h * 1000003u + (unsigned long long)(__VA_ARGS__))',
           'unsigned long long h;',
           'static void cufn(int *p) { H(*p); }',
           'struct za { int n; char z[0]; int tail[]; };']
    body = []
    for k in range(r.randint(4, 12)):
        c = r.randint(0, 7)
        if c <= 2:
            body.append('H(%s);' % iexpr(r))
        elif c == 3:
            body += overflow(r, k)
        elif c == 4:
            d, u = ranges(r, k, r.random() < 0.5)
            body.append(d)
            body.append(u)
        elif c == 5:
            d, u = structrange(r, k)
            (top if r.random() < 0.5 else body).append(d)
            body.append(u)
        elif c == 6:
            td, u = vectors(r, k)
            top.append(td)
            body.append(u)
        else:
            if r.random() < 0.5:
                body.append(cleanup(r, k))
            else:
                body.append('{ __auto_type w = %s; H(w); H(sizeof w); H(sizeof(struct za)); }' % iexpr(r))
    top.append('__attribute__((noinline)) void f(unsigned long long a, long long b, int c, unsigned d)\n{\n    %s\n}' % '\n    '.join(body))
    calls = []
    for _ in range(3):
        calls.append('f(%s, %s, %d, %su);' % (r.choice(['0', '1', '0xffffffffffffffffull', '0x8000000000000000ull', '%dull' % r.randint(0, 1 << 40)]),
                                             r.choice(['0', '-1', '%dll' % r.randint(-(1 << 40), 1 << 40)]),
                                             r.randint(-1000, 1000), r.randint(0, 1 << 31)))
    top.append('int main(void)\n{\n    %s\n    printf("%%llu\\n", h);\n    return 0;\n}' % '\n    '.join(calls))
    return '\n'.join(top) + '\n'


def run(cmd, timeout=60):
    try:
        return subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return None


bad = skipped = 0
for t in range(COUNT):
    r = random.Random(SEED * 100003 + t)
    f = os.path.join(WORK, 'g%d_%d.c' % (SEED, t))
    with open(f, 'w') as fh:
        fh.write(gen(r))
    ub = run([CLANG, '-O0', '-w', '-fsanitize=undefined,address', '-fno-sanitize-recover=all', f, '-o', f + '.ub'])
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
