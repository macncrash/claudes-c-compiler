"""Constant folding of floating-point expressions against run time, ccc against
clang.

usage: ffuzz.py CCC CLANG WORKDIR SEED COUNT [EXPRS_PER_FILE]

Expressions over float, double and integer constants with + - * /, casts both
ways, comparisons and ?:. Every value is kept finite and every conversion to an
integer type in range, so nothing is undefined. Each is folded in a static
initializer and computed at run time from volatile copies; results print in
%a, exactly. clang's output is the reference.
"""
import math
import os
import random
import re
import struct
import subprocess
import sys

CCC, CLANG, W, seed, count = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]), int(sys.argv[5])
per = int(sys.argv[6]) if len(sys.argv) > 6 else 40
os.makedirs(W, exist_ok=True)

INTS = {'int': (32, True), 'unsigned': (32, False), 'long': (64, True), 'unsigned long': (64, False),
        'short': (16, True), 'unsigned char': (8, False), 'signed char': (8, True)}


def f32(x):
    try:
        return struct.unpack('f', struct.pack('f', x))[0]
    except OverflowError:
        # Beyond float's range: infinity, as the C conversion gives, which
        # the generator's finiteness checks then reject.
        return math.copysign(math.inf, x)


def rounded(x, t):
    return f32(x) if t == 'float' else x


class Gen:
    def __init__(self, r):
        self.r = r
        self.vols = []

    def leaf(self):
        k = self.r.random()
        if k < 0.45:
            t = self.r.choice(['float', 'double'])
            v = self.r.choice([0.5, 1.0, 3.0, 0.1, 1e10, 1e-10, 2.5, 7.25, 1.0 / 3, 123456789.0, 16777217.0,
                               0.30000000000000004, 1e30, 65504.0, 4294967296.0])
            v = v * self.r.choice([1, -1])
            v = rounded(v, t)
            lit = repr(v) + ('f' if t == 'float' else '')
            if 'e' not in lit and '.' not in lit:
                lit = lit.replace('f', '.0f') if t == 'float' else lit + '.0'
        else:
            t = self.r.choice(['int', 'unsigned', 'long', 'unsigned long'])
            bits, signed = INTS[t]
            v = self.r.choice([0, 1, 3, 7, 255, 1000, 16777217, 2147483647, 123456789]) if signed else \
                self.r.choice([0, 1, 9, 4294967295, 3000000000, 16777217])
            if t.endswith('long') and self.r.random() < 0.5:
                v = self.r.choice([9007199254740993, 1 << 62, 123456789012345]) if signed else \
                    self.r.choice([18446744073709551615, 9007199254740993, 1 << 63])
            lit = '%d%s' % (v, {'int': '', 'unsigned': 'u', 'long': 'l', 'unsigned long': 'ul'}[t])
        name = 'v%d' % len(self.vols)
        self.vols.append((name, t, lit))
        return lit, name, v, t

    def expr(self, depth):
        if depth <= 0 or self.r.random() < 0.3:
            return self.leaf()
        k = self.r.random()
        if k < 0.25:
            a = self.expr(depth - 1)
            t = self.r.choice(['float', 'double'] + list(INTS))
            if t in INTS:
                bits, signed = INTS[t]
                lo, hi = (-(1 << (bits - 1)), (1 << (bits - 1)) - 1) if signed else (0, (1 << bits) - 1)
                if a[3] in ('float', 'double'):
                    if not (lo - 1 < a[2] < hi + 1) or math.isnan(a[2]):
                        return a
                    v = int(a[2])
                else:
                    v = a[2] & ((1 << bits) - 1)
                    if signed and v >> (bits - 1):
                        v -= 1 << bits
            else:
                v = rounded(float(a[2]), t)
            return '((%s)%s)' % (t, a[0]), '((%s)%s)' % (t, a[1]), v, t
        if k < 0.35:
            c, a, b = self.expr(depth - 1), self.expr(depth - 1), self.expr(depth - 1)
            if 'float' not in (a[3], b[3]) and 'double' not in (a[3], b[3]):
                return a
            t = 'double' if 'double' in (a[3], b[3]) else 'float'
            v = rounded(float(a[2] if c[2] else b[2]), t)
            return ('(%s ? %s : %s)' % (c[0], a[0], b[0]), '(%s ? %s : %s)' % (c[1], a[1], b[1]), v, t)
        a, b = self.expr(depth - 1), self.expr(depth - 1)
        if a[3] not in ('float', 'double') and b[3] not in ('float', 'double'):
            return a
        t = 'double' if 'double' in (a[3], b[3]) else 'float'
        x, y = rounded(float(a[2]), t), rounded(float(b[2]), t)
        op = self.r.choice(['+', '-', '*', '/', '<', '<=', '==', '!='])
        if op in ('<', '<=', '==', '!='):
            v = int({'<': x < y, '<=': x <= y, '==': x == y, '!=': x != y}[op])
            return '(%s %s %s)' % (a[0], op, b[0]), '(%s %s %s)' % (a[1], op, b[1]), v, 'int'
        if op == '/' and y == 0:
            return a
        try:
            v = {'+': x + y, '-': x - y, '*': x * y, '/': x / y if y else 0}[op]
        except OverflowError:
            return a
        v = rounded(v, t)
        if not math.isfinite(v) or abs(v) > 3e38:
            return a
        return '(%s %s %s)' % (a[0], op, b[0]), '(%s %s %s)' % (a[1], op, b[1]), v, t


def program(r, n):
    out = ['#include <stdio.h>', '']
    body = []
    for i in range(n):
        g = Gen(r)
        e = g.expr(r.randint(2, 5))
        t = e[3]
        for name, vt, lit in g.vols:
            body.append('    volatile %s %s_%d = %s;' % (vt, name, i, lit))
        run = re.sub(r'\bv(\d+)\b', lambda m: 'v%s_%d' % (m.group(1), i), e[1])
        out.append('static const %s c%d = %s;' % (t, i, e[0]))
        if t in ('float', 'double'):
            body.append('    { %s r = %s; printf("%d %%a %%a\\n", (double)c%d, (double)r); }' % (t, run, i, i))
        else:
            body.append('    { %s r = %s; printf("%d %%lld %%lld\\n", (long long)c%d, (long long)r); }' % (t, run, i, i))
    out.append('int main(void)\n{')
    out += body
    out.append('    return 0;\n}')
    return '\n'.join(out) + '\n'


def main():
    r = random.Random(seed)
    bad = 0
    for k in range(count):
        src = os.path.join(W, 'g%d_%d.c' % (seed, k))
        open(src, 'w').write(program(r, per))
        outs = {}
        for tag, cc in (('k', CLANG), ('c', CCC)):
            exe = os.path.join(W, 'y_' + tag)
            p = subprocess.run([cc, '-O2', '-w', '-o', exe, src], capture_output=True, text=True)
            outs[tag] = subprocess.run([exe], capture_output=True, text=True, timeout=20).stdout if p.returncode == 0 \
                else 'COMPILE FAILED ' + p.stderr[:300]
        if outs['k'].startswith('COMPILE FAILED'):
            print('clang rejected', src, outs['k'][:200])
            continue
        if outs['c'] != outs['k']:
            bad += 1
            diffs = [(a, b) for a, b in zip(outs['k'].split('\n'), outs['c'].split('\n')) if a != b]
            print('MISMATCH', os.path.basename(src), diffs[:3] if diffs else outs['c'][:200])
        else:
            os.remove(src)
    print('files', count, 'mismatching', bad)


main()
