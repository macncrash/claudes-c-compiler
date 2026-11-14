#!/usr/bin/env python3
"""Random float-sum loops (the shape reduce_vectorize handles, and near
misses it must leave alone), ccc vs clang, both -ffp-contract=off so the
in-order vector sums must match the scalar loop bit for bit.

usage: rdfuzz.py CCC CLANG WORKDIR SEED COUNT [FUNCS]
env: OPT (default -O2)
"""
import os
import random
import subprocess
import sys

CCC, CLANG, WORK, SEED, COUNT = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]), int(sys.argv[5])
FUNCS = int(sys.argv[6]) if len(sys.argv) > 6 else 6
OPT = os.environ.get('OPT', '-O2')
os.makedirs(WORK, exist_ok=True)


def gen_func(r, idx):
    """One loop function; returns (source, call)."""
    nsums = r.randint(1, 6)
    counter = r.choice(['int', 'int', 'int', 'long', 'unsigned'])
    start = r.choice(['0', '0', '0', '1', 'm', '3'])
    leaves = []
    loads = []
    # Unit-stride loads at small offsets.
    for k in range(r.randint(1, 4)):
        arr = r.choice(['a', 'b', 'c'])
        off = r.randint(0, 3)
        loads.append('float u%d = %s[w + %d];' % (k, arr, off))
        leaves.append('u%d' % k)
    # Pairs (cos/sin style), usually both halves.
    if r.random() < 0.7:
        loads.append('float pe = p[2 * w], po = p[2 * w + 1];')
        leaves += ['pe', 'po']
    elif r.random() < 0.3:
        loads.append('float pe = p[2 * w];')  # half a pair: must stay scalar
        leaves.append('pe')
    # Struct arrays.
    if r.random() < 0.6:
        for f in r.sample(['f0', 'f1', 'f2'], r.randint(1, 3)):
            loads.append('float s_%s = st->%s[w];' % (f, f))
            leaves.append('s_' + f)
    # Invariants.
    if r.random() < 0.5:
        leaves.append('k')
    if r.random() < 0.3:
        loads.append('float iv = b[5];')
        leaves.append('iv')
    if r.random() < 0.3:
        leaves.append('%.3ff' % r.uniform(-2, 2))

    def expr(d):
        if d == 0 or r.random() < 0.3:
            return r.choice(leaves)
        op = r.choice(['+', '-', '*', '*', '/', 'neg'])
        if op == 'neg':
            return '(-%s)' % expr(d - 1)
        if op == '/':
            # A denominator kept away from zero: leaves are in [0.25, 2].
            return '(%s / (%s + 3.0f))' % (expr(d - 1), r.choice([l for l in leaves if not l.endswith('f') and l != 'k'] or ['3.0f']))
        return '(%s %s %s)' % (expr(d - 1), op, expr(d - 1))

    body = list(loads)
    temps = []
    for t in range(r.randint(0, 3)):
        body.append('float t%d = %s;' % (t, expr(2)))
        temps.append('t%d' % t)
        leaves.append('t%d' % t)
    for s in range(nsums):
        nup = r.choice([1, 1, 1, 2])
        for _ in range(nup):
            form = r.choice(['+=', '-=', 'add', 'radd', 'fma', 'fms'])
            e = expr(2)
            if form == '+=':
                body.append('s%d += %s;' % (s, e))
            elif form == '-=':
                body.append('s%d -= %s;' % (s, e))
            elif form == 'add':
                body.append('s%d = s%d + %s;' % (s, s, e))
            elif form == 'radd':
                body.append('s%d = %s + s%d;' % (s, e, s))
            elif form == 'fma':
                body.append('s%d += %s * %s;' % (s, expr(1), expr(1)))
            else:
                body.append('s%d -= %s * %s;' % (s, expr(1), expr(1)))
    # Near misses.
    miss = r.random()
    if miss < 0.06:
        body.append('out[8 + (w & 3)] = s0;')  # a store
    elif miss < 0.10:
        body.append('s0 += s%d * 0.5f;' % (nsums - 1))  # a sum read by another update
    elif miss < 0.13:
        body.append('if (s0 > 1e30f) break;')
    inits = ', '.join('s%d = %s' % (s, r.choice(['0.0f', '1.0f', '-0.5f', 'k', 'a[0]'])) for s in range(nsums))
    src = ['__attribute__((noinline)) void f%d(const float *a, const float *b, const float *c, const float *p, '
           'const struct S *st, float k, int n, int m, float *out)' % idx, '{',
           '    float %s;' % inits,
           '    %s w;' % counter,
           '    for (w = %s; w < %s; w++) {' % (start, 'n' if counter != 'unsigned' else '(unsigned)n'),
           ]
    src += ['        ' + x for x in body]
    src += ['    }']
    src += ['    out[%d] = s%d;' % (s, s) for s in range(nsums)]
    src += ['}']
    return '\n'.join(src), nsums


def gen_file(r):
    parts = ['#include <stdio.h>', '#include <string.h>',
             'struct S { int pad[3]; float f0[80]; float f1[80]; float f2[80]; };']
    calls = []
    for i in range(FUNCS):
        s, nsums = gen_func(r, i)
        parts.append(s)
        calls.append((i, nsums))
    main = ['int main(void)', '{',
            '    static float a[96], b[96], c[96], p[200], out[16];',
            '    static struct S st;',
            '    for (int i = 0; i < 96; i++) {',
            '        a[i] = 0.25f + (float)((i * 37) % 29) / 16.0f;',
            '        b[i] = 0.25f + (float)((i * 11) % 23) / 13.0f;',
            '        c[i] = 0.25f + (float)((i * 5) % 17) / 9.0f;',
            '    }',
            '    for (int i = 0; i < 200; i++) p[i] = 0.25f + (float)((i * 13) % 31) / 17.0f;',
            '    for (int i = 0; i < 80; i++) { st.f0[i] = a[i] * 0.5f + 0.25f; st.f1[i] = b[i]; st.f2[i] = 2.0f - c[i] * 0.5f; }',
            '    int ns[] = { 0, 1, 2, 3, 4, 5, 7, 8, 9, 13, 16, 31, 64 };',
            '    for (unsigned q = 0; q < sizeof ns / sizeof ns[0]; q++) {',
            '        int n = ns[q];']
    for i, nsums in calls:
        main.append('        memset(out, 0, sizeof out);')
        main.append('        f%d(a, b, c, p, &st, 0.75f, n, n / 3, out);' % i)
        main.append('        printf("f%d n=%%d' % i + ' %a' * nsums + '\\n", n' + ''.join(', out[%d]' % s for s in range(nsums)) + ');')
    main += ['    }', '    return 0;', '}']
    parts.append('\n'.join(main))
    return '\n\n'.join(parts) + '\n'


def run(cmd, timeout=60):
    return subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)


bad = 0
for t in range(COUNT):
    r = random.Random(SEED * 100003 + t)
    src = gen_file(r)
    f = os.path.join(WORK, 'r%d_%d.c' % (SEED, t))
    with open(f, 'w') as fh:
        fh.write(src)
    flags = ['-ffp-contract=off', '-w']
    cl = run([CLANG, '-arch', 'arm64', '-O1'] + flags + [f, '-o', f + '.clang'])
    if cl.returncode != 0:
        print('clang-fail', f, cl.stderr[:300])
        continue
    cc = run([CCC, OPT] + flags + [f, '-o', f + '.ccc'])
    if cc.returncode != 0:
        print('ccc-fail', f, cc.stderr[:300])
        bad += 1
        continue
    a = run([f + '.clang']).stdout
    b = run([f + '.ccc']).stdout
    if a != b:
        print('MISMATCH', f)
        bad += 1
    else:
        os.remove(f + '.ccc')
        os.remove(f + '.clang')
print('done', COUNT, 'bad', bad)
