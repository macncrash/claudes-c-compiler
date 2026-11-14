#!/usr/bin/env python3
"""Single-threaded atomics fuzzer: _Atomic objects (globals, locals, struct
members, array elements), compound assigns and ++/-- on them, __atomic_*,
__sync_* and <stdatomic.h> calls on atomic and plain objects. Every result
is mixed into a hash that is printed. Programs UBSan flags are skipped.
usage: atfz.py CCC CLANG WORK SEED COUNT   (env OPT, default -O2)"""
import os
import random
import subprocess
import sys

CCC, CLANG, WORK, SEED, COUNT = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]), int(sys.argv[5])
OPT = os.environ.get('OPT', '-O2')
os.makedirs(WORK, exist_ok=True)

TYPES = ['unsigned char', 'signed char', 'unsigned short', 'short', 'unsigned', 'int',
         'unsigned long', 'long', 'unsigned long long', 'long long', '_Bool']
FTYPES = ['float', 'double']
ORDERS = ['__ATOMIC_RELAXED', '__ATOMIC_ACQUIRE', '__ATOMIC_RELEASE', '__ATOMIC_ACQ_REL', '__ATOMIC_SEQ_CST']
LOAD_ORD = ['__ATOMIC_RELAXED', '__ATOMIC_ACQUIRE', '__ATOMIC_SEQ_CST']
STORE_ORD = ['__ATOMIC_RELAXED', '__ATOMIC_RELEASE', '__ATOMIC_SEQ_CST']
SO = ['memory_order_relaxed', 'memory_order_seq_cst', 'memory_order_acq_rel']


def val(r, t):
    """A small expression of the operands x, y, k (runtime) or a constant."""
    c = r.random()
    if c < 0.3:
        return '(%s)x' % t
    if c < 0.5:
        return '(%s)(y >> %d)' % (t, r.randint(0, 40))
    if c < 0.6:
        return '(%s)k' % t
    return '(%s)%d' % (t, r.choice([0, 1, 2, 3, 7, -1, -2, 100, 127, 128, 255, 256, 65535, -129, 0x7fffffff]))


class Obj:
    def __init__(self, name, t, atomic, fp=False):
        self.name, self.t, self.atomic, self.fp = name, t, atomic, fp


def stmt(r, objs):
    o = r.choice(objs)
    n, t = o.name, o.t
    if o.fp:
        op = r.choice(['+=', '-=', '*=', '++', '--', 'load', '='])
        if op in ('++', '--'):
            return 'H(%s%s);' % ((n + op) if r.random() < 0.5 else (op + n), '')
        if op == 'load':
            return 'H(%s);' % n
        if op == '=':
            return 'H(%s = (%s)(int)x * 0.5);' % (n, t)
        return 'H(%s %s (%s)((int)k) * 0.25);' % (n, op, t)
    v = val(r, t)
    c = r.randint(12, 17) if o.atomic else r.randint(0, 12)
    if o.atomic and t == '_Bool' and c in (13, 14, 15, 16):  # clang stores 2 into an _Atomic _Bool on += and ++
        c = 17
    if o.atomic and c >= 13:
        if c == 13:
            return 'H(%s %s %s);' % (n, r.choice(['+=', '-=', '&=', '|=', '^=', '*=']), v)
        if c == 14:
            # clang shifts a negative signed _Atomic logically for >>= (gcc and ccc
            # arithmetically), and <<= of a negative value is UB: unsigned only.
            sop, amt = r.choice(['<<=', '>>=']), r.randint(0, 3)
            if not t.startswith('unsigned'):
                sop = '&='
            return 'H(%s %s %d);' % (n, sop, amt)
        if c == 15:
            return 'H(%s);' % r.choice([n + '++', n + '--', '++' + n, '--' + n])
        if c == 16:
            return 'H(%s %s);' % (r.choice(['atomic_fetch_add', 'atomic_fetch_sub', 'atomic_fetch_and',
                                            'atomic_fetch_or', 'atomic_fetch_xor']) + '(&' + n + ',', v + ')')
        return 'H(atomic_exchange_explicit(&%s, %s, %s)); H(atomic_load(&%s));' % (n, v, r.choice(SO), n)
    if t == '_Bool' and c <= 5:
        c = 6
    if c <= 1:
        f = r.choice(['fetch_add', 'fetch_sub', 'fetch_and', 'fetch_or', 'fetch_xor', 'fetch_nand',
                      'add_fetch', 'sub_fetch', 'and_fetch', 'or_fetch', 'xor_fetch', 'nand_fetch'])
        return 'H(__atomic_%s(&%s, %s, %s));' % (f, n, v, r.choice(ORDERS))
    if c <= 3:
        f = r.choice(['fetch_and_add', 'fetch_and_sub', 'fetch_and_or', 'fetch_and_and', 'fetch_and_xor',
                      'add_and_fetch', 'sub_and_fetch', 'or_and_fetch', 'and_and_fetch', 'xor_and_fetch'])
        return 'H(__sync_%s(&%s, %s));' % (f, n, v)
    if c == 4:
        return 'H(__sync_val_compare_and_swap(&%s, %s, %s));' % (n, r.choice([n, v]) if not o.atomic else v, val(r, t))
    if c == 5:
        return 'H(__sync_lock_test_and_set(&%s, %s));' % (n, v)
    if c == 6:
        return 'H(__atomic_exchange_n(&%s, %s, %s));' % (n, v, r.choice(ORDERS))
    if c == 7:
        return 'H(__atomic_load_n(&%s, %s));' % (n, r.choice(LOAD_ORD))
    if c == 8:
        return '__atomic_store_n(&%s, %s, %s); H(%s);' % (n, v, r.choice(STORE_ORD), n)
    if c == 9 or c == 10:
        e = 'e%d' % r.randint(0, 10**6)
        init = r.choice(['__atomic_load_n(&%s, __ATOMIC_RELAXED)' % n, val(r, t)])
        w = 0
        return '{ %s %s = %s; H(__atomic_compare_exchange_n(&%s, &%s, %s, %d, __ATOMIC_SEQ_CST, %s)); H(%s); H(%s); }' % (
            t, e, init, n, e, val(r, t), w, r.choice(['__ATOMIC_RELAXED', '__ATOMIC_SEQ_CST']), e, n)
    if c == 11:
        e = 'e%d' % r.randint(0, 10**6)
        return '{ %s %s = %s, d_%s; __atomic_exchange(&%s, &%s, &d_%s, %s); H(d_%s); __atomic_load(&%s, &%s, __ATOMIC_SEQ_CST); H(%s); }' % (
            t, e, v, e, n, e, e, r.choice(ORDERS), e, n, e, e)
    return 'H(%s);' % n


def gen_file(r):
    parts = ['#include <stdio.h>', '#include <stdatomic.h>',
             'static unsigned long long h;',
             '#define H(e) (h = h * 1000003u + (unsigned long long)(e))']
    gl = []
    for i in range(r.randint(1, 4)):
        t = r.choice(TYPES + FTYPES)
        at = r.random() < 0.7
        init = '%d' % r.choice([0, 1, 5, 200, -3]) if t != '_Bool' else '1'
        parts.append('static %s%s g%d = %s;' % ('_Atomic ' if at else '', t, i, init))
        gl.append(Obj('g%d' % i, t, at, t in FTYPES))
    nf = r.randint(2, 4)
    for fi in range(nf):
        body = ['__attribute__((noinline)) void f%d(unsigned long long x, unsigned long long y, int k)' % fi, '{']
        objs = list(gl)
        for i in range(r.randint(1, 3)):
            t = r.choice(TYPES + FTYPES)
            at = r.random() < 0.7
            body.append('    %s%s a%d = %s;' % ('_Atomic ' if at else '', t, i, val(r, t)))
            objs.append(Obj('a%d' % i, t, at, t in FTYPES))
        if r.random() < 0.5:
            t = r.choice(TYPES)
            at = r.random() < 0.6
            body.append('    struct { char c; %s%s m; short s; } s0 = { 1, %s, 2 };' % ('_Atomic ' if at else '', t, val(r, t)))
            objs.append(Obj('s0.m', t, at))
        if r.random() < 0.5:
            t = r.choice(TYPES)
            body.append('    %s arr[3] = { %s, %s, 0 };' % (t, val(r, t), val(r, t)))
            objs.append(Obj('arr[k & 1]', t, False))
            objs.append(Obj('arr[2]', t, False))
        if r.random() < 0.4:
            t = r.choice(TYPES)
            body.append('    static _Atomic %s *p = 0; static _Atomic %s pv; p = &pv;' % (t, t))
            objs.append(Obj('(*p)', t, True))
        for _ in range(r.randint(4, 12)):
            body.append('    ' + stmt(r, objs))
        for o in objs:
            body.append('    H(%s);' % o.name)
        body.append('}')
        parts.append('\n'.join(body))
    main = ['int main(void)', '{']
    for fi in range(nf):
        for _ in range(2):
            main.append('    f%d(%du, %du, %d); printf("f%d %%llu\\n", h);' % (
                fi, r.choice([0, 1, 255, 65537, 4294967295, 123456789]), r.randint(0, 2**31),
                r.randint(-3, 5), fi))
    for o in gl:
        if o.fp:
            main.append('    printf("%s %%g\\n", (double)%s);' % (o.name, o.name))
        else:
            main.append('    printf("%s %%llu\\n", (unsigned long long)%s);' % (o.name, o.name))
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
    f = os.path.join(WORK, 'a%d_%d.c' % (SEED, t))
    with open(f, 'w') as fh:
        fh.write(gen_file(r))
    ub = run([CLANG, '-arch', 'arm64', '-O0', '-w', '-fsanitize=undefined', '-fno-sanitize-recover=all', f, '-o', f + '.ub'])
    if ub is None or ub.returncode != 0:
        print('clang-fail', f, (ub.stderr[:300] if ub else 'timeout'))
        continue
    u = run([f + '.ub'])
    os.remove(f + '.ub')
    if u is None or u.returncode != 0:
        skipped += 1
        os.remove(f)
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
print('done', COUNT, 'bad', bad, 'skipped', skipped)
