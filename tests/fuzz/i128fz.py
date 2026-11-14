#!/usr/bin/env python3
"""Runtime __int128 arithmetic: random expression trees over unsigned and
signed __int128 (wrapping arithmetic done unsigned; signed only for /, %,
>>, compares and conversions), mixed with 64/32-bit and double
conversions, in noinline functions called on edge-case values. ccc vs
clang; files UBSan flags are skipped.

usage: i128fz.py CCC CLANG WORKDIR SEED COUNT
env: OPT (default -O2)
"""
import os
import random
import subprocess
import sys

CCC, CLANG, WORK, SEED, COUNT = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]), int(sys.argv[5])
OPT = os.environ.get('OPT', '-O2')
os.makedirs(WORK, exist_ok=True)

U = 'unsigned __int128'
I = '__int128'
VARS = ['a', 'b', 'c', '(U)x', '(U)(I)x', '(U)y', '(U)(I)y', '(U)k', '(U)(I)k']


def leaf(r):
    k = r.random()
    if k < 0.65:
        return r.choice(VARS)
    if k < 0.85:
        return '(U)%s' % r.choice(['0', '1', '2', '3', '63', '64', '127', '0xffffffffffffffffULL', '0x8000000000000000ULL',
                                     '0x123456789abcdef0ULL', '-1', '-7'])
    return r.choice(['((U)1 << 64)', '((U)1 << 127)', '(((U)0x0123456789abcdefULL << 64) | 0xfedcba9876543210ULL)',
                     '(~(U)0 >> 1)'])


def e(r, d):
    if d == 0 or r.random() < 0.2:
        return leaf(r)
    a = e(r, d - 1)
    b = e(r, d - 1)
    op = r.choice(['+', '-', '*', '&', '|', '^', '<<', '>>', 'sar', '/', '%', 'sdiv', 'smod',
                   '<', 'slt', '==', '?:', '~', 'neg', 'u64', 's64', 'u32', 's32', 'dbl', 'sdbl', 'clz', 'popc',
                   'mulh', 'rot'])
    if op in ('+', '-', '*', '&', '|', '^'):
        return '(%s %s %s)' % (a, op, b)
    if op == '<<':
        return '(%s << (%s & 127))' % (a, b) if r.random() < 0.5 else '(%s << %d)' % (a, r.randint(0, 127))
    if op == '>>':
        return '(%s >> (%s & 127))' % (a, b) if r.random() < 0.5 else '(%s >> %d)' % (a, r.randint(0, 127))
    if op == 'sar':
        return '(U)((I)%s >> (%s & 127))' % (a, b) if r.random() < 0.5 else '(U)((I)%s >> %d)' % (a, r.randint(0, 127))
    if op == '/':
        return 'udv(%s, %s)' % (a, b)
    if op == '%':
        return 'umd(%s, %s)' % (a, b)
    if op == 'sdiv':
        return 'sdv(%s, %s)' % (a, b)
    if op == 'smod':
        return 'smd(%s, %s)' % (a, b)
    if op == '<':
        return '(U)(%s < %s)' % (a, b)
    if op == 'slt':
        return '(U)((I)%s < (I)%s)' % (a, b)
    if op == '==':
        return '(U)(%s == %s)' % (a, b)
    if op == '?:':
        return '(%s ? %s : %s)' % (a, b, e(r, d - 1))
    if op == '~':
        return '(~%s)' % a
    if op == 'neg':
        return '(-%s)' % a
    if op == 'u64':
        return '(U)(unsigned long long)%s' % a
    if op == 's64':
        return '(U)(I)(long long)%s' % a
    if op == 'u32':
        return '(U)(unsigned)%s' % a
    if op == 's32':
        return '(U)(I)(int)%s' % a
    if op == 'dbl':
        return 'fromd((double)%s)' % a
    if op == 'sdbl':
        return 'fromsd((double)(I)%s)' % a
    if op == 'clz':
        return '(U)lz(%s)' % a
    if op == 'popc':
        return '(U)pc(%s)' % a
    if op == 'mulh':
        return '(U)(((%s) * (U)(unsigned long long)(%s)) >> 64)' % (a, b)
    return '((%s << 13) | (%s >> 115))' % (a, a)


PRE = r'''#include <stdio.h>
typedef unsigned __int128 U;
typedef __int128 I;
static U udv(U a, U b) { return b ? a / b : a; }
static U umd(U a, U b) { return b ? a % b : a; }
static U sdv(U a, U b) { I x = (I)a, y = (I)b; if (!y || (y == -1 && x == (I)((U)1 << 127))) return a; return (U)(x / y); }
static U smd(U a, U b) { I x = (I)a, y = (I)b; if (!y || (y == -1 && x == (I)((U)1 << 127))) return a; return (U)(x % y); }
static U fromd(double d) { if (!(d >= 0) || d >= 3.4e38) return 7; return (U)d; }
static U fromsd(double d) { if (!(d > -1.7e38 && d < 1.7e38)) return 9; return (U)(I)d; }
static int lz(U v) { unsigned long long h = v >> 64, l = (unsigned long long)v; return h ? __builtin_clzll(h) : l ? 64 + __builtin_clzll(l) : 128; }
static int pc(U v) { return __builtin_popcountll((unsigned long long)(v >> 64)) + __builtin_popcountll((unsigned long long)v); }
static void pr(const char *n, U v) { printf("%s %016llx%016llx\n", n, (unsigned long long)(v >> 64), (unsigned long long)v); }
'''

VALS = ['0', '1', '(U)-1', '((U)1 << 127)', '(~(U)0 >> 1)', '((U)1 << 64)', '(((U)1 << 64) - 1)',
        '(((U)0x0123456789abcdefULL << 64) | 0xfedcba9876543210ULL)', '(U)(I)-5', '(U)0x8000000000000000ULL',
        '(((U)0x00000000ffffffffULL << 64) | 0xffffffff00000000ULL)', '(U)12345678901234567ULL', '(U)(I)-12345678901234567LL']


def gen_file(r):
    parts = [PRE]
    nf = r.randint(3, 6)
    for i in range(nf):
        body = ['__attribute__((noinline)) U f%d(U a, U b, U c, unsigned long long x, long long y, int k)' % i, '{']
        nv = r.randint(1, 4)
        for j in range(nv):
            body.append('    U t%d = %s;' % (j, e(r, r.randint(1, 4))))
            if r.random() < 0.4:
                body.append('    %s = t%d %s %s;' % (r.choice('abc'), j, r.choice(['+', '^', '*', '-']), e(r, 2)))
        body.append('    return %s;' % ' ^ '.join('t%d' % j for j in range(nv)))
        body.append('}')
        parts.append('\n'.join(body))
    main = ['int main(void)', '{']
    for i in range(nf):
        for _ in range(4):
            args = (r.choice(VALS), r.choice(VALS), r.choice(VALS),
                    r.choice(['0', '1', '0xffffffffffffffffULL', '0x8000000000000000ULL', '12345ULL']),
                    r.choice(['0', '-1', '-9223372036854775807LL - 1', '9223372036854775807LL', '-77']),
                    r.choice(['0', '1', '-1', '64', '127', '-2147483647 - 1']))
            main.append('    pr("f%d", f%d(%s));' % (i, i, ', '.join(args)))
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
    f = os.path.join(WORK, 'q%d_%d.c' % (SEED, t))
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
        for x in ('.ccc', '.clang', '.ubsan'):
            os.remove(f + x)
print('done', COUNT, 'bad', bad, 'ub-skipped', skipped)
