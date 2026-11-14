#!/usr/bin/env python3
"""Random loop nests over arrays, ccc vs clang.

usage: loopfuzz.py CCC CLANG WORKDIR SEED COUNT [FUNCS]   (OPT=-O2 default)

Each function gets two runtime values, a trip bound n <= 60, and two pointers
into global buffers at runtime offsets (possibly the same buffer, so they
may overlap).  Loops of several shapes read and write a local array and the
two pointers; every index stays in bounds and every operation is defined.
"""
import os
import random
import subprocess
import sys

CCC, CLANG, W = sys.argv[1], sys.argv[2], sys.argv[3]
seed, count = int(sys.argv[4]), int(sys.argv[5])
per = int(sys.argv[6]) if len(sys.argv) > 6 else 4

# name, bits, signed
ETYPES = [('int8_t', 8, 1), ('uint8_t', 8, 0), ('int16_t', 16, 1), ('uint16_t', 16, 0),
          ('int32_t', 32, 1), ('uint32_t', 32, 0), ('int64_t', 64, 1), ('uint64_t', 64, 0)]
BUFN = 96   # global buffer length; offsets <= 32, indices < 64
LOCN = 64


def ut(t):
    return 'uint32_t' if t[1] <= 32 else 'uint64_t'


class Fn:
    def __init__(self, r, k):
        self.r = r
        self.k = k
        self.tl = r.choice(ETYPES)   # local array a
        self.tp = r.choice(ETYPES)   # *p
        self.tq = self.tp if r.random() < 0.6 else r.choice(ETYPES)
        # A local struct whose address escapes (see text and sstmt), and
        # the element type of its buffer.
        self.st = r.random() < 0.3
        self.tb = r.choice(ETYPES)
        self.lines = []
        self.depth = 0
        self.ivs = []       # (name, max value exclusive) of enclosing loops
        self.nvar = 0

    def emit(self, s):
        self.lines.append('    ' * (self.depth + 1) + s)

    def fresh(self, p):
        self.nvar += 1
        return '%s%d' % (p, self.nvar)

    # Arrays and in-bounds indices.  An index is built from a loop variable
    # whose values are below its bound; the array has room for bound + 3.
    def arrays(self):
        return [('a', self.tl, LOCN), ('p', self.tp, BUFN - 32), ('q', self.tq, BUFN - 32)]

    def index(self, size):
        r = self.r
        if not self.ivs or r.random() < 0.1:
            return str(r.randint(0, size - 1))
        iv, hi = r.choice(self.ivs)
        c = r.randint(0, 3)
        choice = r.random()
        if choice < 0.5 and hi + c <= size:
            return iv if c == 0 else '%s + %d' % (iv, c)
        if choice < 0.65 and hi <= size:
            return '%d - %s' % (hi - 1, iv)
        m = 63 if size >= 64 else 31
        return '((%s * %d + %d) & %d)' % (iv, r.choice([1, 2, 3, 5, 7, 8]), r.randint(0, 9), m)

    def elem(self):
        name, t, size = self.r.choice(self.arrays())
        return '%s[%s]' % (name, self.index(size)), t, name, size

    def leaf(self):
        r = self.r
        x = r.random()
        if self.st and r.random() < 0.25:
            return self.sleaf()
        if x < 0.55:
            e, t, _, _ = self.elem()
            return e, t
        if x < 0.7 and self.ivs:
            return '(int)%s' % r.choice(self.ivs)[0], ('int', 32, 1)
        if x < 0.8:
            return r.choice(['s', 'pa', 'pb']), ('uint64_t', 64, 0)
        if x < 0.85:
            return 't', ('uint32_t', 32, 0)
        return str(r.choice([0, 1, 2, 3, 5, 7, 100, 255, 1000, 65535, -1, -3, -128])), ('int', 32, 1)

    def expr(self, d):
        """An expression and its type; defined for every operand value."""
        r = self.r
        if d <= 0:
            return self.leaf()
        x = r.random()
        if x < 0.35:
            a, ta = self.expr(d - 1)
            b, tb = self.expr(d - 1)
            op = r.choice(['+', '-', '*', '^', '&', '|'])
            # Promoted to int: only uint16 * uint16 can overflow it.
            u16 = ta[1] == 16 and tb[1] == 16 and not ta[2] and not tb[2]
            if ta[1] <= 16 and tb[1] <= 16 and d == 1 and not (op == '*' and u16):
                return '(%s %s %s)' % (a, op, b), ('int', 32, 1)
            u = 'uint64_t' if 64 in (ta[1], tb[1]) or r.random() < 0.3 else 'uint32_t'
            return '((%s)%s %s (%s)%s)' % (u, a, op, u, b), (u, 64 if u == 'uint64_t' else 32, 0)
        if x < 0.5:
            a, ta = self.expr(d - 1)
            b, tb = self.expr(d - 1)
            op = r.choice(['<', '<=', '==', '!=', '>', '>='])
            return '(%s %s %s)' % (a, op, b), ('int', 32, 1)
        if x < 0.6:
            a, ta = self.expr(d - 1)
            u = ut(ta)
            bits = 32 if u == 'uint32_t' else 64
            if r.random() < 0.5:
                return '((%s)%s << %d)' % (u, a, r.randint(0, bits - 1)), (u, bits, 0)
            return '((%s)%s >> %d)' % (u, a, r.randint(0, bits - 1)), (u, bits, 0)
        if x < 0.68:
            a, ta = self.expr(d - 1)
            u = ut(ta)
            c = r.choice([3, 7, 10, 16, 255, 1000])
            return '((%s)%s %s %d)' % (u, a, r.choice('/%'), c), (u, 32 if u == 'uint32_t' else 64, 0)
        if x < 0.78:
            c, _ = self.expr(d - 1)
            a, ta = self.expr(d - 1)
            b, tb = self.expr(d - 1)
            return '(%s ? (uint64_t)%s : (uint64_t)%s)' % (c, a, b), ('uint64_t', 64, 0)
        if x < 0.86:
            t = r.choice(ETYPES)
            a, _ = self.expr(d - 1)
            return '((%s)%s)' % (t[0], a), t
        return self.leaf()

    def store(self):
        r = self.r
        e, t, name, size = self.elem()
        v, tv = self.expr(r.randint(0, 2))
        x = r.random()
        if x < 0.5:
            self.emit('%s = (%s)%s;' % (e, t[0], v))
        elif x < 0.75:
            self.emit('%s %s (%s)%s;' % (e, r.choice(['^=', '|=', '&=']), t[0], v))
        else:
            # Wrapping arithmetic through the unsigned type.
            u = ut(t)
            self.emit('%s = (%s)((%s)%s %s (%s)%s);' % (e, t[0], u, e, r.choice('+-*'), u, v))

    def stmt(self, budget):
        r = self.r
        x = r.random()
        if self.st and r.random() < 0.3:
            self.sstmt()
        elif x < 0.35:
            self.store()
        elif x < 0.55:
            v, _ = self.expr(r.randint(0, 2))
            y = r.random()
            if y < 0.5:
                self.emit('s += (uint64_t)%s;' % v)
            elif y < 0.7:
                self.emit('t ^= (uint32_t)%s;' % v)
            elif y < 0.85:
                self.emit('s = (uint64_t)%s > s ? (uint64_t)%s : s;' % (v, v))
            else:
                self.emit('t = t * 31u + (uint32_t)%s;' % v)
        elif x < 0.65 and self.ivs:
            c, _ = self.expr(r.randint(1, 2))
            self.emit('if (%s) %s;' % (c, r.choice(['break', 'continue', 'cnt++'])))
        elif x < 0.75:
            c, _ = self.expr(r.randint(1, 2))
            self.emit('if (%s) {' % c)
            self.depth += 1
            self.stmt(budget - 1)
            self.depth -= 1
            if r.random() < 0.5:
                self.emit('} else {')
                self.depth += 1
                self.stmt(budget - 1)
                self.depth -= 1
            self.emit('}')
        elif budget > 0 and len(self.ivs) < 3:
            self.loop(budget - 1)
        else:
            self.store()

    def bound(self):
        """(lo, hi expression, max hi) with hi <= 60."""
        r = self.r
        if self.ivs and r.random() < 0.3:
            iv, hi = r.choice(self.ivs)
            return '0', '%s + 1' % iv, hi
        x = r.random()
        lo = r.choice(['0', '0', '0', '1', '2', '(n & 3)'])
        if x < 0.5:
            return lo, 'n', 61
        if x < 0.65:
            return lo, '(n / 2)', 31
        if x < 0.8:
            c = r.randint(0, 60)
            return lo, str(c), c + 1
        return lo, '(n < %d ? n : %d)' % ((c := r.randint(1, 60)), c), c + 1

    def loop(self, budget):
        r = self.r
        lo, hi, mx = self.bound()
        iv = self.fresh('i')
        kind = r.random()
        ity = r.choice(['int', 'unsigned', 'long', 'size_t', 'unsigned char', 'uint32_t', 'int64_t'])
        if r.random() < float(os.environ.get('INPLACE', '0.3')):
            self.inplace()
            return
        if r.random() < float(os.environ.get('KERNEL', '0.25')):
            self.kernel(iv, ity, lo, hi, mx)
            return
        if r.random() < 0.15:
            # A short constant trip count, for full unrolling.
            c0 = r.randint(0, 4)
            c1 = c0 + r.randint(0, 9)
            lo, hi, mx = str(c0), str(c1), c1 + 1
        if kind < 0.45:
            step = r.choice([1, 1, 1, 2, 3])
            signed = ity in ('int', 'long', 'int64_t')
            y = r.random()
            if y < 0.15 and step == 1 and lo == '0':
                self.emit('for (%s %s = 0; %s != %s; %s++) {' % (ity, iv, iv, hi, iv))
            elif y < 0.3 and signed:
                self.emit('for (%s %s = %s; %s <= (%s)(%s) - 1; %s += %d) {' % (ity, iv, lo, iv, ity, hi, iv, step))
            else:
                self.emit('for (%s %s = %s; %s < %s; %s += %d) {' % (ity, iv, lo, iv, hi, iv, step))
        elif kind < 0.6:
            if ity in ('int', 'long', 'int64_t'):
                self.emit('for (%s %s = (%s)(%s) - 1; %s >= (%s)%s; %s--) {' % (ity, iv, ity, hi, iv, ity, lo, iv))
            else:
                self.emit('for (%s %s = %s; %s-- > %s;) {' % (ity, iv, hi, iv, lo))
        elif kind < 0.72:
            self.emit('%s %s = %s;' % (ity, iv, lo))
            self.emit('while (%s < %s) {' % (iv, hi))
            self.depth += 1
            self.ivs.append((iv, mx))
            for _ in range(r.randint(1, 3)):
                self.stmt(budget)
            self.emit('%s++;' % iv)
            self.ivs.pop()
            self.depth -= 1
            self.emit('}')
            return
        elif kind < 0.82:
            # A pointer walk over a, with the offset as the index.
            pv = self.fresh('e')
            t = self.tl[0]
            self.emit('for (%s *%s = a + %s; %s < a + %s; %s++) {' % (t, pv, lo, pv, hi, pv))
            self.depth += 1
            self.emit('long %s = %s - a;' % (iv, pv))
            v, _ = self.expr(r.randint(0, 2))
            self.ivs.append((iv, mx))
            self.emit('*%s = (%s)(%s);' % (pv, t, v) if r.random() < 0.5 else 's += (uint64_t)*%s;' % pv)
            for _ in range(r.randint(0, 2)):
                self.stmt(budget)
            self.ivs.pop()
            self.depth -= 1
            self.emit('}')
            return
        else:
            self.emit('%s %s = %s;' % (ity, iv, lo))
            self.emit('if (%s < %s) do {' % (iv, hi))
            self.depth += 1
            self.ivs.append((iv, mx))
            for _ in range(r.randint(1, 3)):
                self.stmt(budget)
            self.ivs.pop()
            self.depth -= 1
            self.emit('} while (++%s < %s);' % (iv, hi))
            return
        self.depth += 1
        self.ivs.append((iv, mx))
        for _ in range(r.randint(1, 3)):
            self.stmt(budget)
        self.ivs.pop()
        self.depth -= 1
        self.emit('}')

    def inplace(self):
        """x[i] = x[i] op w over one array: the shape loop_vectorize takes,
        and its near misses (w read from the array inside the loop, the
        count-down pointer walk, the saturating subtract, odd widths)."""
        r = self.r
        name, t, size = r.choice(self.arrays())
        off = r.randint(0, 3)
        x = self.fresh('x')
        m = self.fresh('m')
        w = self.fresh('w')
        if t[1] == 64:
            wt = 'uint64_t'
        elif t[1] == 32:
            wt = r.choice(['uint32_t', 'uint64_t'])
        else:
            wt = r.choice(['uint32_t', 'uint64_t', 'uint8_t', 'int8_t', 'unsigned', t[0]])
            if wt == 'uint16_t':
                wt = 'uint32_t'   # uint16 * uint16 overflows int
        mexpr = r.choice(['n', 'n', '(n / 2)', str(r.randint(0, 60)), '(n < 9 ? n : 9)', '(n | 8)'])
        wv = r.choice(['pa', 'pb', '(pa >> 13)', '(pb >> 50)', str(r.choice([1, 2, 3, 7, 8, 255, 256, 1000, 65535, 0x8000]))])
        self.emit('{')
        self.depth += 1
        self.emit('%s *%s = %s + %d; unsigned %s = %s; %s %s = (%s)%s;' % (t[0], x, name, off, m, mexpr, wt, w, wt, wv))
        # The operand: the invariant, or (a near miss) an element of the
        # same array, read inside the loop after earlier iterations wrote it.
        if r.random() < 0.2:
            wexpr = '(%s)%s[%d]' % (wt, x, r.randint(0, 20))
        else:
            wexpr = w
        sat = r.random() < 0.25
        op = r.choice(['+', '-', '*', '&', '|', '^'])

        def f(e):
            if sat:
                return '(%s)(%s >= %s ? %s - %s : 0)' % (t[0], e, wexpr, e, wexpr)
            return '(%s)(%s %s %s)' % (t[0], e, op, wexpr)
        k = r.random()
        i = self.fresh('i')
        if k < 0.35:
            self.emit('for (unsigned %s = 0; %s < %s; %s++) %s[%s] = %s;' % (i, i, m, i, x, i, f('%s[%s]' % (x, i))))
        elif k < 0.6:
            y = self.fresh('y')
            self.emit('%s *%s = %s + %s; unsigned %s = %s;' % (t[0], y, x, m, i, m))
            self.emit('if (%s) do { %s--; *%s = %s; } while (--%s);' % (i, y, y, f('*' + y), i))
        elif k < 0.8:
            y = self.fresh('y')
            self.emit('for (%s *%s = %s; %s < %s + %s; %s++) *%s = %s;' % (t[0], y, x, y, x, m, y, y, f('*' + y)))
        else:
            self.emit('for (int %s = (int)%s - 1; %s >= 0; %s--) %s[%s] = %s;' % (i, m, i, i, x, i, f('%s[%s]' % (x, i))))
        self.depth -= 1
        self.emit('}')

    def inplace(self):
        """x[i] = x[i] op w over one array: the shape loop_vectorize takes,
        and its near misses (w read from the array inside the loop, the
        count-down pointer walk, the saturating subtract, odd widths)."""
        r = self.r
        name, t, size = r.choice(self.arrays())
        off = r.randint(0, 3)
        x = self.fresh('x')
        m = self.fresh('m')
        w = self.fresh('w')
        if t[1] == 64:
            wt = 'uint64_t'
        elif t[1] == 32:
            wt = r.choice(['uint32_t', 'uint64_t'])
        else:
            wt = r.choice(['uint32_t', 'uint64_t', 'uint8_t', 'int8_t', 'unsigned', t[0]])
            if wt == 'uint16_t':
                wt = 'uint32_t'   # uint16 * uint16 overflows int
        mexpr = r.choice(['n', 'n', '(n / 2)', str(r.randint(0, 60)), '(n < 9 ? n : 9)', '(n | 8)'])
        wv = r.choice(['pa', 'pb', '(pa >> 13)', '(pb >> 50)', str(r.choice([1, 2, 3, 7, 8, 255, 256, 1000, 65535, 0x8000]))])
        self.emit('{')
        self.depth += 1
        self.emit('%s *%s = %s + %d; unsigned %s = %s; %s %s = (%s)%s;' % (t[0], x, name, off, m, mexpr, wt, w, wt, wv))
        # The operand: the invariant, or (a near miss) an element of the
        # same array, read inside the loop after earlier iterations wrote it.
        if r.random() < 0.2:
            wexpr = '(%s)%s[%d]' % (wt, x, r.randint(0, 20))
        else:
            wexpr = w
        sat = r.random() < 0.25
        op = r.choice(['+', '-', '*', '&', '|', '^'])

        def f(e):
            if sat:
                return '(%s)(%s >= %s ? %s - %s : 0)' % (t[0], e, wexpr, e, wexpr)
            return '(%s)(%s %s %s)' % (t[0], e, op, wexpr)
        k = r.random()
        i = self.fresh('i')
        if k < 0.35:
            self.emit('for (unsigned %s = 0; %s < %s; %s++) %s[%s] = %s;' % (i, i, m, i, x, i, f('%s[%s]' % (x, i))))
        elif k < 0.6:
            y = self.fresh('y')
            self.emit('%s *%s = %s + %s; unsigned %s = %s;' % (t[0], y, x, m, i, m))
            self.emit('if (%s) do { %s--; *%s = %s; } while (--%s);' % (i, y, y, f('*' + y), i))
        elif k < 0.8:
            y = self.fresh('y')
            self.emit('for (%s *%s = %s; %s < %s + %s; %s++) *%s = %s;' % (t[0], y, x, y, x, m, y, y, f('*' + y)))
        else:
            self.emit('for (int %s = (int)%s - 1; %s >= 0; %s--) %s[%s] = %s;' % (i, m, i, i, x, i, f('%s[%s]' % (x, i))))
        self.depth -= 1
        self.emit('}')

    def kernel(self, iv, ity, lo, hi, mx):
        """A straight-line loop of the shapes a vectorizer takes."""
        r = self.r
        arrs = self.arrays()

        def ref(off_ok=True):
            name, t, size = r.choice(arrs)
            c = r.randint(0, 3) if off_ok and r.random() < 0.4 and mx + 3 <= size else 0
            return ('%s[%s + %d]' % (name, iv, c) if c else '%s[%s]' % (name, iv)), t

        def val():
            x = r.random()
            if x < 0.6:
                return ref()
            if x < 0.8:
                return str(r.choice([1, 3, -1, 7, 255, 1000, -5])), ('int', 32, 1)
            return '(int)%s' % iv, ('int', 32, 1)

        self.emit('for (%s %s = %s; %s < %s; %s++) {' % (ity, iv, lo, iv, hi, iv))
        self.depth += 1
        for _ in range(r.randint(1, 3)):
            x = r.random()
            a, ta = val()
            b, tb = val()
            if x < 0.5:
                d, td = ref()
                op = r.choice(['+', '-', '*', '&', '|', '^', '<<', '>>', 'min', 'max', 'avg', '?'])
                u = ut(td) if r.random() < 0.7 else 'uint64_t'
                if op in ('<<', '>>'):
                    e = '(%s)%s %s %d' % (u, a, op, r.randint(0, 7))
                elif op == 'min':
                    e = '%s < %s ? %s : %s' % (a, b, a, b)
                elif op == 'max':
                    e = '%s > %s ? %s : %s' % (a, b, a, b)
                elif op == 'avg':
                    e = '((%s)%s + (%s)%s + 1) >> 1' % ('uint64_t', a, 'uint64_t', b)
                elif op == '?':
                    c, _ = val()
                    e = '%s > %s ? (%s)%s : (%s)%s' % (a, b, u, c, u, a)
                else:
                    e = '(%s)%s %s (%s)%s' % (u, a, op, u, b)
                self.emit('%s = (%s)(%s);' % (d, td[0], e))
            elif x < 0.7:
                self.emit('s += (uint64_t)%s * (uint64_t)%s;' % (a, b))
            elif x < 0.8:
                self.emit('t += (uint32_t)%s;' % a)
            elif x < 0.9:
                # A min or max reduction in the element's own type.
                self.emit('t = (uint32_t)%s %s t ? (uint32_t)%s : t;' % (a, r.choice('<>'), a))
            else:
                d, td = ref(False)
                self.emit('%s += (%s)%s;' % (d, td[0], a) if td[1] <= 16 or not td[2] else '%s ^= (%s)%s;' % (d, td[0], a))
        self.depth -= 1
        self.emit('}')

    def kernel(self, iv, ity, lo, hi, mx):
        """A straight-line loop of the shapes a vectorizer takes."""
        r = self.r
        arrs = self.arrays()

        def ref(off_ok=True):
            name, t, size = r.choice(arrs)
            c = r.randint(0, 3) if off_ok and r.random() < 0.4 and mx + 3 <= size else 0
            return ('%s[%s + %d]' % (name, iv, c) if c else '%s[%s]' % (name, iv)), t

        def val():
            x = r.random()
            if x < 0.6:
                return ref()
            if x < 0.8:
                return str(r.choice([1, 3, -1, 7, 255, 1000, -5])), ('int', 32, 1)
            return '(int)%s' % iv, ('int', 32, 1)

        self.emit('for (%s %s = %s; %s < %s; %s++) {' % (ity, iv, lo, iv, hi, iv))
        self.depth += 1
        for _ in range(r.randint(1, 3)):
            x = r.random()
            a, ta = val()
            b, tb = val()
            if x < 0.5:
                d, td = ref()
                op = r.choice(['+', '-', '*', '&', '|', '^', '<<', '>>', 'min', 'max', 'avg', '?'])
                u = ut(td) if r.random() < 0.7 else 'uint64_t'
                if op in ('<<', '>>'):
                    e = '(%s)%s %s %d' % (u, a, op, r.randint(0, 7))
                elif op == 'min':
                    e = '%s < %s ? %s : %s' % (a, b, a, b)
                elif op == 'max':
                    e = '%s > %s ? %s : %s' % (a, b, a, b)
                elif op == 'avg':
                    e = '((%s)%s + (%s)%s + 1) >> 1' % ('uint64_t', a, 'uint64_t', b)
                elif op == '?':
                    c, _ = val()
                    e = '%s > %s ? (%s)%s : (%s)%s' % (a, b, u, c, u, a)
                else:
                    e = '(%s)%s %s (%s)%s' % (u, a, op, u, b)
                self.emit('%s = (%s)(%s);' % (d, td[0], e))
            elif x < 0.7:
                self.emit('s += (uint64_t)%s * (uint64_t)%s;' % (a, b))
            elif x < 0.8:
                self.emit('t += (uint32_t)%s;' % a)
            elif x < 0.9:
                # A min or max reduction in the element's own type.
                self.emit('t = (uint32_t)%s %s t ? (uint32_t)%s : t;' % (a, r.choice('<>'), a))
            else:
                d, td = ref(False)
                self.emit('%s += (%s)%s;' % (d, td[0], a) if td[1] <= 16 or not td[2] else '%s ^= (%s)%s;' % (d, td[0], a))
        self.depth -= 1
        self.emit('}')

    def text(self):
        r = self.r
        k, tb = self.k, self.tb[0]
        pre = []
        if self.st:
            # A local struct whose address escapes: to a call, and to a global
            # the loop may write through.
            pre = ['struct S%d { uint32_t n; %s buf[16]; uint16_t h; uint8_t c; };' % (k, tb),
                   'static struct S%d *esc%d;' % (k, k),
                   '__attribute__((noinline)) static void touch%d(struct S%d *z) { esc%d = z; z->h ^= (uint16_t)(z->n * 3); z->c++; }'
                   % (k, k, k)]
        head = ['__attribute__((noinline)) uint64_t f%d(uint64_t pa, uint64_t pb, unsigned n, %s *p, %s *q) {'
                % (self.k, self.tp[0], self.tq[0]),
                '    uint64_t s = pa; uint32_t t = (uint32_t)pb; unsigned cnt = 0;',
                '    %s a[%d];' % (self.tl[0], LOCN),
                '    for (unsigned k = 0; k < %d; k++) a[k] = (%s)(pa * (k + 1) ^ (pb >> (k & 15)));' % (LOCN, self.tl[0])]
        if self.st:
            head += ['    struct S%d L; memset(&L, 0, sizeof L); L.n = (uint32_t)pa & 7; L.h = (uint16_t)pb; L.c = 1;' % k,
                     '    for (unsigned k = 0; k < 16; k++) L.buf[k] = (%s)(pb + k * k);' % tb,
                     '    touch%d(&L);' % k]
        for _ in range(r.randint(1, 4)):
            self.loop(2)
        tail = ['    for (unsigned k = 0; k < %d; k++) s = s * 3 + (uint64_t)a[k];' % LOCN]
        if self.st:
            tail += ['    s = s * 7 + L.n; s = s * 7 + L.h; s = s * 7 + L.c;',
                     '    for (unsigned k = 0; k < 16; k++) s = s * 5 + (uint64_t)L.buf[k];']
        tail += ['    return s ^ ((uint64_t)t << 7) ^ cnt;', '}']
        return '\n'.join(pre + head + self.lines + tail)

    def sleaf(self):
        r = self.r
        k = self.k
        return r.choice([('L.n', ('uint32_t', 32, 0)), ('L.h', ('uint16_t', 16, 0)), ('L.c', ('uint8_t', 8, 0)),
                         ('L.buf[(L.n + %d) & 15]' % r.randint(0, 15), self.tb),
                         ('esc%d->n' % k, ('uint32_t', 32, 0)),
                         ('((unsigned char *)&L)[%d]' % r.randint(0, 3), ('uint8_t', 8, 0))])

    def sstmt(self):
        r = self.r
        k, tb = self.k, self.tb
        v, _ = self.expr(r.randint(0, 2))
        u = ut(tb)
        self.emit(r.choice([
            'L.buf[L.n & 15] = (%s)((%s)L.buf[L.n & 15] + (%s)%s); L.n++;' % (tb[0], u, u, v),
            'L.n += (uint32_t)%s;' % v,
            'L.h = (uint16_t)(L.h * 3 + (uint16_t)%s);' % v,
            '((unsigned char *)&L)[%d] ^= (unsigned char)%s;' % (r.randint(0, 3), v),
            '{ uint32_t *pn = &L.n; *pn = *pn * 5 + 1; }',
            'esc%d->n += 2;' % k,
            'esc%d->buf[%d] = (%s)%s;' % (k, r.randint(0, 15), tb[0], v),
            'touch%d(&L);' % k,
            '{ struct S%d M = L; M.n ^= 1; M.c += 2; L = M; }' % k,
            'if (L.n & 1) L.c++; else L.n++;',
        ]))


VALUES = ['0', '1', '2', '7', '0x7f', '0x80', '0xff', '0x8000', '0xfff9', '0x12345678', '0x80000000',
          '0xffffffff', '0xfffffffffffffff9', '0x8000000000000000', '0x7fffffffffffffff', '0x0123456789abcdef']


def program(r, nf):
    out = ['#include <stdint.h>', '#include <stddef.h>', '#include <stdio.h>', '#include <string.h>', '']
    fns = []
    for k in range(nf):
        f = Fn(r, k)
        out.append(f.text())
        fns.append(f)
    out.append('static uint64_t vals[] = { %s };' % ', '.join(v + 'ULL' for v in VALUES))
    for t in ETYPES:
        out.append('static %s B_%s[2][%d];' % (t[0], t[0], BUFN))
    out.append('static void fill(void) {')
    for t in ETYPES:
        out.append('    for (unsigned k = 0; k < %d; k++) { B_%s[0][k] = (%s)(k * 0x9e3779b97f4a7c15ULL >> 21); '
                   'B_%s[1][k] = (%s)(k * 77 - 1000); }' % (BUFN, t[0], t[0], t[0], t[0]))
    out.append('}')
    out.append('static uint64_t sum(void) {\n    uint64_t h = 0;')
    for t in ETYPES:
        out.append('    for (unsigned k = 0; k < %d; k++) h = h * 1000003 ^ (uint64_t)B_%s[0][k] ^ ((uint64_t)B_%s[1][k] << 3);'
                   % (BUFN, t[0], t[0]))
    out.append('    return h;\n}')
    out.append('int main(void) {')
    out.append('    unsigned nv = sizeof vals / sizeof vals[0];')
    out.append('    static const unsigned ns[] = { 0, 1, 2, 3, 4, 5, 7, 8, 9, 15, 16, 17, 31, 32, 33, 47, 59, 60 };')
    out.append('    static const unsigned offs[][2] = { {0, 0}, {0, 1}, {1, 0}, {0, 4}, {4, 0}, {3, 3}, {0, 32}, {17, 2} };')
    out.append('    for (unsigned i = 0; i < nv; i += 3) for (unsigned j = 0; j < 18; j++) for (unsigned o = 0; o < 8; o++) {')
    for k, f in enumerate(fns):
        same = 0 if f.tp == f.tq else 1
        out.append('        fill();')
        out.append('        { uint64_t v = f%d(vals[i], vals[(i + j) %% nv], ns[j], &B_%s[0][offs[o][0]], &B_%s[%d][offs[o][1]]);'
                   % (k, f.tp[0], f.tq[0], same))
        out.append('          printf("%d %%u %%u %%u %%llx %%llx\\n", i, j, o, (unsigned long long)v, (unsigned long long)sum()); }' % k)
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
        src = os.path.join(W, 'l%d_%d.c' % (seed, k))
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
            # Apple clang 16's vectorizer has miscompiled loops this
            # generator makes: a mismatch counts only if clang -O0 and
            # gcc-15 -O2 agree with clang and not with ccc.
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
