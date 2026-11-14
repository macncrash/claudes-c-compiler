#!/usr/bin/env python3
"""Conversion fuzzer: integer <-> float/double/long double for every integer
width incl. __int128 (values near powers of two, rounding ties, limits),
double -> float rounding (ties, subnormals, overflow to inf), float ->
integer for in-range values only (UBSan float-cast-overflow), casts to
_Bool, and chains of conversions. Each conversion is done on a runtime
value (through a noinline identity) and on a literal constant, so constant
folding is compared too. Float results are hashed by their bits.
usage: cvfz.py CCC CLANG WORK SEED COUNT   (env OPT, default -O2)"""
import os
import random
import struct
import subprocess
import sys

CCC, CLANG, WORK, SEED, COUNT = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]), int(sys.argv[5])
OPT = os.environ.get('OPT', '-O2')
os.makedirs(WORK, exist_ok=True)

INTS = {'signed char': (8, True), 'unsigned char': (8, False), 'short': (16, True), 'unsigned short': (16, False),
        'int': (32, True), 'unsigned': (32, False), 'long': (64, True), 'unsigned long': (64, False),
        'long long': (64, True), 'unsigned long long': (64, False), '__int128': (128, True),
        'unsigned __int128': (128, False)}
FLTS = ['float', 'double', 'long double']


def int_value(r, bits, signed):
    lo, hi = (-(1 << (bits - 1)), (1 << (bits - 1)) - 1) if signed else (0, (1 << bits) - 1)
    c = r.random()
    if c < 0.3:
        # near a power of two, where float/double rounding happens
        p = r.choice([p for p in (4, 7, 8, 15, 16, 23, 24, 25, 31, 32, 52, 53, 54, 63, 64, 100, 127) if p < bits])
        v = (1 << p) + r.randint(-3, 3) * r.choice([1, (1 << max(0, p - 24)), (1 << max(0, p - 53)), (1 << max(0, p - 25))])
        if signed and r.random() < 0.5:
            v = -v
    elif c < 0.5:
        # exact ties for float (24-bit) or double (53-bit) mantissas
        m = r.choice([24, 53])
        e = r.randint(m + 1, bits - 1) if bits - 1 > m + 1 else bits - 1
        v = (r.getrandbits(m) | (1 << (m - 1))) << (e - m) | (1 << (e - m - 1)) if e > m else r.getrandbits(bits - 1)
        if signed and r.random() < 0.5:
            v = -v
    elif c < 0.6:
        v = r.choice([lo, hi, lo + 1, hi - 1, 0, 1, -1])
    else:
        v = r.randint(lo, hi)
    return max(lo, min(hi, v))


def int_lit(v, t):
    bits, signed = INTS[t]
    if bits == 128:
        hi, lo = (v >> 64) & ((1 << 64) - 1), v & ((1 << 64) - 1)
        return '((%s)(((unsigned __int128)0x%xull << 64) | 0x%xull))' % (t, hi, lo)
    if v < 0:
        return '((%s)(-0x%xll - 1))' % (t, -v - 1)
    return '((%s)0x%xull)' % (t, v)


def flt_value(r):
    c = r.random()
    if c < 0.25:
        # double that rounds to float at a tie or near one
        m = r.getrandbits(24) | (1 << 23)
        e = r.randint(-140, 127)
        bits = (m << 29) | (r.choice([1 << 28, (1 << 28) - 1, (1 << 28) + 1, 0, 1]))
        return bits * 2.0 ** (e - 52)
    if c < 0.4:
        return r.choice([0.5, 1.5, 2.5, -0.5, -2.5, 3.4028235677973366e38, 3.4028234663852886e38,
                         1.1754943508222875e-38, 1.401298464324817e-45, 7.006492321624085e-46,
                         2.1019476964872256e-45, 9.007199254740993e15, 1.8446744073709552e19,
                         9.223372036854775e18, -9.223372036854776e18, 4294967295.5, 2147483647.5,
                         -2147483648.5, 0.9999999999999999, 65535.99, -0.0])
    if c < 0.7:
        return r.uniform(-1e6, 1e6)
    return r.uniform(-2.0, 2.0) * 2.0 ** r.randint(-150, 140)


def flt_lit(x):
    return '%a' % x if False else float.hex(x)


def fits(x, t):
    """x truncated toward zero is in range of integer type t (no UB)."""
    bits, signed = INTS[t]
    lo, hi = (-(1 << (bits - 1)), (1 << (bits - 1)) - 1) if signed else (0, (1 << bits) - 1)
    if x != x or x in (float('inf'), float('-inf')):
        return False
    return lo - 1 < x < hi + 1


NEEDS = []


def gen(r):
    NEEDS.clear()
    lines = []
    ops = []
    for k in range(r.randint(8, 24)):
        c = r.randint(0, 4)
        if c <= 1:
            # integer -> floating
            it = r.choice(list(INTS))
            ft = r.choice(FLTS)
            v = int_value(r, *INTS[it])
            lit = int_lit(v, it)
            ops.append('{ %s v = id_%s(%s); HF((%s)v); HF((%s)%s); }' % (it, mangle(it), lit, ft, ft, lit))
        elif c == 2:
            # floating -> floating
            x = flt_value(r)
            src = r.choice(['double', 'long double'])
            dst = r.choice(['float', 'double'])
            ops.append('{ %s v = id_%s(%s); HF((%s)v); HF((%s)(%s)%s); }' % (src, mangle(src), flt_lit(x), dst, dst, src, flt_lit(x)))
        elif c == 3:
            # floating -> integer, in range only
            x = flt_value(r)
            ft = r.choice(FLTS)
            if ft == 'float':
                x = struct.unpack('f', struct.pack('f', max(-3.4e38, min(3.4e38, x))))[0]
            it = r.choice(list(INTS))
            if not fits(x, it):
                x = r.uniform(-100, 100) if INTS[it][1] else r.uniform(0, 200)
                if ft == 'float':
                    x = struct.unpack('f', struct.pack('f', x))[0]
            ops.append('{ %s v = id_%s((%s)%s); HI((%s)v); HI((%s)(%s)%s); HI((_Bool)v); }' % (
                ft, mangle(ft), ft, flt_lit(x), it, it, ft, flt_lit(x)))
        elif c == 4 and r.random() < 0.6:
            # implicit conversions: compound assignment, comparisons, returns
            it = r.choice(list(INTS))
            ft = r.choice(FLTS)
            v = int_value(r, *INTS[it])
            lit = int_lit(v, it)
            k2 = r.randint(0, 3)
            if k2 == 0:
                # i op= x: computed in floating point, converted back (in range)
                signed = INTS[it][1]
                x = r.uniform(-3, 3) if signed else r.uniform(0, 3)
                op = r.choice(['+=', '-=', '*=']) if signed else r.choice(['+=', '*='])
                ops.append('{ %s y = (%s)(id_%s(%s) %% 32); %s x = id_%s((%s)%s); y %s x; HI(y); HF((%s)y + x); }' % (
                    it, it, mangle(it), lit, ft, mangle(ft), ft, flt_lit(x), op, ft))
            elif k2 == 1:
                # integer vs floating comparisons where the integer is inexact
                ops.append('{ %s v = id_%s(%s); %s x = (%s)v; HI(v == x); HI(v < x); HI(v > x); HI((%s)x == v); }' % (
                    it, mangle(it), lit, ft, ft, ft))
            elif k2 == 2:
                # return-value and argument conversions
                ops.append('{ HF(cv_%s_%s(id_%s(%s))); }' % (mangle(it), mangle(ft), mangle(it), lit))
                need = ('%s cv_%s_%s(%s x)' % (ft, mangle(it), mangle(ft), it), 'return x;')
                if need not in NEEDS:
                    NEEDS.append(need)
            else:
                # float promoted through varargs, and back
                x = flt_value(r)
                ops.append('{ float f = id_float((float)%s); HF(va_back(1, f)); }' % flt_lit(max(-3.4e38, min(3.4e38, x))))
        else:
            # chains: int -> float -> double -> int
            it = r.choice(['int', 'unsigned', 'long long', 'unsigned long long', 'short'])
            v = int_value(r, *INTS[it])
            lit = int_lit(v, it)
            ops.append('{ %s v = id_%s(%s); float f = v; double d = f; HF(f); HF(d); HF(d - (double)v); HF((float)(double)v); }' % (
                it, mangle(it), lit))
    return ops


def mangle(t):
    return t.replace(' ', '_')


def gen_file(r):
    top = ['#include <stdio.h>', '#include <string.h>', '#include <stdarg.h>',
           'unsigned long long h;',
           'static void hb(const void *p, unsigned n) { const unsigned char *b = p; for (unsigned i = 0; i < n; i++) h = h * 1000003u + b[i]; }',
           '#define HF(x) do { __typeof__(x) t_ = (x); hb(&t_, sizeof t_ > 8 ? 8 : sizeof t_); h += (t_ != t_); } while (0)',
           '#define HI(x) do { __typeof__(x) t_ = (x); hb(&t_, sizeof t_); } while (0)']
    for t in list(INTS) + FLTS:
        top.append('__attribute__((noinline)) %s id_%s(%s x) { return x; }' % (t, mangle(t), t))
    body = gen(r)
    for sig, b in NEEDS:
        top.append('__attribute__((noinline)) %s { %s }' % (sig, b))
    top.append('__attribute__((noinline)) double va_back(int n, ...) { va_list ap; va_start(ap, n); double d = va_arg(ap, double); va_end(ap); return d + n - 1; }')
    top.append('__attribute__((noinline)) void f(void)\n{\n    %s\n}' % '\n    '.join(body))
    top.append('int main(void)\n{\n    f();\n    printf("%llu\\n", h);\n    return 0;\n}')
    return '\n'.join(top) + '\n'


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
    ub = run([CLANG, '-O0', '-w', '-fsanitize=undefined,address,float-cast-overflow', '-fno-sanitize-recover=all', f, '-o', f + '.ub'])
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
