"""Variadic macro fuzzer: random variadic macros (__VA_ARGS__, #__VA_ARGS__,
, ## __VA_ARGS__, __VA_OPT__, GNU named args..., argument counting, nested
and indirect invocations), called with random arguments and spacing; each
line prints the expansion stringified (and, where it is an int expression,
its value). clang is the oracle; lines where gcc disagrees are dropped.

usage: vmfz.py CCC CLANG WORK SEED COUNT
"""
import os, random, subprocess, sys

CCC, CLANG, WORK, SEED, COUNT = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]), int(sys.argv[5])
GCC = os.environ.get('GCC', '/opt/homebrew/bin/gcc-15')
os.makedirs(WORK, exist_ok=True)

HDR = r'''#include <stdio.h>
#define S_(...) #__VA_ARGS__
#define S(...) S_(__VA_ARGS__)
#define EMPTY()
#define ID(...) __VA_ARGS__
#define CNT_(_1, _2, _3, _4, _5, _6, _7, _8, N, ...) N
#define CNT(...) CNT_(__VA_ARGS__, 8, 7, 6, 5, 4, 3, 2, 1, 0)
static int f(int a, ...) { return a; }
'''

def R(xs):
    return random.choice(xs)

def sp():
    return R(['', '', ' ', '  ', '\t'])

def atom():
    return R(['1', '2', 'x', 'y', '(a, b)', '"s,t"', "','", '3 + 4', 'f(1)', '()', '', 'EMPTY()', 'a  b', '-1', '(1)'])

def args_text(n):
    return ','.join(sp() + atom() + sp() for _ in range(n))

def gen_macros():
    defs, names = [], []
    for k in range(random.randint(4, 8)):
        name = 'M%d' % k
        nnamed = random.randint(0, 2)
        named = ['p%d' % i for i in range(nnamed)]
        style = random.randrange(3)  # 0: ..., 1: GNU args..., 2: ... with __VA_OPT__
        va = 'rest' if style == 1 else '__VA_ARGS__'
        plist = ', '.join(named + (['rest...'] if style == 1 else ['...']))
        pieces = []
        for _ in range(random.randint(1, 4)):
            c = random.randrange(9)
            if c == 0: pieces.append(va)
            elif c == 1: pieces.append('#' + va)
            elif c == 2: pieces.append('[%s]' % ', '.join(named + ['##' + va]) if named else '[q, ## %s]' % va)
            elif c == 3 and style == 2: pieces.append('__VA_OPT__(%s)' % R([',', 'x %s y' % va, '#%s' % (named[0] if named else 'z'), '(' + va + ')', '']))
            elif c == 4 and named: pieces.append('#' + R(named))
            elif c == 6: pieces.append('S(%s)' % va)
            elif c == 7: pieces.append('CNT(%s)' % va)
            else: pieces.append(R(named) if named else '0')
        body = ' '.join(pieces)
        defs.append('#define %s(%s) %s' % (name, plist, body))
        names.append((name, nnamed))
    return defs, names

def gen_lines(names, n):
    lines = []
    for _ in range(n):
        name, nnamed = R(names)
        nargs = nnamed + random.randint(0, 4)
        inner = args_text(nargs) if nargs else sp()
        call = '%s(%s)' % (name, inner)
        k = random.randrange(4)
        if k == 0:
            call = 'ID(%s)' % call
        elif k == 1 and names:
            n2, nn2 = R(names)
            call = '%s(%s%s)' % (n2, ', '.join(['1'] * nn2) + (', ' if nn2 else ''), call)
        lines.append('  printf("%%d [%%s]\\n", __LINE__, S(%s));' % call)
    return lines

def run(cmd, timeout=60):
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
        return p.returncode, p.stdout, p.stderr
    except subprocess.TimeoutExpired:
        return -1, '', 'timeout'

WS = [0]

def main():
    bad = skipped = 0
    for it in range(COUNT):
        random.seed(SEED * 100003 + it)
        defs, names = gen_macros()
        lines = gen_lines(names, 60)
        src = os.path.join(WORK, 'm%d_%d.c' % (SEED, it))
        open(src, 'w').write(HDR + '\n'.join(defs) + '\nint main(void) {\n' + '\n'.join(lines) + '\n  return 0;\n}\n')
        rc, _, err = run([CLANG, '-std=gnu17', '-w', src, '-o', src + '.c.bin'])
        if rc != 0:
            skipped += 1
            continue
        rcg, _, _ = run([GCC, '-std=gnu17', '-w', src, '-o', src + '.g.bin'])
        _, oc, _ = run([src + '.c.bin'])
        og = run([src + '.g.bin'])[1] if rcg == 0 else None
        rcx, _, errx = run([CCC, '-w', src, '-o', src + '.x.bin'])
        if rcx != 0:
            bad += 1
            print('FAIL-BUILD', src, errx[:300])
            continue
        _, ox, _ = run([src + '.x.bin'])
        cl, xl = oc.splitlines(), ox.splitlines()
        gl = og.splitlines() if og is not None else None
        nows = lambda s: ''.join(s.split())
        diffs = []
        for k, a in enumerate(cl):
            x = xl[k] if k < len(xl) else '<missing>'
            if a != x and (gl is None or (k < len(gl) and gl[k] == a)):
                if nows(a) == nows(x):
                    WS[0] += 1
                else:
                    diffs.append((a, x))
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
    print('done %d bad %d skipped %d whitespace-only lines %d' % (COUNT, bad, skipped, WS[0]))

main()
