#!/usr/bin/env python3
"""Random control flow, ccc vs clang.

usage: cffuzz.py CCC CLANG WORKDIR SEED COUNT [FUNCS]   (OPT=-O2 default, SAN=1)

Each function takes two runtime u64 values and a small n, keeps locals of
several widths, and runs nested if/else with side-effecting short-circuit
conditions, switches (fallthrough, GNU case ranges, negative and 64-bit
cases, default in any position), bounded for/while/do loops with break and
continue (also from inside a switch), forward and bounded backward gotos,
computed gotos, ?: and comma chains, and early returns.  Every executed
statement mixes a value into a trace hash, so a wrong path shows up even
when the result happens to agree.  All arithmetic is unsigned or within range.
"""
import os
import random
import subprocess
import sys

CCC, CLANG, W = sys.argv[1], sys.argv[2], sys.argv[3]
seed, count = int(sys.argv[4]), int(sys.argv[5])
per = int(sys.argv[6]) if len(sys.argv) > 6 else 4

VARS = [('x8', 'uint8_t'), ('y16', 'uint16_t'), ('s8', 'int8_t'), ('s16', 'int16_t'),
        ('z32', 'uint32_t'), ('w64', 'uint64_t'), ('i', 'int')]
VALUES = ['0', '1', '2', '3', '5', '7', '0xff', '0x80', '0x7f', '0xffff', '0x8000', '0x7fffffff', '0x80000000',
          '0xffffffff', '0x100000000', '0xfffffffffffffffe', '0x8000000000000000', '0x123456789abcdef',
          '0xdeadbeefcafebabe', '42', '100', '0xfffffff0']


class Fn:
    def __init__(self, r, k):
        self.r = r
        self.k = k
        self.lab = 0
        self.lines = []
        self.loopdepth = 0
        self.swdepth = 0
        self.fresh_vars = []

    def uid(self):
        self.lab += 1
        return '%d_%d' % (self.k, self.lab)

    # expressions: every one is valid as uint64_t without UB
    def leaf(self):
        r = self.r
        c = r.random()
        if c < 0.45:
            return r.choice(VARS)[0]
        if c < 0.6:
            return r.choice(['a', 'b', 'n'])
        return r.choice(['0', '1', '2', '3', '7', '15', '255', '0x80', '1000', '0xffffffffu', '0x7fffffffffffffffULL',
                         '(-1)', '(-128)', '65535'])

    def expr(self, d=0):
        r = self.r
        if d > 2 or r.random() < 0.3:
            return self.leaf()
        e1, e2 = self.expr(d + 1), self.expr(d + 1)
        c = r.randrange(12)
        if c == 0:
            return '((uint64_t)%s * (uint64_t)%s)' % (e1, e2)
        if c == 1:
            return '((uint64_t)%s << (%s & 63))' % (e1, e2)
        if c == 2:
            return '((uint64_t)%s >> (%s & 63))' % (e1, e2)
        if c == 3:
            return '((uint64_t)%s / ((uint64_t)%s | 1))' % (e1, e2)
        if c == 4:
            return '((uint64_t)%s %% ((uint64_t)%s | 1))' % (e1, e2)
        if c == 5:
            return '(%s ? %s : %s)' % (self.cond(d + 1), e1, e2)
        if c == 6:
            # side effects inside an expression only touch a variable of
            # their own, so nothing is modified and read unsequenced
            v = self.fresh()
            return '(%s %s %s, (uint64_t)%s)' % (v, r.choice(['=', '+=', '^=', '|=', '-=']), e1, v)
        if c == 7:
            return '(uint64_t)(%s)%s' % (r.choice(['uint8_t', 'int8_t', 'uint16_t', 'int16_t', 'uint32_t', 'int32_t']), e1)
        if c == 8:
            return '((uint64_t)%s %s (uint64_t)%s)' % (e1, r.choice(['+', '-']), e2)
        return '((uint64_t)%s %s (uint64_t)%s)' % (e1, r.choice(['&', '|', '^']), e2)

    def safe_rhs(self, v, e):
        # i is a signed int: keep its value small so += and -= never overflow
        if v == 'i':
            return '(int)((uint64_t)%s & 0xffff)' % e
        return '(%s)' % e

    def cond(self, d=0):
        r = self.r
        c = r.randrange(9)
        if d > 2:
            c = r.randrange(3)
        if c == 0:
            return '(%s %s %s)' % (self.expr(d + 1), r.choice(['<', '>', '<=', '>=', '==', '!=']), self.expr(d + 1))
        if c == 1:
            t = r.choice(['int8_t', 'int16_t', 'int32_t', 'int64_t'])
            return '((%s)%s %s (%s)%s)' % (t, self.expr(d + 1), r.choice(['<', '>', '<=', '>=']), t, self.expr(d + 1))
        if c == 2:
            v = r.choice(VARS)[0]
            return '(%s %s %s)' % (v, r.choice(['<', '>', '==', '!=', '<=']), r.choice(['0', '1', '-1', '5', '200', '-100', '40000']))
        if c == 3:
            return '(%s && %s)' % (self.cond(d + 1), self.cond(d + 1))
        if c == 4:
            return '(%s || %s)' % (self.cond(d + 1), self.cond(d + 1))
        if c == 5:
            return '(!%s)' % self.cond(d + 1)
        if c == 6 or c == 7:
            v = self.fresh()
            op = r.choice(['++', '--'])
            return '((%s) & %d)' % (v + op if c == 6 else op + v, r.choice([1, 2, 4]))
        return '((%s) & 1)' % self.expr(d + 1)

    def emit(self, ind, s):
        self.lines.append('    ' * ind + s)

    def assign(self, ind):
        r = self.r
        v = r.choice(VARS)[0]
        op = r.choice(['=', '+=', '^=', '|=', '&=', '-=']) if v != 'i' else r.choice(['=', '+=', '-=', '^='])
        self.emit(ind, '%s %s %s;' % (v, op, self.safe_rhs(v, self.expr())))

    def stmt(self, ind, d):
        r = self.r
        c = r.random()
        if d > 3 or c < 0.25:
            self.assign(ind)
            self.emit(ind, 'h = T(h + %s);' % r.choice(VARS)[0])
            return
        c = r.randrange(12)
        if c == 0 or c == 1:
            self.emit(ind, 'if %s {' % self.cond())
            self.block(ind + 1, d + 1)
            if r.random() < 0.6:
                if r.random() < 0.3:
                    self.emit(ind, '} else if %s {' % self.cond())
                    self.block(ind + 1, d + 1)
                self.emit(ind, '} else {')
                self.block(ind + 1, d + 1)
            self.emit(ind, '}')
        elif c == 2 or c == 3:
            self.switch(ind, d)
        elif c == 4:
            u = self.uid()
            self.emit(ind, 'for (unsigned l%s = 0; l%s < %d; l%s++) {' % (u, u, r.choice([1, 2, 3, 5, 8]), u))
            self.emit(ind + 1, 'h = T(h ^ l%s);' % u)
            self.loop_body(ind + 1, d)
            self.emit(ind, '}')
        elif c == 5:
            u = self.uid()
            self.emit(ind, 'unsigned c%s = 0;' % u)
            self.emit(ind, 'while (c%s++ < %d && %s) {' % (u, r.choice([2, 3, 6]), self.cond()))
            self.loop_body(ind + 1, d)
            self.emit(ind, '}')
            self.emit(ind, 'h = T(h + c%s);' % u)
        elif c == 6:
            u = self.uid()
            self.emit(ind, 'unsigned c%s = 0;' % u)
            self.emit(ind, 'do {')
            self.loop_body(ind + 1, d)
            self.emit(ind, '} while (++c%s < %d && %s);' % (u, r.choice([2, 4]), self.cond()))
            self.emit(ind, 'h = T(h + c%s);' % u)
        elif c == 7:
            u = self.uid()
            self.emit(ind, 'if %s goto F%s;' % (self.cond(), u))
            self.block_items(ind, d + 1)
            self.emit(ind, 'F%s: h = T(h * 3 + %s);' % (u, r.choice(VARS)[0]))
        elif c == 8:
            u = self.uid()
            self.emit(ind, '{ unsigned g%s = 0;' % u)
            self.emit(ind, 'B%s: h = T(h + g%s);' % (u, u))
            self.block_items(ind + 1, d + 1)
            self.emit(ind, 'if (++g%s < %d && %s) goto B%s; }' % (u, r.choice([2, 3, 4]), self.cond(), u))
        elif c == 9:
            u = self.uid()
            m = r.choice([2, 3, 4])
            self.emit(ind, '{ static void *const t%s[] = { %s };' % (u, ', '.join('&&C%s_%d' % (u, j) for j in range(m))))
            self.emit(ind, 'goto *t%s[(uint64_t)%s %% %d];' % (u, self.expr(), m))
            for j in range(m):
                self.emit(ind, 'C%s_%d:' % (u, j))
                self.block_items(ind + 1, d + 1)
                if r.random() < 0.6:
                    self.emit(ind + 1, 'goto C%s_e;' % u)
            self.emit(ind, 'C%s_e: h = T(h + 11); }' % u)
        elif c == 10:
            self.emit(ind, 'if %s return T(h ^ %s);' % (self.cond(), self.expr()))
        else:
            self.emit(ind, 'h = T(h + (%s ? %s : %s ? %s : %s));' % (self.cond(), self.expr(), self.cond(),
                                                                    self.expr(), self.expr()))

    def loop_body(self, ind, d):
        self.loopdepth += 1
        sw = self.swdepth
        self.swdepth = 0
        self.block_items(ind, d + 1)
        self.swdepth = sw
        self.loopdepth -= 1

    def block_items(self, ind, d):
        r = self.r
        for _ in range(r.randint(1, 3)):
            if self.loopdepth and r.random() < 0.15:
                self.emit(ind, 'if %s %s;' % (self.cond(), r.choice(['break', 'continue'])
                                              if not self.swdepth else 'continue'))
            self.stmt(ind, d)

    def block(self, ind, d):
        self.block_items(ind, d)

    def switch(self, ind, d):
        r = self.r
        kind = r.randrange(4)
        if kind == 0:
            sel, pool = '(uint8_t)%s' % self.expr(), list(range(0, 256))
        elif kind == 1:
            sel, pool = '(int8_t)%s' % self.expr(), list(range(-128, 128))
        elif kind == 2:
            sel, pool = '(%s & 15)' % self.expr(), list(range(0, 16))
        else:
            sel, pool = '(uint64_t)%s' % self.expr(), None
        self.emit(ind, 'switch (%s) {' % sel)
        used = set()
        labels = []
        for _ in range(r.randint(1, 6)):
            if pool is None:
                v = r.choice(['0', '1', '2', '7', '0xffffffffULL', '0x100000000ULL', '0xffffffffffffffffULL',
                              '0x8000000000000000ULL', '42', '255', '1000'])
                if v in used:
                    continue
                used.add(v)
                labels.append('case %s:' % v)
            else:
                lo = r.choice(pool)
                hi = lo + (r.choice([0, 0, 1, 3, 10]) if r.random() < 0.3 else 0)
                hi = min(hi, pool[-1])
                rng = set(range(lo, hi + 1))
                if rng & used:
                    continue
                used |= rng
                labels.append('case %d:' % lo if lo == hi else 'case %d ... %d:' % (lo, hi))
        if r.random() < 0.8:
            labels.insert(r.randrange(len(labels) + 1), 'default:')
        self.swdepth += 1
        for lab in labels:
            self.emit(ind, lab)
            self.emit(ind + 1, 'h = T(h * 5 + %d);' % (len(self.lines) % 97))
            if r.random() < 0.7:
                self.block_items(ind + 1, d + 1)
            c = r.random()
            if c < 0.6:
                self.emit(ind + 1, 'break;')
            elif c < 0.7 and self.loopdepth:
                self.emit(ind + 1, 'continue;')
        self.swdepth -= 1
        self.emit(ind, '}')

    def text(self):
        r = self.r
        self.emit(0, '__attribute__((noinline)) uint64_t f%d(uint64_t a, uint64_t b, unsigned n) {' % self.k)
        self.emit(1, 'uint64_t h = 0;')
        for name, t in VARS:
            if t == 'int':
                self.emit(1, 'int i = (int)(a & 0xff) - 100;')
            else:
                self.emit(1, '%s %s = (%s)%s;' % (t, name, t, r.choice(['a', 'b', '(a ^ b)', '(a >> 32)', '(b * 3)', 'n'])))
        head = len(self.lines)
        for _ in range(r.randint(3, 7)):
            self.stmt(1, 0)
        fr = ''.join(' ^ (uint64_t)%s' % v for v, _ in self.fresh_vars)
        self.emit(1, 'return T(h ^ x8 ^ y16 ^ (uint64_t)s8 ^ (uint64_t)s16 ^ z32 ^ w64 ^ (uint64_t)i%s);' % fr)
        self.emit(0, '}')
        decls = ['    %s %s = (%s)(a + %d);' % (t, v, t, j) for j, (v, t) in enumerate(self.fresh_vars)]
        self.lines[head:head] = decls
        return '\n'.join(self.lines) + '\n'

    def fresh(self):
        v = 'q%d' % len(self.fresh_vars)
        self.fresh_vars.append((v, self.r.choice(['uint8_t', 'int8_t', 'uint16_t', 'uint32_t', 'uint64_t'])))
        return v


def program(r, nf):
    out = ['#include <stdint.h>', '#include <stdio.h>', '',
           'static uint64_t tr;',
           '__attribute__((noinline)) static uint64_t T(uint64_t v) { tr = tr * 0x100000001b3ULL ^ v; return v; }', '']
    for k in range(nf):
        out.append(Fn(r, k).text())
    out.append('static const uint64_t vals[] = { %s };' % ', '.join(v + 'ULL' for v in VALUES))
    out.append('int main(void) {')
    out.append('    unsigned nv = sizeof vals / sizeof vals[0];')
    out.append('    for (unsigned i = 0; i < nv; i++) for (unsigned j = 0; j < nv; j += 3) for (unsigned n = 0; n < 4; n++) {')
    for k in range(nf):
        out.append('        tr = 0; { uint64_t v = f%d(vals[i], vals[j], n * 7);' % k)
        out.append('          printf("%d %%u %%u %%u %%llx %%llx\\n", i, j, n, (unsigned long long)v, (unsigned long long)tr); }' % k)
    out.append('    }')
    out.append('    return 0;\n}')
    return '\n'.join(out) + '\n'


def run(cc, src, exe):
    c = subprocess.run([cc, os.environ.get('OPT', '-O2'), '-w', '-o', exe, src], capture_output=True, text=True)
    if c.returncode != 0:
        return 'COMPILE FAILED: ' + c.stderr[:400]
    try:
        p = subprocess.run([exe], capture_output=True, text=True, timeout=30)
    except subprocess.TimeoutExpired:
        return 'TIMEOUT'
    return p.stdout + ('' if p.returncode == 0 else 'EXIT %d' % p.returncode)


def main():
    rnd = random.Random(seed)
    found = 0
    for k in range(count):
        src = os.path.join(W, 'c%d_%d.c' % (seed, k))
        open(src, 'w').write(program(rnd, per))
        if os.environ.get('SAN'):
            exe = os.path.abspath(os.path.join(W, 'xs'))
            c = subprocess.run([CLANG, '-O1', '-w', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                                '-o', exe, src], capture_output=True, text=True)
            p = subprocess.run([exe], capture_output=True, text=True, timeout=120) if c.returncode == 0 else c
            print('SAN', src, c.returncode, p.returncode, p.stderr[:600])
            continue
        ref = run(CLANG, src, os.path.abspath(os.path.join(W, 'xk')))
        if ref.startswith('COMPILE') or ref == 'TIMEOUT':
            print('clang failed', src, ref[:300])
            continue
        got = run(CCC, src, os.path.abspath(os.path.join(W, 'xc')))
        if got != ref:
            o = os.environ.get('OPT', '-O2')
            os.environ['OPT'] = '-O0'
            r0 = run(CLANG, src, os.path.abspath(os.path.join(W, 'x0')))
            os.environ['OPT'] = '-O2'
            rg = run(os.environ.get('GCC', '/opt/homebrew/bin/gcc-15'), src, os.path.abspath(os.path.join(W, 'xg')))
            os.environ['OPT'] = o
            if got == r0 and got == rg:
                print('CLANG-WRONG', src)
                continue
            found += 1
            d = [(p, q) for p, q in zip(ref.split('\n'), got.split('\n')) if p != q]
            print('MISMATCH', src, d[:2] if d else got[:300])
        else:
            os.remove(src)
    print('seed %d: %d of %d files mismatched' % (seed, found, count))


main()
