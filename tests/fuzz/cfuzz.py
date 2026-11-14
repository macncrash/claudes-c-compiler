"""Constant folding against runtime evaluation, ccc against clang.

usage: cfuzz.py CCC WORKDIR SEED COUNT [EXPRS_PER_FILE]

Each program evaluates random well-defined integer expressions three ways:
folded in a static initializer, folded in an integer constant expression
(an array size via sizeof), and at run time from volatile copies of the same
constants. clang's output is the reference; any line where ccc differs, or
where ccc's three ways disagree with each other, is a finding.
"""
import os
import random
import subprocess
import sys

CCC, W, seed, count = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
per = int(sys.argv[5]) if len(sys.argv) > 5 else 40
os.makedirs(W, exist_ok=True)

TYPES = [('signed char', 8, True), ('unsigned char', 8, False), ('short', 16, True),
         ('unsigned short', 16, False), ('int', 32, True), ('unsigned', 32, False),
         ('long', 64, True), ('unsigned long', 64, False), ('long long', 64, True),
         ('unsigned long long', 64, False), ('_Bool', 1, False)]
SUFFIX = {'int': '', 'unsigned': 'u', 'long': 'l', 'unsigned long': 'ul', 'long long': 'll',
          'unsigned long long': 'ull'}


def wrap(v, bits, signed):
    if bits == 1:
        return 1 if v else 0
    v &= (1 << bits) - 1
    if signed and v >> (bits - 1):
        v -= 1 << bits
    return v


def rank(t):
    return {'_Bool': 0, 'signed char': 1, 'unsigned char': 1, 'short': 2, 'unsigned short': 2,
            'int': 3, 'unsigned': 3, 'long': 4, 'unsigned long': 4, 'long long': 5,
            'unsigned long long': 5}[t]


def info(t):
    return next(x for x in TYPES if x[0] == t)


def promote(t):
    return 'int' if rank(t) < 3 else t


def usual(a, b):
    a, b = promote(a), promote(b)
    if a == b:
        return a
    (_, ba, sa), (_, bb, sb) = info(a), info(b)
    if sa == sb:
        return a if rank(a) >= rank(b) else b
    u, s = (a, b) if not sa else (b, a)
    if rank(u) >= rank(s):
        return u
    if info(s)[1] > info(u)[1]:
        return s
    return {'long': 'unsigned long', 'long long': 'unsigned long long', 'int': 'unsigned'}[s]


class Gen:
    """Expressions as (C text with constants, C text with volatiles, value, type)."""

    def __init__(self, rnd):
        self.r = rnd
        self.vols = []

    def leaf(self):
        t = self.r.choice(['int', 'unsigned', 'long', 'unsigned long', 'long long', 'unsigned long long'])
        bits, signed = info(t)[1], info(t)[2]
        pick = self.r.random()
        if pick < 0.3:
            v = self.r.randint(0, 9)
        elif pick < 0.6:
            v = self.r.choice([0x7f, 0x80, 0xff, 0x7fff, 0x8000, 0xffff, 0x7fffffff, 0x80000000 - 1, 0xffffffff, 255, 256, 65535])
        else:
            v = self.r.getrandbits(bits - (1 if signed else 0))
        if signed and v > (1 << (bits - 1)) - 1:
            v = (1 << (bits - 1)) - 1
        if not signed:
            v = wrap(v, bits, False)
        lit = '%d%s' % (v, SUFFIX[t])
        if t == 'int' and v > 0x7fffffff:
            lit = '%du' % v
        name = 'v%d' % len(self.vols)
        self.vols.append((name, t, lit))
        return lit, name, v, t

    def expr(self, depth):
        if depth <= 0 or self.r.random() < 0.25:
            return self.leaf()
        k = self.r.random()
        if k < 0.15:
            a = self.expr(depth - 1)
            t = self.r.choice([x[0] for x in TYPES])
            bits, signed = info(t)[1], info(t)[2]
            return ('((%s)%s)' % (t, a[0]), '((%s)%s)' % (t, a[1]), wrap(a[2], bits, signed), t)
        if k < 0.25:
            a = self.expr(depth - 1)
            op = self.r.choice(['-', '~', '!'])
            t = promote(a[3]) if op != '!' else 'int'
            bits, signed = info(t)[1], info(t)[2]
            v = {'-': -a[2], '~': ~a[2], '!': int(a[2] == 0)}[op]
            return ('(%s%s)' % (op, a[0]), '(%s%s)' % (op, a[1]), wrap(v, bits, signed), t)
        if k < 0.33:
            c, a, b = self.expr(depth - 1), self.expr(depth - 1), self.expr(depth - 1)
            t = usual(a[3], b[3])
            bits, signed = info(t)[1], info(t)[2]
            v = wrap(a[2] if c[2] else b[2], bits, signed)
            return ('(%s ? %s : %s)' % (c[0], a[0], b[0]), '(%s ? %s : %s)' % (c[1], a[1], b[1]), v, t)
        a, b = self.expr(depth - 1), self.expr(depth - 1)
        op = self.r.choice(['+', '-', '*', '/', '%', '&', '|', '^', '<<', '>>', '<', '<=', '==', '!=', '&&', '||'])
        if op in ('<<', '>>'):
            t = promote(a[3])
            bits, signed = info(t)[1], info(t)[2]
            sh = b[2] % bits
            if sh < 0:
                sh += bits
            if signed and op == '<<' and (a[2] < 0 or (a[2] << sh) >= 1 << (bits - 1)):
                return a
            v = a[2] << sh if op == '<<' else a[2] >> sh
            st = '%d' % sh
            return ('(%s %s %s)' % (a[0], op, st), '(%s %s %s)' % (a[1], op, st), wrap(v, bits, signed), t)
        if op in ('<', '<=', '==', '!='):
            t = usual(a[3], b[3])
            bits, signed = info(t)[1], info(t)[2]
            x, y = wrap(a[2], bits, signed), wrap(b[2], bits, signed)
            v = int({'<': x < y, '<=': x <= y, '==': x == y, '!=': x != y}[op])
            return ('(%s %s %s)' % (a[0], op, b[0]), '(%s %s %s)' % (a[1], op, b[1]), v, 'int')
        if op in ('&&', '||'):
            v = int(bool(a[2]) and bool(b[2])) if op == '&&' else int(bool(a[2]) or bool(b[2]))
            return ('(%s %s %s)' % (a[0], op, b[0]), '(%s %s %s)' % (a[1], op, b[1]), v, 'int')
        t = usual(a[3], b[3])
        bits, signed = info(t)[1], info(t)[2]
        x, y = wrap(a[2], bits, signed), wrap(b[2], bits, signed)
        if op in ('/', '%'):
            if y == 0 or (signed and x == -(1 << (bits - 1)) and y == -1):
                return a
            q = abs(x) // abs(y) * (1 if (x < 0) == (y < 0) else -1)
            v = q if op == '/' else x - q * y
        else:
            v = {'+': x + y, '-': x - y, '*': x * y, '&': x & y, '|': x | y, '^': x ^ y}[op]
            if signed and v != wrap(v, bits, True):
                return a
        return ('(%s %s %s)' % (a[0], op, b[0]), '(%s %s %s)' % (a[1], op, b[1]), wrap(v, bits, signed), t)


def program(rnd, n):
    lines = ['#include <stdio.h>', '']
    body = []
    for i in range(n):
        g = Gen(rnd)
        e = g.expr(rnd.randint(2, 5))
        t = e[3]
        for name, vt, lit in g.vols:
            body.append('    volatile %s %s_%d = %s;' % (vt, name, i, lit))
        run = e[1]
        for name, _, _ in sorted(g.vols, key=lambda x: -len(x[0])):
            run = run.replace(name, '%s_%d' % (name, i))
        lines.append('static const %s c%d = %s;' % (t, i, e[0]))
        body.append('    { %s r = %s; enum { E%d = (%s) != 0 }; char a%d[(%s) ? 2 : 1];'
                    ' printf("%d %%llx %%llx %%d %%zu\\n", (unsigned long long)c%d, (unsigned long long)r, E%d, sizeof a%d); }'
                    % (t, run, i, e[0], i, e[0], i, i, i, i))
    lines.append('int main(void)\n{')
    lines += body
    lines.append('    return 0;\n}')
    return '\n'.join(lines) + '\n'


def main():
    rnd = random.Random(seed)
    found = 0
    for k in range(count):
        src = os.path.join(W, 'f%d_%d.c' % (seed, k))
        open(src, 'w').write(program(rnd, per))
        outs = {}
        for tag, cc in (('k', '/usr/bin/cc'), ('c', CCC)):
            exe = os.path.join(W, 'x_' + tag)
            r = subprocess.run([cc, '-O2', '-w', '-o', exe, src], capture_output=True, text=True)
            if r.returncode != 0:
                outs[tag] = 'COMPILE FAILED: ' + r.stderr[:300]
                continue
            p = subprocess.run([exe], capture_output=True, text=True, timeout=20)
            outs[tag] = p.stdout
        if outs['k'].startswith('COMPILE FAILED'):
            print('clang rejected', src, outs['k'][:200])
            continue
        if outs['c'] != outs['k']:
            found += 1
            bad = [(a, b) for a, b in zip(outs['k'].split('\n'), outs['c'].split('\n')) if a != b]
            print('MISMATCH', src, bad[:3] if bad else outs['c'][:200])
        else:
            os.remove(src)
        # ccc's three ways must agree among themselves too.
        for line in outs['c'].split('\n'):
            p = line.split()
            if len(p) == 5 and p[1] != p[2]:
                print('FOLD-VS-RUN', src, line)
    print('files', count, 'mismatching', found)


main()
