#!/usr/bin/env python3
"""Bit-field layout fuzzer with #pragma pack: random structs (bit-fields of
every base type incl. _Bool, zero-width and unnamed bit-fields, plain
members, arrays, nested structs, member aligned/packed attributes, struct
packed/aligned) under #pragma pack(N), push/pop or none. Checks sizeof,
_Alignof, offsetof, the byte image of a static initializer and of a zeroed
object after runtime stores, read-back of every field, and compound ops.
Reference: clang (it defines the Apple arm64 layout), with UBSan+ASan.
usage: pkfz.py CCC CLANG WORK SEED COUNT   (env OPT, default -O2)"""
import os
import random
import subprocess
import sys

CCC, CLANG, WORK, SEED, COUNT = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]), int(sys.argv[5])
OPT = os.environ.get('OPT', '-O2')
os.makedirs(WORK, exist_ok=True)

BT = {'char': (8, True), 'signed char': (8, True), 'unsigned char': (8, False), 'short': (16, True),
      'unsigned short': (16, False), 'int': (32, True), 'unsigned': (32, False), 'long': (64, True),
      'unsigned long': (64, False), 'long long': (64, True), 'unsigned long long': (64, False),
      '_Bool': (1, False)}


class St:
    def __init__(self, idx):
        self.idx = idx
        self.name = 'struct P%d' % idx
        self.fields = []  # (name or None, kind, info)


def gen_struct(r, idx, prev):
    s = St(idx)
    nf = r.randint(1, 8)
    for fi in range(nf):
        c = r.random()
        name = 'f%d' % fi
        if c < 0.55:
            t = r.choice(list(BT))
            bits, signed = BT[t]
            w = r.randint(0, bits) if t != '_Bool' else r.randint(0, 1)
            if w == 0 or r.random() < 0.12:
                s.fields.append((None, 'bf', (t, w)))      # unnamed: padding or zero-width
            else:
                attr = ' __attribute__((packed))' if r.random() < 0.08 else ''
                s.fields.append((name, 'bf', (t, w, attr)))
        elif c < 0.75:
            t = r.choice(list(BT))
            attr = r.choice(['', '', '', ' __attribute__((aligned(%d)))' % r.choice([1, 2, 4, 8, 16]),
                             ' __attribute__((packed))'])
            s.fields.append((name, 'scal', (t, attr)))
        elif c < 0.85:
            s.fields.append((name, 'arr', (r.choice(['char', 'short', 'int']), r.randint(1, 3))))
        elif prev:
            s.fields.append((name, 'st', r.choice(prev)))
        else:
            s.fields.append((name, 'scal', ('int', '')))
    pk = r.random()
    s.pragma = None
    if pk < 0.6:
        s.pragma = r.choice([1, 2, 4, 8, 16])
    s.push = r.random() < 0.5
    a = r.random()
    s.attr = ' __attribute__((packed))' if a < 0.15 else (' __attribute__((aligned(%d)))' % r.choice([2, 4, 8, 16]) if a < 0.25 else '')
    return s


def struct_def(s):
    out = []
    if s.pragma:
        out.append('#pragma pack(push, %d)' % s.pragma if s.push else '#pragma pack(%d)' % s.pragma)
    lines = ['%s {' % s.name]
    for name, kind, info in s.fields:
        if kind == 'bf':
            if name is None:
                lines.append('    %s : %d;' % (info[0], info[1]))
            else:
                lines.append('    %s %s : %d%s;' % (info[0], name, info[1], info[2]))
        elif kind == 'scal':
            lines.append('    %s %s%s;' % (info[0], name, info[1]))
        elif kind == 'arr':
            lines.append('    %s %s[%d];' % (info[0], name, info[1]))
        else:
            lines.append('    %s %s;' % (info.name, name))
    lines.append('}%s;' % s.attr)
    out.append('\n'.join(lines))
    if s.pragma:
        out.append('#pragma pack(pop)' if s.push else '#pragma pack()')
    return '\n'.join(out)


def leaves(s, lv):
    """(expr, type, width or None) of every named leaf, in declaration order."""
    out = []
    for name, kind, info in s.fields:
        if name is None:
            continue
        e = '%s.%s' % (lv, name)
        if kind == 'bf':
            out.append((e, info[0], info[1]))
        elif kind == 'scal':
            out.append((e, info[0], None))
        elif kind == 'arr':
            for j in range(info[1]):
                out.append(('%s[%d]' % (e, j), info[0], None))
        else:
            out.extend(leaves(info, e))
    return out


def value(r, t, w):
    bits, signed = BT[t]
    if w is None:
        w = bits
    if t == '_Bool':
        return r.randint(0, 1)
    if signed:
        return r.randint(-(1 << (w - 1)), (1 << (w - 1)) - 1)
    return r.randint(0, (1 << w) - 1)


def lit(v, t):
    if v < 0:
        return '(%s)(%dll)' % (t, v)
    return '(%s)%dull' % (t, v)


def init_list(r, s):
    items = []
    for name, kind, info in s.fields:
        if name is None:
            continue
        if kind == 'bf':
            items.append(lit(value(r, info[0], info[1]), info[0]))
        elif kind == 'scal':
            items.append(lit(value(r, info[0], None), info[0]))
        elif kind == 'arr':
            items.append('{ %s }' % ', '.join(lit(value(r, info[0], None), info[0]) for _ in range(info[1])))
        else:
            items.append(init_list(r, info))
    return '{ %s }' % ', '.join(items)


def gen(r):
    sts = []
    for i in range(r.randint(1, 4)):
        sts.append(gen_struct(r, i, list(sts)))
    top = ['#include <stdio.h>', '#include <string.h>', '#include <stddef.h>',
           '#define H(...) (h = h * 1000003u + (unsigned long long)(__VA_ARGS__))',
           'unsigned long long h;',
           'static void bytes(const void *p, unsigned n) { const unsigned char *b = p; for (unsigned i = 0; i < n; i++) H(b[i]); }']
    for s in sts:
        top.append(struct_def(s))
    body = []
    for s in sts:
        k = s.idx
        top.append('%s gs%d = %s;' % (s.name, k, init_list(r, s)))
        body.append('H(sizeof(%s)); H(_Alignof(%s));' % (s.name, s.name))
        for name, kind, info in s.fields:
            if name is not None and kind != 'bf':
                body.append('H(offsetof(%s, %s));' % (s.name, name))
        body.append('bytes(&gs%d, sizeof gs%d);' % (k, k))
        lv = leaves(s, 'gs%d' % k)
        for e, t, w in lv:
            body.append('H((long long)%s);' % e)
        body.append('{ %s x; memset(&x, 0, sizeof x);' % s.name)
        for e, t, w in leaves(s, 'x'):
            body.append('  %s = %s;' % (e, lit(value(r, t, w), t)))
        body.append('  bytes(&x, sizeof x);')
        for e, t, w in leaves(s, 'x'):
            body.append('  H((long long)%s);' % e)
        # compound ops on bit-fields (unsigned wrap; signed kept in range)
        for e, t, w in leaves(s, 'x'):
            if w is not None and t != '_Bool' and r.random() < 0.4:
                bits, signed = BT[t]
                if signed:
                    body.append('  %s = %s;' % (e, lit(value(r, t, w), t)))
                    body.append('  %s ^= %s;' % (e, lit(r.randint(0, (1 << (w - 1)) - 1) if w > 1 else 0, t)))
                else:
                    body.append('  %s += %d; %s--;' % (e, r.randint(0, 9), e))
                body.append('  H((long long)%s); bytes(&x, sizeof x);' % e)
        body.append('  gs%d = x; bytes(&gs%d, sizeof gs%d); }' % (k, k, k))
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
    f = os.path.join(WORK, 'p%d_%d.c' % (SEED, t))
    with open(f, 'w') as fh:
        fh.write(gen(r))
    ub = run([CLANG, '-O0', '-w', '-fsanitize=undefined,address', '-fno-sanitize-recover=all', f, '-o', f + '.ub'])
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
