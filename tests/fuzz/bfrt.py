#!/usr/bin/env python3
"""Runtime operations on bit-fields: random structs (bit-fields of several
widths and signedness, plain members, packed or not, a union view), random
statements (assign, compound assign, ++/--, conditionals, mixing fields),
every field printed after each function. ccc vs clang; files with UB under
clang -fsanitize=undefined are skipped.

usage: bfrt.py CCC CLANG WORKDIR SEED COUNT
env: OPT (default -O2)
"""
import os
import random
import subprocess
import sys

CCC, CLANG, WORK, SEED, COUNT = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]), int(sys.argv[5])
OPT = os.environ.get('OPT', '-O2')
os.makedirs(WORK, exist_ok=True)

BASES = [('unsigned', 32, False), ('int', 32, True), ('unsigned char', 8, False), ('signed char', 8, True),
         ('unsigned short', 16, False), ('short', 16, True), ('unsigned long long', 64, False),
         ('_Bool', 1, False)]


def gen_struct(r, name):
    fields = []
    for i in range(r.randint(2, 7)):
        if r.random() < 0.25:
            t = r.choice(['int', 'unsigned char', 'short', 'long long', 'unsigned'])
            fields.append(('m%d' % i, t, None, t not in ('unsigned', 'unsigned char')))
            continue
        base, bits, signed = r.choice(BASES)
        if base == '_Bool':
            w = 1
        else:
            w = r.randint(1, bits)
            # long long bit-fields of 32 bits or less promote differently
            # between compilers; keep them wide.
            if bits == 64:
                w = r.randint(33, 64)
        fields.append(('b%d' % i, base, w, signed))
    if r.random() < 0.2:
        fields.insert(r.randint(0, len(fields)), ('z%d' % len(fields), 'unsigned', 0, False))
    packed = r.random() < 0.25
    lines = ['struct %s {' % name]
    for n, t, w, s in fields:
        if w is None:
            lines.append('    %s %s;' % (t, n))
        elif w == 0:
            lines.append('    %s : 0;' % t)
        else:
            lines.append('    %s %s : %d;' % (t, n, w))
    lines.append('}%s;' % (' __attribute__((packed))' if packed else ''))
    real = [f for f in fields if f[2] != 0]
    return '\n'.join(lines), real


def lval(r, var, fields):
    n, t, w, s = r.choice(fields)
    return '%s.%s' % (var, n), (n, t, w, s)


def const(r):
    return r.choice(['0', '1', '2', '3', '7', '-1', '-3', '100', '255', '-128', '0x7f', '1000', '65535',
                     '0x12345', '-70000', '0x7fffffff', '5u', '0x80000000u', '0xffffffffu'])


def expr(r, var, fields, d):
    if d == 0 or r.random() < 0.3:
        if r.random() < 0.6:
            return lval(r, var, fields)[0]
        return '(%s)' % const(r)
    op = r.choice(['+', '-', '*', '&', '|', '^', '<<', '>>', '<', '==', '?:', '~', '-u', 'cast'])
    a = expr(r, var, fields, d - 1)
    b = expr(r, var, fields, d - 1)
    if op in ('<<', '>>'):
        return '(%s %s (%d))' % (a, op, r.randint(0, 7))
    if op == '?:':
        return '(%s ? %s : %s)' % (a, b, expr(r, var, fields, d - 1))
    if op == '~':
        return '(~%s)' % a
    if op == '-u':
        return '(-%s)' % a
    if op == 'cast':
        return '((%s)%s)' % (r.choice(['int', 'unsigned', 'long long', 'unsigned char', 'short', 'unsigned long long']), a)
    return '(%s %s %s)' % (a, op, b)


def gen_func(r, idx, sname, fields):
    body = []
    for _ in range(r.randint(3, 10)):
        lv, (n, t, w, s) = lval(r, 'x', fields)
        k = r.random()
        if k < 0.3:
            body.append('%s = %s;' % (lv, expr(r, 'x', fields, 2)))
        elif k < 0.55:
            op = r.choice(['+=', '-=', '*=', '&=', '|=', '^=', '<<=', '>>='])
            rhs = '%d' % r.randint(0, 5) if op in ('<<=', '>>=') else expr(r, 'x', fields, 1)
            if t == '_Bool' and op in ('*=', '<<=', '>>=', '+=', '-='):
                op = '|='
            body.append('%s %s %s;' % (lv, op, rhs))
        elif k < 0.7:
            body.append(r.choice(['%s++;', '%s--;', '++%s;', '--%s;']) % lv if t != '_Bool' else '%s = !%s;' % (lv, lv))
        elif k < 0.8:
            body.append('acc += %s;' % r.choice(['%s++', '%s--', '++%s', '--%s']) % lv if t != '_Bool' else 'acc += %s;' % lv)
        elif k < 0.9:
            lv2 = lval(r, 'x', fields)[0]
            body.append('if (%s %s %s) %s = %s;' % (expr(r, 'x', fields, 1), r.choice(['<', '>', '==', '!=']),
                                                     expr(r, 'x', fields, 1), lv2, expr(r, 'x', fields, 1)))
        else:
            body.append('acc = acc * 3 + (long long)%s;' % expr(r, 'x', fields, 2))
    src = ['__attribute__((noinline)) long long f%d(struct %s *p)' % (idx, sname), '{',
           '    struct %s x = *p;' % sname, '    long long acc = 0;']
    src += ['    ' + b for b in body]
    src += ['    *p = x;', '    return acc;', '}']
    return '\n'.join(src)


def gen_file(r):
    parts = ['#include <stdio.h>', '#include <string.h>']
    funcs = []
    for s in range(3):
        sdef, fields = gen_struct(r, 'S%d' % s)
        parts.append(sdef)
        for k in range(2):
            idx = s * 2 + k
            parts.append(gen_func(r, idx, 'S%d' % s, fields))
            funcs.append((idx, 'S%d' % s, fields))
    main = ['int main(void)', '{']
    for idx, sname, fields in funcs:
        main.append('    {')
        main.append('        struct %s v;' % sname)
        main.append('        memset(&v, 0, sizeof v);')
        for n, t, w, s in fields:
            main.append('        v.%s = %s;' % (n, const(r)))
        main.append('        long long a = f%d(&v);' % idx)
        fmt = ' '.join('%lld' for _ in fields)
        args = ', '.join('(long long)v.%s' % n for n, *_ in fields)
        main.append('        printf("f%d %%lld %s\\n", a, %s);' % (idx, fmt, args))
        main.append('        a = f%d(&v);' % idx)
        main.append('        printf("f%d %%lld %s\\n", a, %s);' % (idx, fmt, args))
        main.append('    }')
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
    f = os.path.join(WORK, 'b%d_%d.c' % (SEED, t))
    with open(f, 'w') as fh:
        fh.write(gen_file(r))
    u = run([CLANG, '-arch', 'arm64', '-O0', '-w', '-fsanitize=undefined', '-fno-sanitize-recover=all', f, '-o', f + '.ubsan'])
    if u is None or u.returncode != 0:
        print('clang-fail', f, (u.stderr[:200] if u else 'timeout'))
        continue
    ru = run([f + '.ubsan'])
    if ru is None or ru.returncode != 0:
        skipped += 1
        os.remove(f + '.ubsan')
        continue
    cl = run([CLANG, '-arch', 'arm64', '-O1', '-w', f, '-o', f + '.clang'])
    cc = run([CCC, OPT, '-w', f, '-o', f + '.ccc'])
    if cc is None or cc.returncode != 0:
        print('ccc-fail', f, (cc.stderr[:300] if cc else 'timeout'))
        bad += 1
        continue
    a = run([f + '.clang'])
    b = run([f + '.ccc'])
    if a is None or b is None or a.stdout != b.stdout or a.stdout != ru.stdout:
        print('MISMATCH', f)
        bad += 1
    else:
        for e in ('.ccc', '.clang', '.ubsan'):
            os.remove(f + e)
print('done', COUNT, 'bad', bad, 'ub-skipped', skipped)
