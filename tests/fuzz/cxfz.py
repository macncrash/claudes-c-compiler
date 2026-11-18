#!/usr/bin/env python3
"""Runtime _Complex arithmetic: expression trees over double _Complex,
float _Complex, double, float and int operands (+ - * /, unary -, ~ as
conj, creal/cimag, casts between the complex types and to/from reals,
compound assignments, ==/!=, ?: on a complex condition), in noinline
functions taking and returning complex values, called on small values
whose products are exact. Printed at 10 significant digits so a
division's last-bit rounding (clang's __divdc3 scaling) does not count.
ccc vs clang, both -ffp-contract=off.

usage: cxfz.py CCC CLANG WORKDIR SEED COUNT
env: OPT (default -O2)
"""
import os
import re
import random
import subprocess
import sys

CCC, CLANG, WORK, SEED, COUNT = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]), int(sys.argv[5])
OPT = os.environ.get('OPT', '-O2')
os.makedirs(WORK, exist_ok=True)

CVARS = ['a', 'b', 'c', '(double _Complex)f', 'g']
RVARS = ['x', '(double)y', '(double)k', 'y', 'k']
NICE = ['(1.0 + 1.0i)', '(2.0 - 0.5i)', '(0.5i)', '(3.0 + 0.0i)', '(-1.5 + 2.0i)', '1.0i']


def leaf(r, complex_ok=True):
    k = r.random()
    if complex_ok and k < 0.55:
        return r.choice(CVARS)
    if k < 0.8:
        return r.choice(RVARS)
    return r.choice(['2.0', '0.5', '-3.0', '1.0i', '(2.0 - 1.0i)', '0.25f', '3', '-2'])


def cx(r, d):
    if d == 0 or r.random() < 0.2:
        return leaf(r)
    a = cx(r, d - 1)
    op = r.choice(['+', '-', '*', '*', '/', 'neg', 'conj', 'fcast', 'creal', 'cimag', '?:', 'sel', 'cmk'])
    if op in ('+', '-', '*'):
        return '(%s %s %s)' % (a, op, cx(r, d - 1))
    if op == '/':
        return '(%s / %s)' % (a, r.choice(NICE + ['x', '(double _Complex)f']))
    if op == 'neg':
        return '(-(%s))' % a
    if op == 'conj':
        return '(~(double _Complex)%s)' % a
    if op == 'fcast':
        return '((float _Complex)%s)' % a
    if op == 'creal':
        return '__real__ ((double _Complex)%s)' % a
    if op == 'cimag':
        return '__imag__ ((double _Complex)%s)' % a
    if op == '?:':
        return '(%s ? %s : %s)' % (a, cx(r, d - 1), cx(r, d - 1))
    if op == 'sel':
        return '((%s) %s (%s) ? %s : %s)' % (a, r.choice(['==', '!=']), cx(r, d - 1), cx(r, d - 1), cx(r, d - 1))
    return '__builtin_complex((double)__real__ (double _Complex)(%s), (double)__imag__ (double _Complex)(%s))' % (a, cx(r, d - 1))


PRE = r'''#include <stdio.h>
static void pc(const char *n, double _Complex v) { printf("%s %.10g %.10g\n", n, __real__ v, __imag__ v); }
static void pf(const char *n, float _Complex v) { printf("%s %.6g %.6g\n", n, (double)__real__ v, (double)__imag__ v); }
'''

VALS_C = ['0', '1.0', '(1.0 + 2.0i)', '(-0.5 + 3.0i)', '(4.0 - 1.0i)', '2.0i', '(-2.0 - 2.0i)', '(0.25 + 0.75i)']
VALS_F = ['0', '(1.5f + 0.5if)', '(-2.0f + 1.0if)', '3.0if', '(0.5f - 4.0if)']
VALS_R = ['0.0', '1.0', '-2.5', '0.5', '3.0']


def gen_file(r):
    parts = [PRE]
    nf = r.randint(3, 6)
    sigs = []
    for i in range(nf):
        ret = r.choice(['double _Complex', 'float _Complex', 'double'])
        body = ['__attribute__((noinline)) %s f%d(double _Complex a, double _Complex b, float _Complex f, double x, float y, int k)' % (ret, i), '{']
        # g before c, and c's initializer must not read c itself.
        body.append('    float _Complex g = f;')
        init = cx(r, 2)
        while re.search(r'\bc\b', init):
            init = cx(r, 2)
        body.append('    double _Complex c = %s;' % init)
        for _ in range(r.randint(1, 4)):
            t = r.random()
            if t < 0.4:
                body.append('    c %s %s;' % (r.choice(['+=', '-=', '*=']), cx(r, 2)))
            elif t < 0.55:
                body.append('    c /= %s;' % r.choice(NICE))
            elif t < 0.7:
                body.append('    g %s %s;' % (r.choice(['+=', '-=', '*=']), cx(r, 1)))
            elif t < 0.8:
                body.append('    x %s %s;' % (r.choice(['+=', '-=', '*=']), r.choice(['__real__ c', '__imag__ g', 'y', '(double)k'])))
            elif t < 0.9:
                body.append('    if (%s) c = %s;' % (cx(r, 1), cx(r, 2)))
            else:
                body.append('    __real__ c = %s; __imag__ g = %s;' % (r.choice(['x', '__imag__ c', '2.0']), r.choice(['y', '__real__ g', '-1.0f'])))
        res = cx(r, 3)
        if ret == 'double':
            body.append('    return __real__ (double _Complex)(%s) + __imag__ (double _Complex)(c + g) + x;' % res)
        else:
            body.append('    return (%s) + c + g + x;' % res)
        body.append('}')
        parts.append('\n'.join(body))
        sigs.append(ret)
    main = ['int main(void)', '{']
    for i, ret in enumerate(sigs):
        for _ in range(3):
            args = (r.choice(VALS_C), r.choice(VALS_C), r.choice(VALS_F), r.choice(VALS_R), r.choice(VALS_R) + 'f', str(r.randint(-3, 3)))
            call = 'f%d(%s)' % (i, ', '.join(args))
            if ret == 'double _Complex':
                main.append('    pc("f%d", %s);' % (i, call))
            elif ret == 'float _Complex':
                main.append('    pf("f%d", %s);' % (i, call))
            else:
                main.append('    printf("f%d %%.10g\\n", %s);' % (i, call))
    main += ['    return 0;', '}']
    parts.append('\n'.join(main))
    return '\n\n'.join(parts) + '\n'


def run(cmd, timeout=60):
    try:
        return subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return None


bad = skipped = 0
for t in range(COUNT):
    r = random.Random(SEED * 100003 + t)
    f = os.path.join(WORK, 'x%d_%d.c' % (SEED, t))
    with open(f, 'w') as fh:
        fh.write(gen_file(r))
    cl = run([CLANG, '-arch', 'arm64', '-O0', '-w', '-ffp-contract=off', f, '-o', f + '.clang'])
    if cl is None or cl.returncode != 0:
        print('clang-fail', f, (cl.stderr[:300] if cl else 'timeout'))
        continue
    cc = run([CCC, OPT, '-w', '-ffp-contract=off', f, '-o', f + '.ccc'])
    if cc is None or cc.returncode != 0:
        print('ccc-fail', f, (cc.stderr[:300] if cc else 'timeout'))
        bad += 1
        continue
    a = run([f + '.clang'])
    b = run([f + '.ccc'])
    if a is None or b is None or a.returncode != 0 or a.stdout != b.stdout:
        print('MISMATCH', f)
        bad += 1
    else:
        for e in ('.ccc', '.clang'):
            os.remove(f + e)
print('done', COUNT, 'bad', bad)
