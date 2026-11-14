#!/usr/bin/env python3
"""String and character literal fuzzer: escapes (octal, hex, simple),
concatenation (also across prefixes), L/u/U/u8 prefixes, char arrays sized
exactly (no NUL), larger (zero fill) or from the literal, strings in struct
and array initializers (static and automatic, brace-enclosed or not),
pointers to literals, sizeof of literals and arrays, character constants
(multi-char, '\\xff', L'x', u'x', U'x'). Every element is printed by value.
usage: strfz.py CCC CLANG WORK SEED COUNT   (env OPT, default -O2)"""
import os
import random
import subprocess
import sys

CCC, CLANG, WORK, SEED, COUNT = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]), int(sys.argv[5])
OPT = os.environ.get('OPT', '-O2')
os.makedirs(WORK, exist_ok=True)

PREFIX = {'c': ('', 'char', 1), 'L': ('L', 'wchar_t', 4), 'u': ('u', 'char16_t', 2),
          'U': ('U', 'char32_t', 4), 'u8': ('u8', 'char', 1)}
SIMPLE = ['\\n', '\\t', '\\\\', '\\"', "\\'", '\\a', '\\b', '\\f', '\\v', '\\r', '\\?']


def piece(r, kind):
    """One literal body; returns (text, number of code units it adds)."""
    out, n = [], 0
    for _ in range(r.randint(0, 6)):
        c = r.random()
        if c < 0.45:
            ch = r.choice('abcxyzABC019 _-+')
            out.append(ch)
        elif c < 0.6:
            out.append(r.choice(SIMPLE))
        elif c < 0.75:
            v = r.randint(0, 255 if kind in ('c', 'u8') else 0x7ff)
            out.append('\\%o' % min(v, 0o377))
        elif c < 0.9:
            v = r.randint(0, 255) if kind in ('c', 'u8') else r.randint(0, 0xffff if kind == 'u' else 0x10ffff)
            if kind in ('L', 'U') and 0xd800 <= v <= 0xdfff:
                v = 0x41
            if kind == 'u' and 0xd800 <= v <= 0xdfff:
                v = 0x42
            out.append('\\x%x' % v)
            # a following hex digit would extend the escape
            out.append('" "' if r.random() < 0.5 else 'q')
            if out[-1] == '" "':
                out[-1] = '""'
        else:
            out.append(r.choice(['\\0', '\\00', '\\1', '\\177']))
            out.append('""')
        n += 1
    return ''.join(out)


def literal(r, kind):
    """A possibly concatenated literal of the given kind."""
    pre = PREFIX[kind][0]
    parts = []
    for i in range(r.randint(1, 3)):
        # C11: one prefixed piece makes the whole literal that kind
        p = pre if (i == 0 or r.random() < 0.5) else ''
        parts.append('%s"%s"' % (p, piece(r, kind)))
    if not parts[0].startswith(pre):
        parts[0] = pre + parts[0]
    return ' '.join(parts)


def dump_array(name, kind, n):
    t = PREFIX[kind][1]
    cast = '(unsigned)(unsigned char)' if kind in ('c', 'u8') else '(unsigned)'
    return 'for (unsigned i = 0; i < %s; i++) H(%s%s[i]);' % (n, cast, name)


def gen(r):
    lines_g, lines_f = [], []
    nobj = 0
    for _ in range(r.randint(3, 10)):
        kind = r.choice(['c', 'c', 'c', 'u8', 'L', 'u', 'U'])
        t = PREFIX[kind][1]
        lit = literal(r, kind)
        name = 'o%d' % nobj
        nobj += 1
        st = r.random() < 0.5
        where = lines_g if st else lines_f
        c = r.randint(0, 6)
        if c in (3, 4) and kind not in ('c', 'u8') and not os.environ.get('WIDEAGG'):
            c = 0  # open in ccc: wide strings in struct members and arrays of strings
        if c == 0:
            where.append('%s%s %s[] = %s;' % ('' if st else '', t, name, lit))
            lines_f.append('H(sizeof %s);' % name)
            lines_f.append(dump_array(name, kind, 'sizeof %s / sizeof %s[0]' % (name, name)))
        elif c == 1:
            # exact or larger size: sizeof(lit)/unit gives the length with NUL
            extra = r.choice([-1, 0, 0, 1, 3])
            where.append('%s %s[sizeof(%s) / sizeof(%s) + %d] = %s;' % (t, name, lit, t, extra, lit))
            lines_f.append(dump_array(name, kind, 'sizeof %s / sizeof %s[0]' % (name, name)))
        elif c == 2:
            where.append('%s %s[] = { %s };' % (t, name, lit))
            lines_f.append(dump_array(name, kind, 'sizeof %s / sizeof %s[0]' % (name, name)))
        elif c == 3:
            # array of strings
            k = r.randint(1, 4)
            w = r.randint(1, 9)
            lits = [literal(r, kind) for _ in range(k)]
            # keep each within w units plus NUL slack via sizeof check
            where.append('%s %s[][%d] = { %s };' % (t, name, w + 8, ', '.join(lits)))
            lines_f.append('H(sizeof %s);' % name)
            lines_f.append('for (unsigned j = 0; j < sizeof %s / sizeof %s[0]; j++) { %s }' % (
                name, name, dump_array('%s[j]' % name, kind, 'sizeof %s[0] / sizeof %s[0][0]' % (name, name))))
        elif c == 4:
            # struct with a char array member, brace-elided or not
            k = r.randint(1, 12)
            where.append('struct { int a; %s s[%d]; short b; %s p[%d]; } %s = { %d, %s, %d, %s };' % (
                t, k + 8, t, k + 8, name, r.randint(-9, 9), lit, r.randint(-9, 9),
                '{ %s }' % literal(r, kind)))
            lines_f.append('H(%s.a); H(%s.b);' % (name, name))
            lines_f.append(dump_array('%s.s' % name, kind, '%d' % (k + 8)))
            lines_f.append(dump_array('%s.p' % name, kind, '%d' % (k + 8)))
        elif c == 5:
            # pointer to a literal; strings are only read
            where.append('const %s *%s = %s;' % (t, name, lit))
            lines_f.append('H(sizeof(%s));' % lit)
            lines_f.append(dump_array(name, kind, 'sizeof(%s) / sizeof(%s)' % (lit, t)))
        else:
            # character constants
            ccs = ["'a'", "'\\xff'", "'\\377'", "'\\0'", "'\\n'", "'ab'", "'abcd'", "'\\x7f'",
                   "L'x'", "L'\\xff'", "u'x'", "U'\\x10ffff'", "u'\\xffff'", "'\\''", "'\"'", "'\\x80'"]
            for _ in range(r.randint(1, 4)):
                cc = r.choice(ccs)
                lines_f.append('H((long long)(%s)); H(sizeof(%s));' % (cc, cc))
                lines_f.append('{ char c_ = %s; H((long long)c_); unsigned char uc_ = %s; H(uc_); }' % (cc, cc))
            nobj -= 1
            continue
    out = ['#include <stdio.h>', '#include <wchar.h>', 'typedef unsigned short char16_t; typedef unsigned int char32_t;',
           '#define H(...) (h = h * 1000003u + (unsigned long long)(__VA_ARGS__))',
           'unsigned long long h;']
    out += lines_g
    out.append('__attribute__((noinline)) void f(void)\n{')
    # automatic objects first, then the dumps
    auto = [l for l in lines_f if not l.startswith(('H(', 'for', '{'))]
    dumps = [l for l in lines_f if l.startswith(('H(', 'for', '{'))]
    out += ['    ' + l for l in auto + dumps]
    out.append('}')
    out.append('int main(void)\n{\n    f();\n    printf("%llu\\n", h);\n    return 0;\n}')
    return '\n'.join(out) + '\n'


def run(cmd, timeout=60):
    try:
        return subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return None


bad = skipped = 0
for t in range(COUNT):
    r = random.Random(SEED * 100003 + t)
    f = os.path.join(WORK, 'q%d_%d.c' % (SEED, t))
    with open(f, 'w') as fh:
        fh.write(gen(r))
    ub = run([CLANG, '-O0', '-w', '-Werror=excess-initializers', '-fsanitize=undefined,address', '-fno-sanitize-recover=all', f, '-o', f + '.ub'])
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
