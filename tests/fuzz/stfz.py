#!/usr/bin/env python3
"""Struct return/copy fuzzer: random struct types (scalars of every width,
float/double, _Bool, arrays, nested structs and arrays of them, unsigned
bit-fields, packed or aligned), makers returning structs in several styles,
callees taking structs by value and changing their copy, and statements that
copy structs: chains, self-copies through pointers, L = mod(L), ?: and
comma on structs, members of returned structs, swaps, memcpy, statement
expressions, loops over arrays of structs. Every leaf is hashed (never
padding). Reference: clang -O0 with UBSan+ASan.
usage: stfz.py CCC CLANG WORK SEED COUNT   (env OPT, default -O2)"""
import os
import random
import subprocess
import sys

CCC, CLANG, WORK, SEED, COUNT = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]), int(sys.argv[5])
OPT = os.environ.get('OPT', '-O2')
os.makedirs(WORK, exist_ok=True)

SCAL = ['char', 'signed char', 'unsigned char', 'short', 'unsigned short', 'int', 'unsigned',
        'long long', 'unsigned long long', 'float', 'double', '_Bool']
FLT = ('float', 'double')


class St:
    def __init__(self, idx, fields, attr, is_union=False):
        self.idx, self.fields, self.attr, self.is_union = idx, fields, attr, is_union
        self.name = ('union S%d' if is_union else 'struct S%d') % idx


def act(s):
    """The members that are written and read: a union's first member only."""
    return s.fields[:1] if s.is_union else s.fields


def gen_struct(r, idx, prev):
    fields = []
    nf = r.randint(1, 6)
    for fi in range(nf):
        c = r.random()
        name = 'f%d' % fi
        if c < 0.5 or (c >= 0.75 and not prev):
            fields.append((name, 'scal', r.choice(SCAL)))
        elif c < 0.62:
            fields.append((name, 'arr', (r.choice(SCAL[:11]), r.randint(1, 4))))
        elif c < 0.75:
            fields.append((name, 'bf', r.randint(1, 31)))
        elif c < 0.9:
            fields.append((name, 'st', r.choice(prev)))
        else:
            fields.append((name, 'sta', (r.choice(prev), r.randint(1, 3))))
    a = r.random()
    attr = ''
    if a < 0.15:
        attr = ' __attribute__((packed))'
    elif a < 0.25:
        attr = ' __attribute__((aligned(16)))'
    return St(idx, fields, attr, r.random() < 0.2)


def struct_def(s):
    lines = ['%s {' % s.name]
    for name, kind, ex in s.fields:
        if kind == 'scal':
            lines.append('    %s %s;' % (ex, name))
        elif kind == 'arr':
            lines.append('    %s %s[%d];' % (ex[0], name, ex[1]))
        elif kind == 'bf':
            lines.append('    unsigned %s : %d;' % (name, ex))
        elif kind == 'st':
            lines.append('    %s %s;' % (ex.name, name))
        else:
            lines.append('    %s %s[%d];' % (ex[0].name, name, ex[1]))
    lines.append('}%s;' % s.attr)
    return '\n'.join(lines)


def leaves(s, lv):
    """(expr, kind) for every leaf: kind 'f' float, 'i' integer, ('b', w) bit-field."""
    out = []
    for name, kind, ex in act(s):
        e = '%s.%s' % (lv, name)
        if kind == 'scal':
            out.append((e, 'f' if ex in FLT else ('B' if ex == '_Bool' else ('t', ex))))
        elif kind == 'arr':
            for j in range(ex[1]):
                out.append(('%s[%d]' % (e, j), 'f' if ex[0] in FLT else ('t', ex[0])))
        elif kind == 'bf':
            out.append((e, ('b', ex)))
        elif kind == 'st':
            out.extend(leaves(ex, e))
        else:
            for j in range(ex[1]):
                out.extend(leaves(ex[0], '%s[%d]' % (e, j)))
    return out


def val(r, kind, seed):
    c1, c2 = r.randint(-9, 9), r.randint(-50, 50)
    if kind == 'f':
        return '(%s * %d + %d) * 0.25' % (seed, c1, c2)
    if kind == 'B':
        return '((%s + %d) & 1)' % (seed, c2)
    if kind[0] == 'b':
        return '((unsigned)(%s * %d + %d) & %du)' % (seed, c1, c2, (1 << kind[1]) - 1)
    return '(%s)(%s * %d + %d)' % (kind[1], seed, c1, c2)


def hash_leaf(e, kind):
    if kind == 'f':
        return 'H((long long)((%s) * 4));' % e
    return 'H(%s);' % e


def init_list(r, s, seed):
    """A full positional brace initializer for s."""
    items = []
    for name, kind, ex in act(s):
        if kind == 'scal':
            items.append(val(r, 'f' if ex in FLT else ('B' if ex == '_Bool' else ('t', ex)), seed))
        elif kind == 'arr':
            items.append('{ %s }' % ', '.join(val(r, 'f' if ex[0] in FLT else ('t', ex[0]), seed) for _ in range(ex[1])))
        elif kind == 'bf':
            items.append(val(r, ('b', ex), seed))
        elif kind == 'st':
            items.append(init_list(r, ex, seed))
        else:
            items.append('{ %s }' % ', '.join(init_list(r, ex[0], seed) for _ in range(ex[1])))
    return '{ %s }' % ', '.join(items)


def set_stmts(r, s, lv, seed, frac=1.0):
    return ['%s = %s;' % (e, val(r, k, seed)) for e, k in leaves(s, lv) if r.random() < frac]


def gen_helpers(r, s):
    k = s.idx
    out = []
    lv = leaves(s, '(*p)')
    out.append('__attribute__((noinline)) unsigned long long h%d(const %s *p)\n{\n    unsigned long long h = %d;\n    %s\n    return h;\n}' % (
        k, s.name, k, '\n    '.join(hash_leaf(e, kd) for e, kd in lv)))
    q = r.choice(['__attribute__((noinline))', 'static inline', ''])
    style = r.randint(0, 3)
    body = []
    if style == 0:
        body.append('return (%s)%s;' % (s.name, init_list(r, s, 'x')))
    elif style == 1:
        body.append('%s v;' % s.name)
        body += set_stmts(r, s, 'v', 'x')
        body.append('return v;')
    elif style == 2:
        body.append('%s v = %s;' % (s.name, init_list(r, s, 'x')))
        body += set_stmts(r, s, 'v', '(x + 1)', 0.3)
        body.append('return v;')
    else:
        body.append('%s v = t%d;' % (s.name, k))
        body += set_stmts(r, s, 'v', 'x', 0.5)
        body.append('if (x & 1) return t%d;' % k)
        body.append('return v;')
    out.append('%s %s mk%d(int x)\n{\n    %s\n}' % (q, s.name, k, '\n    '.join(body)))
    q = r.choice(['__attribute__((noinline))', 'static inline', ''])
    body = set_stmts(r, s, 'v', 'x', 0.4) + ['return v;']
    out.append('%s %s mod%d(%s v, int x)\n{\n    %s\n}' % (q, s.name, k, s.name, '\n    '.join(body)))
    # by value: change the copy, the caller's object must not change
    body = set_stmts(r, s, 'v', '7', 0.6) + ['return h%d(&v);' % k]
    out.append('__attribute__((noinline)) unsigned long long hv%d(%s v)\n{\n    %s\n}' % (k, s.name, '\n    '.join(body)))
    out.append('%s (*fp%d)(int) = mk%d;' % (s.name, k, k))
    return out


def gen_func(r, fi, sts):
    ind = '    '
    # a callee taking several structs by value between scalars
    params = []
    for _ in range(r.randint(2, 5)):
        params.append(('s', r.choice(sts)) if r.random() < 0.6 else ('d', None))
    pdecl, pbody = [], []
    for i, (kind, ps) in enumerate(params):
        if kind == 's':
            pdecl.append('%s p%d' % (ps.name, i))
            pbody.append('H(h%d(&p%d));' % (ps.idx, i))
        else:
            pdecl.append('double p%d' % i)
            pbody.append('H((long long)(p%d * 4));' % i)
    pk = '__attribute__((noinline)) unsigned long long pk%d(%s)\n{\n    unsigned long long h = 0;\n    %s\n    return h;\n}' % (fi, ', '.join(pdecl), '\n    '.join(pbody))
    body = ['unsigned long long h = 0; int t = 0; (void)t;']
    # locals: name -> struct
    lv = {}  # lvalue expr -> St
    for li in range(r.randint(2, 5)):
        s = r.choice(sts)
        n = 'l%d' % li
        c = r.random()
        if c < 0.3:
            body.append('%s %s = mk%d(x + %d);' % (s.name, n, s.idx, li))
        elif c < 0.6:
            body.append('%s %s = %s;' % (s.name, n, init_list(r, s, 'x')))
        else:
            body.append('%s %s;' % (s.name, n))
            body += set_stmts(r, s, n, '(x - %d)' % li)
        lv[n] = s
    for s in sts:
        lv['g%d' % s.idx] = s
        for j in range(3):
            lv['ga%d[%d]' % (s.idx, j)] = s
    # nested members as lvalues
    for e, s in list(lv.items()):
        if 'packed' in s.attr:
            continue
        for name, kind, ex in act(s):
            if kind == 'st':
                lv['%s.%s' % (e, name)] = ex
            elif kind == 'sta':
                lv['%s.%s[%d]' % (e, name, ex[1] - 1)] = ex[0]
    by_type = {}
    for e, s in lv.items():
        by_type.setdefault(s.idx, []).append(e)
    stbyidx = {s.idx: s for s in sts}

    def rval(s, depth=0, pure=False):
        ls = by_type[s.idx]
        c = r.randint(0, 10 if depth < 2 else 1)
        if pure:
            c = r.choice([0, 1, 3, 6, 9])
        if c == 9:
            return r.choice(['fp%d(x + %d)', '(*fp%d)(x - %d)']) % (s.idx, r.randint(0, 3))
        if c == 0:
            return r.choice(ls)
        if c == 1:
            return 'mk%d(x + %d)' % (s.idx, r.randint(-3, 3))
        if c == 2:
            return 'mod%d(%s, %d)' % (s.idx, rval(s, depth + 1), r.randint(-5, 5))
        if c == 3:
            return '(%s)%s' % (s.name, init_list(r, s, 'x'))
        if c == 4:
            return '(x > %d ? %s : %s)' % (r.randint(-3, 5), rval(s, depth + 1), rval(s, depth + 1))
        if c == 5:
            return '(t++, %s)' % rval(s, depth + 1)
        if c == 6:
            return '*&%s' % r.choice(ls)
        if c == 7:
            stmts = set_stmts(r, s, 'q', 'x', 0.3)
            return '({ %s q = %s; %s q; })' % (s.name, rval(s, depth + 1), ' '.join(stmts))
        if c == 8:
            return '(%s = %s)' % (r.choice(ls), rval(s, depth + 1))
        return 'ga%d[(x & 0x7fffffff) %% 3]' % s.idx

    for _ in range(r.randint(4, 12)):
        s = stbyidx[r.choice(list(by_type))]
        ls = by_type[s.idx]
        L = r.choice(ls)
        c = r.randint(0, 14)
        if c <= 2:
            body.append('%s = %s;' % (L, rval(s)))
        elif c == 3:
            body.append('%s = mod%d(%s, %d);' % (L, s.idx, L, r.randint(-5, 5)))
        elif c == 4:
            body.append('%s = %s = %s;' % (L, r.choice(ls), rval(s)))
        elif c == 5:
            body.append('{ %s *p = &%s; *p = *p; *p = %s; }' % (s.name, L, rval(s)))
        elif c == 6:
            lf = leaves(s, '')
            if lf:
                e, kd = r.choice(lf)
                body.append(hash_leaf('(%s)%s' % (rval(s), e), kd))
        elif c == 7:
            body.append('{ %s tmp = %s; %s = %s; %s = tmp; }' % (s.name, L, L, (o := r.choice(ls)), o))
        elif c == 8:
            o = r.choice(ls)
            if o != L:
                body.append('__builtin_memcpy(&%s, &%s, sizeof %s);' % (L, o, L))
        elif c == 9:
            body += set_stmts(r, s, L, 'x', 0.3)
        elif c == 10:
            body.append('for (int i = 0; i < 3; i++) ga%d[i] = mod%d(ga%d[(i + 1) %% 3], i);' % (s.idx, s.idx, s.idx))
        elif c == 11:
            body.append('H(hv%d(%s));' % (s.idx, L))
        elif c == 12:
            args = []
            for (kind, ps) in params:
                args.append(rval(ps, pure=True) if kind == 's' else r.choice(['x', '(double)x * 0.5', '%d' % r.randint(-9, 9)]))
            body.append('H(pk%d(%s));' % (fi, ', '.join(args)))
        elif c == 13:
            body.append('{ volatile %s vv = %s; vv = %s; %s = vv; }' % (s.name, rval(s, pure=True), rval(s, pure=True), L))
        else:
            body.append('{ %s *p = &%s; %s }' % (s.name, L, ' '.join(x.replace('(*p).', 'p->') for x in set_stmts(r, s, '(*p)', 'x', 0.4))))
        body.append('H(h%d(&%s));' % (s.idx, L))
    for e, s in lv.items():
        body.append('H(h%d(&%s));' % (s.idx, e))
    body.append('return h + t;')
    return [pk, '__attribute__((noinline)) unsigned long long f%d(int x)\n{\n%s\n}' % (
        fi, '\n'.join(ind + b for b in body))]


def gen_file(r):
    sts = []
    for i in range(r.randint(1, 4)):
        sts.append(gen_struct(r, i, list(sts)))
    parts = ['#include <stdio.h>',
             '#define H(...) (h = h * 1000003u + (unsigned long long)(__VA_ARGS__))']
    for s in sts:
        parts.append(struct_def(s))
    for s in sts:
        parts.append('%s g%d, ga%d[3], t%d = %s;' % (s.name, s.idx, s.idx, s.idx, init_list(r, s, '3')))
    for s in sts:
        parts.extend(gen_helpers(r, s))
    nf = r.randint(1, 3)
    for fi in range(nf):
        parts.extend(gen_func(r, fi, sts))
    main = ['int main(void)', '{']
    for s in sts:
        main.append('    g%d = mk%d(1); for (int i = 0; i < 3; i++) ga%d[i] = mk%d(i + 5);' % (s.idx, s.idx, s.idx, s.idx))
    for fi in range(nf):
        for _ in range(2):
            main.append('    printf("f%d %%llu\\n", f%d(%d));' % (fi, fi, r.randint(-20, 20)))
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
    f = os.path.join(WORK, 's%d_%d.c' % (SEED, t))
    with open(f, 'w') as fh:
        fh.write(gen_file(r))
    ub = run([CLANG, '-O0', '-w', '-fsanitize=undefined,address', '-fno-sanitize-recover=all', f, '-o', f + '.ub'])
    if ub is None or ub.returncode != 0:
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
