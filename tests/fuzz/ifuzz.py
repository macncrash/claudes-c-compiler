"""Aggregate initializers, static and automatic, ccc against clang.

usage: ifuzz.py CCC CLANG WORKDIR SEED COUNT [OBJECTS_PER_FILE]

Random initializers for fixed nested types: positional and designated
members and elements, brace elision, designators that override earlier ones
(whole or in part), string literals for char arrays, unions. Each one
initializes a static object and a local one; every scalar leaf of both is
printed, so padding never matters.
"""
import os
import random
import subprocess
import sys

CCC, CLANG, W = sys.argv[1], sys.argv[2], sys.argv[3]
seed, count = int(sys.argv[4]), int(sys.argv[5])
per = int(sys.argv[6]) if len(sys.argv) > 6 else 12
os.makedirs(W, exist_ok=True)

S = lambda t: ('scalar', t)
P = ('struct', 'P', [('c', S('char')), ('a', ('array', S('int'), 3)), ('s', S('short'))])
U = ('union', 'U', [('i', S('int')), ('b', ('array', S('unsigned char'), 4))])
PT = ('struct', 'PT', [('x', S('short')), ('y', S('short'))])
Q = ('struct', 'Q', [('p', ('array', P, 2)), ('u', U), ('name', ('array', S('char'), 7)),
                     ('l', S('long')), ('pt', PT), ('d', S('double')), ('g', ('array', ('array', S('short'), 3), 2))])
TOPS = [('Q', Q), ('P', P), ('PT', PT), ('Qarr', ('array', Q, 2)), ('Parr', ('array', P, 3))]

PRELUDE = r'''#include <stdio.h>
struct P { char c; int a[3]; short s; };
union U { int i; unsigned char b[4]; };
struct PT { short x, y; };
struct Q { struct P p[2]; union U u; char name[7]; long l; struct PT pt; double d; short g[2][3]; };
typedef struct Q Qarr[2];
typedef struct P Parr[3];
'''


def scalar(r, t):
    if t == 'double':
        return r.choice(['0.5', '-2.25', '3', '1e3', '(1.0 / 4)'])
    v = r.choice([0, 1, -1, 7, 100, -128, 255, 300, 65537, 0x7fffffff])
    if t in ('char', 'unsigned char', 'short') and r.random() < 0.7:
        v = r.randint(-100, 100) if t != 'unsigned char' else r.randint(0, 200)
    return str(v)


def flat(r, t):
    """All scalar leaves of t, for brace elision."""
    k = t[0]
    if k == 'scalar':
        return [scalar(r, t[1])]
    if k == 'array':
        return [x for _ in range(t[2]) for x in flat(r, t[1])]
    if k == 'struct':
        return [x for _, ft in t[2] for x in flat(r, ft)]
    return flat(r, t[2][0][1])          # a union elides to its first member


def init(r, t, depth=0):
    k = t[0]
    if k == 'scalar':
        return scalar(r, t[1])
    if k == 'array':
        elem, n = t[1], t[2]
        if elem == S('char') and r.random() < 0.4:
            s = ''.join(r.choice('abcxyz') for _ in range(r.randint(0, n)))
            return '"%s"' % s if r.random() < 0.7 else '{ "%s" }' % s
        items = []
        i = 0
        while i < n and r.random() < 0.85:
            if r.random() < 0.3:
                i = r.randint(0, n - 1)
                items.append('[%d] = %s' % (i, init(r, elem, depth + 1)))
            elif elem[0] != 'scalar' and r.random() < 0.25:
                items.append(', '.join(flat(r, elem)))
            else:
                items.append(init(r, elem, depth + 1))
            i += 1
        return '{ %s }' % ', '.join(items)
    if k == 'union':
        f, ft = r.choice(t[2])
        if r.random() < 0.3:
            return '{ %s }' % init(r, t[2][0][1], depth + 1)
        return '{ .%s = %s }' % (f, init(r, ft, depth + 1))
    fields = t[2]
    items = []
    i = 0
    # After a designator reaching into a member, the next positional value
    # would continue inside that member; this generator designates instead.
    deep = False
    while (i < len(fields) or deep) and r.random() < 0.9:
        if deep or r.random() < 0.35:
            deep = False
            i = r.randrange(len(fields))
            name, ft = fields[i]
            # A designator may reach into the member: .p[1].a[2] = 5
            if ft[0] == 'array' and ft[1][0] != 'scalar' and r.random() < 0.4:
                j = r.randrange(ft[2])
                sub = ft[1]
                deep = True
                if sub[0] == 'struct':
                    sn, sft = r.choice(sub[2])
                    items.append('.%s[%d].%s = %s' % (name, j, sn, init(r, sft, depth + 1)))
                else:
                    items.append('.%s[%d] = %s' % (name, j, init(r, sub, depth + 1)))
            else:
                items.append('.%s = %s' % (name, init(r, ft, depth + 1)))
        else:
            name, ft = fields[i]
            if ft[0] != 'scalar' and r.random() < 0.2 and not (ft[0] == 'array' and ft[1] == S('char')):
                items.append(', '.join(flat(r, ft)))
            else:
                items.append(init(r, ft, depth + 1))
        i += 1
    return '{ %s }' % ', '.join(items)


def leaves(t, path):
    k = t[0]
    if k == 'scalar':
        fmt = '%g' if t[1] == 'double' else '%ld'
        cast = '(double)' if t[1] == 'double' else '(long)'
        return [(path, fmt, cast)]
    if k == 'array':
        return [x for i in range(t[2]) for x in leaves(t[1], '%s[%d]' % (path, i))]
    if k == 'union':
        return leaves(t[2][1][1], path + '.b')   # the bytes, whichever member was set
    return [x for n, ft in t[2] for x in leaves(ft, '%s.%s' % (path, n))]


def program(r, n):
    lines = [PRELUDE]
    body = ['int main(void)', '{']
    for i in range(n):
        tname, t = r.choice(TOPS)
        ctype = tname if tname in ('Qarr', 'Parr') else ('struct %s' % tname)
        text = init(r, t)
        lines.append('static %s g%d = %s;' % (ctype, i, text))
        body.append('    {')
        body.append('        %s l = %s;' % (ctype, text))
        for side in ('g%d' % i, 'l'):
            ls = leaves(t, side)
            for j in range(0, len(ls), 8):
                chunk = ls[j:j + 8]
                body.append('        printf("%d %s%s\\n", %s);' % (
                    i, side[0], ' '.join(f for _, f, _ in chunk), ', '.join(c + p for p, _, c in chunk)))
        body.append('    }')
    body.append('    return 0;')
    body.append('}')
    return '\n'.join(lines + body) + '\n'


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
        src = os.path.join(W, 'i%d_%d.c' % (seed, k))
        open(src, 'w').write(program(rnd, per))
        ref = run(CLANG, src, os.path.join(W, 'x_k'))
        got = run(CCC, src, os.path.join(W, 'x_c'))
        if ref.startswith('COMPILE FAILED'):
            print('clang rejected', src, ref[:300])
            continue
        if got != ref:
            found += 1
            bad = [(a, b) for a, b in zip(ref.split('\n'), got.split('\n')) if a != b]
            print('MISMATCH', src, bad[:4] if bad else got[:300])
        else:
            os.remove(src)
    print('seed %d: %d of %d files mismatched' % (seed, found, count))


main()
