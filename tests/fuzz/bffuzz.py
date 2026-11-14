"""Bit-field layout, static initializers and run-time access, ccc against clang.

usage: bffuzz.py CCC CLANG WORKDIR SEED COUNT [STRUCTS_PER_FILE]

Each program declares random structs of bit-fields (with zero-width and plain
members mixed in), initializes a static copy with constants that may not fit,
prints its bytes, then writes, updates and reads the fields of a local copy
from volatile values. Only well-defined operations are generated; clang's
output is the reference.
"""
import os
import random
import subprocess
import sys

CCC, CLANG, W = sys.argv[1], sys.argv[2], sys.argv[3]
seed, count = int(sys.argv[4]), int(sys.argv[5])
per = int(sys.argv[6]) if len(sys.argv) > 6 else 12
os.makedirs(W, exist_ok=True)

# (C type, bits, signed)
BTYPES = [('int', 32, True), ('unsigned', 32, False), ('signed char', 8, True),
          ('unsigned char', 8, False), ('short', 16, True), ('unsigned short', 16, False),
          ('long long', 64, True), ('unsigned long long', 64, False), ('_Bool', 1, False),
          ('long', 64, True), ('unsigned long', 64, False)]
PLAIN = ['char', 'short', 'int', 'long', 'double', 'unsigned char']


def const_text(r, bits):
    k = r.random()
    if k < 0.3:
        v = r.randint(-9, 9)
    elif k < 0.6:
        v = r.choice([0x7f, 0x80, 0xff, 0x100, 0x7fff, 0x8000, 0xffff, 0x7fffffff, -1, -128])
    else:
        v = r.getrandbits(min(bits + 2, 63))
        if r.random() < 0.3:
            v = -v
    lit = '%dll' % v if abs(v) > 0x7ffffff0 else '%d' % v
    if r.random() < 0.3:
        lit = '(%s + %d)' % (lit, r.randint(-3, 3))
    if r.random() < 0.2:
        lit = '(unsigned char)%s' % lit
    return lit


def struct(r, i):
    fields = []
    n = r.randint(2, 7)
    for j in range(n):
        k = r.random()
        if k < 0.1:
            t = r.choice(['int', 'unsigned', 'char', 'long long'])
            fields.append(('zero', t, 0, False, None))
            continue
        if k < 0.15:
            t, bits, signed = r.choice(BTYPES[:8])
            fields.append(('unnamed', t, r.randint(1, bits), False, None))
            continue
        if k < 0.25:
            fields.append(('plain', r.choice(PLAIN), 0, True, 'p%d' % j))
            continue
        if k < 0.3 and not any(f[0] == 'ptr' for f in fields):
            fields.append(('ptr', 'const void *', 0, False, 'q%d' % j))
            continue
        t, bits, signed = r.choice(BTYPES)
        w = 1 if bits == 1 else r.randint(1, bits)
        if r.random() < 0.15:
            w = bits
        fields.append(('bf', t, w, signed, 'f%d' % j))
    attrs = r.choice(['', '', '', ' __attribute__((packed))'])
    decl = ['struct%s S%d {' % (attrs, i)]
    for kind, t, w, signed, name in fields:
        if kind == 'zero':
            decl.append('    %s : 0;' % t)
        elif kind == 'unnamed':
            decl.append('    %s : %d;' % (t, w))
        elif kind == 'plain':
            decl.append('    %s %s;' % (t, name))
        elif kind == 'ptr':
            decl.append('    %s%s;' % (t, name))
        else:
            decl.append('    %s %s : %d;' % (t, name, w))
    decl.append('};')
    return decl, [f for f in fields if f[0] not in ('zero', 'unnamed')]


def program(r, n):
    top = ['#include <stdio.h>', '#include <string.h>', '',
           'static void dump(const char *tag, const void *p, unsigned long n)',
           '{', '    const unsigned char *b = p;', '    printf("%s %lu:", tag, n);',
           '    for (unsigned long i = 0; i < n; i++) printf(" %02x", b[i]);', '    printf("\\n");', '}',
           'static int anchor;', '']
    body = []
    for i in range(n):
        decl, fields = struct(r, i)
        top += decl
        inits = []
        for kind, t, w, signed, name in fields:
            if kind == 'ptr':
                inits.append('.%s = &anchor' % name)
            elif r.random() < 0.8:
                bits = 64 if kind == 'plain' else w
                inits.append('.%s = %s' % (name, const_text(r, bits)))
        top.append('static struct S%d g%d = { %s };' % (i, i, ', '.join(inits)))
        top.append('')
        # A pointer member's address differs between programs: copy and clear it.
        ptrs = [f[4] for f in fields if f[0] == 'ptr']
        b = ['    {', '        struct S%d t = g%d;%s dump("g%d", &t, sizeof t);'
             % (i, i, ''.join(' t.%s = 0;' % p + ' if (g%d.%s != &anchor) printf("bad ptr\\n");' % (i, p) for p in ptrs), i),
             '        struct S%d l; memset(&l, 0, sizeof l);' % i]
        bfs = [f for f in fields if f[0] == 'bf']
        for kind, t, w, signed, name in fields:
            if kind in ('plain', 'ptr'):
                continue
            vname = 'v%d_%s' % (i, name)
            b.append('        volatile long long %s = %s;' % (vname, const_text(r, w)))
            b.append('        l.%s = %s;' % (name, vname))
        for _ in range(r.randint(1, 6)):
            if not bfs:
                break
            kind, t, w, signed, name = r.choice(bfs)
            promo_wider = (t in ('long long', 'unsigned long long', 'long', 'unsigned long') and w < 64) or w < 32
            ops = ['^=', '|=', '&=']
            if not signed or promo_wider:
                ops += ['+=', '-=', '>>=']
            if not signed:
                ops += ['<<=', '*=']
            if signed and w <= 28 and t not in ('_Bool',):
                ops += ['*=']
            if t == '_Bool':
                ops = ['^=', '|=', '&=', '=']
            op = r.choice(ops)
            amt = r.randint(0, 3) if op in ('<<=', '>>=', '*=') else r.randint(-5, 300)
            b.append('        { volatile int k = %d; l.%s %s k; }' % (amt, name, op))
            if r.random() < 0.3 and t != '_Bool' and (not signed or promo_wider):
                b.append('        l.%s%s;' % (name, r.choice(['++', '--'])))
        for kind, t, w, signed, name in bfs:
            b.append('        printf("%d.%s %%lld %%d\\n", (long long)l.%s, (int)(l.%s - 1 < 0));'
                     % (i, name, name, name) if t not in ('unsigned long long', 'unsigned long', 'long long', 'long') or w < 32
                     else '        printf("%d.%s %%lld\\n", (long long)l.%s);' % (i, name, name))
            b.append('        printf("%d.g%s %%lld\\n", (long long)g%d.%s);' % (i, name, i, name))
        small = [f for f in bfs if f[2] <= 30 and f[1] not in ('long long', 'unsigned long long', 'long', 'unsigned long')]
        if len(small) >= 2:
            x, y = r.sample(small, 2)
            b.append('        printf("%d.sum %%d %%d\\n", l.%s + l.%s, g%d.%s > l.%s);' % (i, x[4], y[4], i, x[4], y[4]))
        b.append('        dump("l%d", &l, sizeof l);' % i)
        if not ptrs:
            # Positional initializers in an array and in an enclosing struct,
            # and the struct passed and returned by value.
            def positional():
                return '{ %s }' % ', '.join(const_text(r, 64 if f[0] == 'plain' else f[2]) for f in fields)
            top.append('static struct S%d a%d[2] = { %s, %s };' % (i, i, positional(), positional()))
            top.append('static struct W%d { char c; struct S%d s; short t : 5; } w%d = { 1, %s, %s };'
                       % (i, i, i, positional(), const_text(r, 5)))
            if bfs:
                f = r.choice(bfs)
                op = '= !' if f[1] == '_Bool' else '^='
                top.append('static __attribute__((noinline)) struct S%d pass%d(struct S%d x, int k) { x.%s %s k; return x; }'
                           % (i, i, i, f[4], op))
                b.append('        struct S%d p = pass%d(a%d[1], %d);' % (i, i, i, r.randint(0, 9)))
                for kind, t, w, signed, name in bfs:
                    b.append('        printf("%d.p%s %%lld\\n", (long long)p.%s);' % (i, name, name))
            b.append('        dump("a%d", a%d, sizeof a%d); dump("w%d", &w%d, sizeof w%d);' % (i, i, i, i, i, i))
        b.append('    }')
        body += b
    top.append('int main(void)\n{')
    top += body
    top.append('    return 0;\n}')
    return '\n'.join(top) + '\n'


def run(cc, src, exe):
    c = subprocess.run([cc, '-O2', '-w', '-o', exe, src], capture_output=True, text=True)
    if c.returncode != 0:
        return 'COMPILE FAILED: ' + c.stderr[:300]
    p = subprocess.run([exe], capture_output=True, text=True, timeout=20)
    return p.stdout + ('' if p.returncode == 0 else 'EXIT %d' % p.returncode)


def main():
    rnd = random.Random(seed)
    found = 0
    for k in range(count):
        src = os.path.join(W, 'b%d_%d.c' % (seed, k))
        open(src, 'w').write(program(rnd, per))
        ref = run(CLANG, src, os.path.join(W, 'x_k'))
        got = run(CCC, src, os.path.join(W, 'x_c'))
        if ref.startswith('COMPILE FAILED'):
            print('clang rejected', src, ref[:200])
            continue
        # An address at an unaligned offset of a packed static is a link
        # error on macOS (ld: pointer not aligned); clang -O2 links only
        # when it drops that global. Not a miscompile.
        if 'pointer not aligned' in got:
            print('unaligned pointer, skipped', src)
            continue
        if got != ref:
            found += 1
            bad = [(a, b) for a, b in zip(ref.split('\n'), got.split('\n')) if a != b]
            print('MISMATCH', src, bad[:3] if bad else got[:200])
        else:
            os.remove(src)
    print('seed %d: %d of %d files mismatched' % (seed, found, count))


main()
