"""Integer arithmetic on runtime values, ccc against clang.

usage: rtfuzz.py CCC CLANG WORKDIR SEED COUNT [FUNCS]   (OPT=-O0 etc.)

Each function takes two 64-bit values the optimizer cannot see, narrows
them to random types, and combines them with division and remainder by
constants, shifts, compares, masks, casts and compound assignment on
narrow locals. Only defined behaviour is generated: signed + - * are done
in uint64_t unless the operands are too narrow to overflow int, divisors
are nonzero constants (never -1 for signed), shifts stay in range, and
left shifts are of unsigned values. main calls every function on a table
of edge values and prints each result.
"""
import os
import random
import subprocess
import sys

CCC, CLANG, W = sys.argv[1], sys.argv[2], sys.argv[3]
seed, count = int(sys.argv[4]), int(sys.argv[5])
per = int(sys.argv[6]) if len(sys.argv) > 6 else 12
os.makedirs(W, exist_ok=True)

# (name, width, signed)
TYPES = [('int8_t', 8, True), ('uint8_t', 8, False), ('int16_t', 16, True), ('uint16_t', 16, False),
         ('int32_t', 32, True), ('uint32_t', 32, False), ('int64_t', 64, True), ('uint64_t', 64, False)]
BYNAME = {t[0]: t for t in TYPES}


def promoted(t):
    name, w, s = t
    if w < 32:
        return BYNAME['int32_t']
    return t


def usual(a, b):
    a, b = promoted(a), promoted(b)
    if a == b:
        return a
    if a[1] == b[1]:
        return a if not a[2] else b          # same width: unsigned wins
    return a if a[1] > b[1] else b           # wider wins (64 holds all 32)


def const(r, t):
    name, w, s = t
    lo, hi = (-(1 << (w - 1)), (1 << (w - 1)) - 1) if s else (0, (1 << w) - 1)
    v = r.choice([0, 1, 2, 3, 5, 7, 10, 16, 100, 255, 256, 1000, 65535, 65536, 0x7fffffff,
                  0x80000000, 0xffffffff, 1 << 40, (1 << 63) - 1, -1, -2, -3, -7, -128, -129, -32768,
                  r.randint(-1 << 20, 1 << 20), r.randint(0, (1 << 64) - 1)])
    v = max(lo, min(hi, v))
    return v


def lit(v, t):
    name, w, s = t
    if w == 64:
        if s:
            if v == -(1 << 63):
                return '(int64_t)(-9223372036854775807LL - 1)'
            return '(int64_t)%dLL' % v
        return '(uint64_t)%dULL' % v
    if w == 32 and s and v == -(1 << 31):
        return '(int32_t)(-2147483647 - 1)'
    return '(%s)%d%s' % (name, v, 'U' if not s and w == 32 else '')


class Gen:
    def __init__(self, r, names):
        self.r = r
        self.names = names     # [(c name, type)]

    def leaf(self):
        r = self.r
        if r.random() < 0.75:
            n, t = r.choice(self.names)
            return n, t
        t = r.choice(TYPES)
        return lit(const(r, t), t), t

    def expr(self, d):
        r = self.r
        if d <= 0 or r.random() < 0.2:
            return self.leaf()
        k = r.random()
        if k < 0.22:
            return self.arith(d)
        if k < 0.40:
            return self.divmod(d)
        if k < 0.52:
            return self.shift(d)
        if k < 0.62:
            x, tx = self.expr(d - 1)
            y, ty = self.expr(d - 1)
            op = r.choice(['&', '|', '^'])
            return '(%s %s %s)' % (x, op, y), usual(tx, ty)
        if k < 0.72:
            x, tx = self.expr(d - 1)
            y, ty = self.expr(d - 1)
            op = r.choice(['<', '<=', '>', '>=', '==', '!='])
            return '(%s %s %s)' % (x, op, y), BYNAME['int32_t']
        if k < 0.80:
            c, _ = self.expr(d - 1)
            x, tx = self.expr(d - 1)
            y, ty = self.expr(d - 1)
            return '(%s ? %s : %s)' % (c, x, y), usual(tx, ty)
        if k < 0.92:
            x, tx = self.expr(d - 1)
            t = r.choice(TYPES)
            return '((%s)%s)' % (t[0], x), t
        x, tx = self.expr(d - 1)
        op = r.choice(['-', '~', '!'])
        if op == '-' and promoted(tx)[2]:
            # Negating a signed value can overflow: negate it unsigned.
            pt = promoted(tx)
            u = 'uint%d_t' % pt[1]
            return '((%s)(-(%s)%s))' % (pt[0], u, x), pt
        if op == '!':
            return '(!%s)' % x, BYNAME['int32_t']
        return '(%s%s)' % (op, x), promoted(tx)

    def arith(self, d):
        r = self.r
        x, tx = self.expr(d - 1)
        y, ty = self.expr(d - 1)
        op = r.choice(['+', '-', '*'])
        res = usual(tx, ty)
        safe = (not res[2]) or (tx[1] <= 8 and ty[1] <= 8) or (op != '*' and tx[1] <= 16 and ty[1] <= 16)
        if safe:
            return '(%s %s %s)' % (x, op, y), res
        u = 'uint%d_t' % res[1]
        return '((%s)((%s)%s %s (%s)%s))' % (res[0], u, x, op, u, y), res

    def divmod(self, d):
        r = self.r
        x, tx = self.expr(d - 1)
        op = r.choice(['/', '%'])
        res = promoted(tx)
        if r.random() < 0.75:
            # Divisor a constant: of the dividend's promoted type or another.
            ct = r.choice([res, r.choice(TYPES)])
            v = 0
            while v == 0 or (v == -1 and usual(tx, ct)[2]):
                v = const(r, ct)
                if v == 0:
                    v = r.choice([3, 7, -5, 10, 1 << 31, 641, 6700417])
                    v = max(-(1 << (ct[1] - 1)) if ct[2] else 1, min(((1 << (ct[1] - 1)) - 1) if ct[2] else (1 << ct[1]) - 1, v))
            return '(%s %s %s)' % (x, op, lit(v, ct)), usual(tx, ct)
        # A runtime divisor, made nonzero; only for unsigned division.
        y, ty = self.expr(d - 1)
        res = usual(tx, ty)
        u = 'uint%d_t' % res[1]
        return '((%s)%s %s ((%s)%s | 1))' % (u, x, op, u, y), BYNAME[u]

    def shift(self, d):
        r = self.r
        x, tx = self.expr(d - 1)
        pt = promoted(tx)
        n = r.randint(0, pt[1] - 1)
        if r.random() < 0.5:
            u = 'uint%d_t' % pt[1]
            return '((%s)%s << %d)' % (u, x, n), BYNAME[u]
        if r.random() < 0.3:
            # A runtime amount, masked into range.
            y, ty = self.expr(d - 1)
            return '(%s >> (%s & %d))' % (x, y, pt[1] - 1), pt
        return '(%s >> %d)' % (x, n), pt


def function(r, i):
    lines = ['__attribute__((noinline)) uint64_t f%d(uint64_t pa, uint64_t pb) {' % i]
    ta, tb = r.choice(TYPES), r.choice(TYPES)
    lines.append('    %s a = (%s)pa; %s b = (%s)pb;' % (ta[0], ta[0], tb[0], tb[0]))
    names = [('a', ta), ('b', tb)]
    for j in range(r.randint(1, 4)):
        g = Gen(r, names)
        t = r.choice(TYPES)
        e, _ = g.expr(r.randint(1, 4))
        v = 'v%d' % j
        lines.append('    %s %s = (%s)%s;' % (t[0], v, t[0], e))
        # Compound assignments on the local.
        for _ in range(r.randint(0, 2)):
            e2, t2 = g.expr(r.randint(0, 2))
            ops = ['&=', '|=', '^=', '>>=']
            if t[1] <= 16 or not t[2]:
                ops += ['+=', '-=']
            if t[1] <= 8 or not t[2]:
                ops.append('*=')
            ops += ['/=', '%=']
            op = r.choice(ops)
            if op == '>>=':
                lines.append('    %s >>= %d;' % (v, r.randint(0, promoted(t)[1] - 1)))
            elif op in ('/=', '%='):
                c = 0
                while c == 0 or c == -1:
                    c = const(r, t)
                    if c == 0:
                        c = 3
                lines.append('    %s %s %s;' % (v, op, lit(c, t)))
            elif t[1] <= 16:
                # Narrow: the right side is cast to the local's type, so the
                # promoted arithmetic cannot overflow int.
                lines.append('    %s %s (%s)%s;' % (v, op, t[0], e2))
            else:
                lines.append('    %s %s (%s)%s;' % (v, op, t[0], e2))
        names.append((v, t))
    g = Gen(r, names)
    e, _ = g.expr(r.randint(1, 3))
    lines.append('    return (uint64_t)%s ^ ((uint64_t)%s << 1);' % (e, names[-1][0]))
    lines.append('}')
    return '\n'.join(lines)


VALUES = ['0', '1', '2', '3', '7', '-1', '-2', '-7', '127', '128', '255', '256', '-128', '-129',
          '32767', '32768', '65535', '65536', '-32768', '0x7fffffff', '0x80000000', '0xffffffff',
          '0x100000000', '-0x80000000', '0x7fffffffffffffff', '0x8000000000000000', '0x123456789abcdef0',
          '0xfedcba9876543210', '1000000007', '-1000000007', '0x5555555555555555', '641']


def program(r, n):
    out = ['#include <stdint.h>', '#include <stdio.h>', '']
    for i in range(n):
        out.append(function(r, i))
    out.append('static const uint64_t vals[] = { %s };' % ', '.join('(uint64_t)' + v + ('LL' if 'x' not in v else 'ULL') for v in VALUES))
    out.append('int main(void) {')
    out.append('    unsigned n = sizeof vals / sizeof vals[0];')
    out.append('    for (unsigned i = 0; i < n; i++) for (unsigned j = 0; j < n; j += 3) {')
    for i in range(n):
        out.append('        printf("%d %%u %%u %%llx\\n", i, j, (unsigned long long)f%d(vals[i], vals[(i + j) %% n]));' % (i, i))
    out.append('    }')
    out.append('    return 0;\n}')
    return '\n'.join(out) + '\n'


def run(cc, src, exe):
    c = subprocess.run([cc, os.environ.get('OPT', '-O2'), '-w', '-o', exe, src], capture_output=True, text=True)
    if c.returncode != 0:
        return 'COMPILE FAILED: ' + c.stderr[:400]
    p = subprocess.run([exe], capture_output=True, text=True, timeout=20)
    return p.stdout + ('' if p.returncode == 0 else 'EXIT %d' % p.returncode)


def main():
    rnd = random.Random(seed)
    found = 0
    for k in range(count):
        src = os.path.join(W, 'r%d_%d.c' % (seed, k))
        open(src, 'w').write(program(rnd, per))
        ref = run(CLANG, src, os.path.join(W, 'xk'))
        if ref.startswith('COMPILE'):
            print('clang rejected', src, ref[:300])
            continue
        # UBSan-free check that the generator stays defined: clang -O0 must agree with -O2.
        got = run(CCC, src, os.path.join(W, 'xc'))
        if got != ref:
            found += 1
            d = [(p, q) for p, q in zip(ref.split('\n'), got.split('\n')) if p != q]
            print('MISMATCH', src, d[:3] if d else got[:300])
        else:
            os.remove(src)
    print('seed %d: %d of %d files mismatched' % (seed, found, count))


main()
