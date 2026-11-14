"""Inlining and static locals fuzzer: random functions marked always_inline,
static inline, plain static or noinline, each with static locals (a counter,
an array, a struct) that persist across calls, calling earlier functions,
recursing with a depth guard, returning early from loops and switches,
using goto, passing addresses of locals and arrays, and also reached
through a function-pointer table (so a function is both inlined and called
out of line, and its statics must be one object). main calls them in loops
and finally asks every function for the state of its statics. All
arithmetic is unsigned or exact. Programs UBSan flags are skipped; lines
where gcc disagrees with clang are dropped.

usage: inlfz.py CCC CLANG WORK SEED COUNT   (env OPT, default -O2)
"""
import os, random, subprocess, sys

CCC, CLANG, WORK, SEED, COUNT = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]), int(sys.argv[5])
OPT = os.environ.get('OPT', '-O2')
GCC = os.environ.get('GCC', '/opt/homebrew/bin/gcc-15')
os.makedirs(WORK, exist_ok=True)

HDR = r'''#include <stdio.h>
#include <string.h>
typedef unsigned long UL;
struct P { UL x; unsigned y; unsigned char t[3]; };
static UL GSUM;
static volatile int VZ = 0;
'''

ATTRS = ['__attribute__((always_inline)) static inline', 'static inline', 'static', '__attribute__((noinline)) static',
         '__attribute__((always_inline)) static inline']

def R(xs):
    return random.choice(xs)

def expr(k, d=0):
    """An unsigned long expression over the parameters and statics of f<k>."""
    c = random.randrange(10)
    if d > 2 or c < 3:
        return R(['a', 'b', 'p.x', 'p.y', 'p.t[1]', 'arr[a & 7]', 'cnt', 'hist[cnt & 3]', 'sp.x', 'sp.y', 'r',
                  '%dUL' % random.randint(0, 999), 'GSUM', '(UL)VZ'])
    if c < 6:
        op = R(['+', '-', '*', '^', '|', '&', '<<', '>>'])
        if op in ('<<', '>>'):
            # Shift amounts in range for any operand width.
            return '((UL)%s %s (%s & 31))' % (expr(k, d + 1), op, expr(k, d + 1))
        return '(%s %s %s)' % (expr(k, d + 1), op, expr(k, d + 1))
    if c < 8 and k > 0:
        return call(k, random.randrange(k), d + 1)
    if c < 9:
        return '(%s ? %s : %s)' % (expr(k, d + 1), expr(k, d + 1), expr(k, d + 1))
    return '(UL)(double)(%s %% 4096) / 3' % expr(k, d + 1)

def call(k, j, d=0):
    """A call to f<j> from f<k> (j < k), with depth to spare."""
    return 'f%d(dep > 0 ? dep - 1 : 0, (unsigned)(%s), %s, (struct P){%s, %s, {1, 2, (unsigned char)a}}, arr)' % (
        j, R(['a', 'cnt', 'p.y', 'r']), R(['b', 'r', 'sp.x', 'cnt']), R(['p.x', 'r', 'b']), R(['p.y', 'a', 'cnt']))

def stmts(k, n, depth=0):
    out = []
    for _ in range(n):
        c = random.randrange(13)
        if c < 3:
            out.append('r = r * 7 + %s;' % expr(k))
        elif c == 3:
            out.append('arr[%s & 7] += (unsigned)%s;' % (R(['a', 'cnt', 'r', 'b']), expr(k)))
        elif c == 4:
            out.append('hist[%s & 3] ^= %s;' % (R(['cnt', 'a', 'r']), expr(k)))
        elif c == 5:
            out.append(R(['sp = p;', 'sp.x += r;', 'sp.y ^= (unsigned)r;', 'sp.t[a % 3] = (unsigned char)r;']))
        elif c == 6 and depth < 2:
            out.append('if ((%s & 3) == %d) {' % (expr(k), random.randrange(4)))
            out += ['    ' + s for s in stmts(k, random.randint(1, 3), depth + 1)]
            out.append('    ' + R(['return r + %d;' % random.randint(0, 9), 'goto out;', 'r ^= 1;']))
            out.append('}')
        elif c == 7 and depth < 2:
            out.append('for (unsigned i = 0; i < %d; i++) {' % random.randint(1, 6))
            out += ['    ' + s.replace('arr[a & 7]', 'arr[(a + i) & 7]') for s in stmts(k, random.randint(1, 2), depth + 1)]
            out.append('    if (((r + i) & 7) == %d) %s' % (random.randrange(8), R(['break;', 'return r ^ i;', 'continue;', 'goto out;'])))
            out.append('}')
        elif c == 8 and depth < 2:
            out.append('switch ((r + a) & 3) {')
            for case in range(3):
                out.append('case %d:' % case)
                out += ['    ' + s for s in stmts(k, 1, depth + 1)]
                out.append('    ' + R(['break;', 'return r * 3;', '/* fall through */']))
            out.append('default: r += 5;')
            out.append('}')
        elif c == 9 and k > 0:
            j = random.randrange(k)
            out.append('{ unsigned loc[8]; memcpy(loc, arr, sizeof loc); loc[r & 7] = %d; r += f%d(dep > 0 ? dep - 1 : 0, a + 1, b, p, loc); r += loc[3] + loc[(r >> 3) & 7]; }'
                       % (random.randint(0, 99), j))
        elif c == 10:
            out.append('if (dep > 0) r += f%d(dep - 1, a ^ 1, b + 1, p, arr);' % k)
        elif c == 11:
            out.append('GSUM += r & 0xffff;')
        else:
            out.append('r += (UL)&cnt != 0;')
    return out

def func(k):
    attr = R(ATTRS)
    body = ['%s UL f%d(int dep, unsigned a, UL b, struct P p, unsigned *arr) {' % (attr, k),
            '    static unsigned cnt; static UL hist[4] = {%d, %d, %d, %d}; static struct P sp;' % tuple(random.randint(0, 99) for _ in range(4)),
            '    UL r = %d;' % random.randint(0, 9),
            '    if (dep < 0) return cnt * 1000003UL + hist[0] + hist[1] * 3 + hist[2] * 5 + hist[3] * 7 + sp.x + sp.y + sp.t[0] + sp.t[2];',
            '    cnt++;']
    body += ['    ' + s for s in stmts(k, random.randint(3, 8))]
    body += ['out:', '    return r + cnt;', '}']
    return '\n'.join(body)

def program():
    n = random.randint(4, 9)
    out = [HDR] + [func(k) for k in range(n)]
    tbl = sorted(random.sample(range(n), random.randint(1, n)))
    out.append('static UL (*const TBL[])(int, unsigned, UL, struct P, unsigned *) = {%s};' % ', '.join('f%d' % j for j in tbl))
    out.append('int main(void) {')
    out.append('    unsigned arr[8] = {3, 1, 4, 1, 5, 9, 2, 6};')
    out.append('    UL acc = 0;')
    for _ in range(random.randint(4, 10)):
        j = random.randrange(n)
        c = random.randrange(3)
        if c == 0:
            out.append('    for (unsigned i = 0; i < %d; i++) acc = acc * 31 + f%d(%d, i, acc, (struct P){i, (unsigned)acc, {1, 2, 3}}, arr);'
                       % (random.randint(1, 5), j, random.randint(0, 3)))
        elif c == 1:
            out.append('    acc = acc * 31 + TBL[%d %% %d](%d, %d, acc, (struct P){acc, 7, {9, 8, 7}}, arr);'
                       % (random.randrange(len(tbl)), len(tbl), random.randint(0, 3), random.randint(0, 99)))
        else:
            out.append('    acc ^= f%d(%d, (unsigned)acc, %d, (struct P){0}, arr);' % (j, random.randint(0, 2), random.randint(0, 999)))
        out.append('    printf("%%lu %%u %%u\\n", acc, arr[0], arr[7]);')
    for k in range(n):
        out.append('    printf("f%d %%lu\\n", f%d(-1, 0, 0, (struct P){0}, arr));' % (k, k))
    out.append('    printf("G %lu\\n", GSUM);')
    out.append('    return 0;\n}')
    return '\n'.join(out) + '\n'

def run(cmd, timeout=60):
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
        return p.returncode, p.stdout, p.stderr
    except subprocess.TimeoutExpired:
        return -1, '', 'timeout'

def main():
    bad = skipped = 0
    for it in range(COUNT):
        random.seed(SEED * 100003 + it)
        src = os.path.join(WORK, 'n%d_%d.c' % (SEED, it))
        open(src, 'w').write(program())
        rc, _, err = run([CLANG, '-O0', '-w', '-fsanitize=undefined', '-fno-sanitize-recover=all', src, '-o', src + '.ub'])
        if rc != 0:
            skipped += 1
            print('clang-fail', src, err[:200])
            continue
        rc, _, err = run([src + '.ub'])
        os.remove(src + '.ub')
        if rc != 0:
            skipped += 1
            print('ub-skip', src, err[:200])
            continue
        run([CLANG, '-w', OPT, src, '-o', src + '.c.bin'])
        _, oc, _ = run([src + '.c.bin'])
        rcg, _, _ = run([GCC, '-w', OPT, src, '-o', src + '.g.bin'])
        og = run([src + '.g.bin'])[1] if rcg == 0 else None
        rcx, _, errx = run([CCC, '-w', OPT, src, '-o', src + '.x.bin'])
        if rcx != 0:
            bad += 1
            print('FAIL-BUILD', src, errx[:300])
            continue
        rx, ox, _ = run([src + '.x.bin'])
        cl, xl = oc.splitlines(), ox.splitlines()
        gl = og.splitlines() if og is not None else None
        diffs = [(a, xl[k] if k < len(xl) else '<missing rc %d>' % rx) for k, a in enumerate(cl)
                 if (xl[k] if k < len(xl) else None) != a and (gl is None or (k < len(gl) and gl[k] == a))]
        if diffs:
            bad += 1
            with open(src + '.diff', 'w') as fh:
                for a, x in diffs:
                    fh.write('clang %s\nccc   %s\n' % (a, x))
            print('DIFF', src, len(diffs))
        for e in ('.c.bin', '.g.bin', '.x.bin'):
            try: os.remove(src + e)
            except OSError: pass
        if not diffs:
            os.remove(src)
    print('done %d bad %d skipped %d' % (COUNT, bad, skipped))

main()
