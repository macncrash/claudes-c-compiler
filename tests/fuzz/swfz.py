"""Switch and computed-goto fuzzer: switches on every integer type and an
enum, with dense case blocks (jump tables) with holes, sparse values
including the type's extremes, GNU case ranges (also near the limits),
default or none, fallthrough, nested switches, computed selectors, and
computed-goto dispatch loops over label addresses. main calls each switch
on every case value, its neighbours, range ends, the type's limits and
random values; some switches are inlinable and called with constants too.
clang is the oracle; lines where gcc disagrees are dropped.

usage: swfz.py CCC CLANG WORK SEED COUNT   (env OPT, default -O2)
"""
import os, random, subprocess, sys

CCC, CLANG, WORK, SEED, COUNT = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]), int(sys.argv[5])
OPT = os.environ.get('OPT', '-O2')
GCC = os.environ.get('GCC', '/opt/homebrew/bin/gcc-15')
os.makedirs(WORK, exist_ok=True)

TYPES = {
    'signed char': (-128, 127), 'unsigned char': (0, 255), 'short': (-32768, 32767), 'unsigned short': (0, 65535),
    'int': (-2**31, 2**31 - 1), 'unsigned': (0, 2**32 - 1), 'long': (-2**63, 2**63 - 1),
    'unsigned long': (0, 2**64 - 1), 'long long': (-2**63, 2**63 - 1), '_Bool': (0, 1), 'enum E': (-3, 2**31 - 1),
}

HDR = r'''#include <stdio.h>
typedef unsigned long UL;
enum E { EM3 = -3, E0 = 0, E1, E2, E7 = 7, E100 = 100, EBIG = 2147483647 };
static volatile int VZ = 0;
'''

def R(xs):
    return random.choice(xs)

def lit(t, v):
    if t == 'enum E':
        return {-3: 'EM3', 0: 'E0', 1: 'E1', 2: 'E2', 7: 'E7', 100: 'E100', 2**31 - 1: 'EBIG'}.get(v, '(enum E)%d' % v)
    lo, hi = TYPES[t]
    if lo < 0 and v == lo:
        return '(%s)(%d - 1)' % (t, v + 1) if t not in ('long', 'long long') else '(%s)(-%dLL - 1)' % (t, -(v + 1))
    suf = {'unsigned': 'U', 'unsigned long': 'UL', 'long': 'L', 'long long': 'LL'}.get(t, '')
    return '%d%s' % (v, suf)

def case_set(t):
    """Disjoint case labels: (kind, lo, hi) with kind 'v' or 'r' (range)."""
    lo, hi = TYPES[t]
    if t == 'enum E':
        return [('v', v, v) for v in random.sample([-3, 0, 1, 2, 7, 100, 2**31 - 1], random.randint(1, 6))]
    if t == '_Bool':
        return [('v', v, v) for v in random.sample([0, 1], random.randint(1, 2))]
    used = []

    def free(a, b):
        return a >= lo and b <= hi and a <= b and all(b < x or a > y for _, x, y in used)

    style = random.randrange(4)
    if style in (0, 1):
        # Dense block, maybe with holes: a jump table.
        n = random.randint(4, 120 if style == 0 else 30)
        base = R([0, 1, -5, lo, hi - n, random.randint(max(lo, -1000), min(hi - n, 1000))])
        base = max(lo, min(base, hi - n))
        for v in range(base, base + n):
            if random.random() < 0.8 and free(v, v):
                used.append(('v', v, v))
    for _ in range(random.randint(1, 10)):
        v = R([lo, hi, lo + 1, hi - 1, 0, -1, 1, random.randint(lo, hi), 255, 256, 65535, 65536, 2**31 - 1, -2**31, 2**32 - 1])
        if free(v, v):
            used.append(('v', v, v))
    for _ in range(random.randint(0, 3)):
        a = R([lo, hi - 10, random.randint(lo, hi), 10, -20])
        b = a + random.randint(0, 40)
        if free(a, b) and b <= hi:
            used.append(('r', a, b))
    if not used:
        used.append(('v', 0 if lo <= 0 <= hi else lo, 0 if lo <= 0 <= hi else lo))
    return used

def probes(t, cases):
    lo, hi = TYPES[t]
    vals = {lo, hi, 0 if lo <= 0 else lo, min(hi, 1)}
    for _, a, b in cases:
        for v in (a - 1, a, b, b + 1, (a + b) // 2):
            if lo <= v <= hi:
                vals.add(v)
    for _ in range(4):
        vals.add(random.randint(lo, hi))
    return sorted(vals)

def body(k, depth):
    c = random.randrange(5)
    if c == 0 and depth < 1:
        return 'r = r * 7 + inner%d((int)(k & 7));' % k
    return R(['r = r * 31 + %d;' % random.randint(1, 999), 'r ^= (UL)k * %d;' % random.randint(1, 99), 'r += r >> 3;',
              'r = r * 3 + (UL)VZ;'])

def gen_switch(k):
    t = R(list(TYPES))
    cases = case_set(t)
    random.shuffle(cases)
    inline = random.random() < 0.4
    attr = 'static inline' if inline else '__attribute__((noinline)) static'
    # k + 1 in unsigned arithmetic for the wide signed types: signed
    # overflow at the type's maximum would be undefined.
    plus1 = '(%s)((unsigned long long)k + 1)' % t if t in ('int', 'long', 'long long') else '(%s)(k + 1)' % t
    sel = R(['k', 'k', plus1, 'k * 1'] if t not in ('_Bool', 'enum E') else ['k'])
    lines = ['static int inner%d(int x) { switch (x) { case 0: return 3; case 2: case 3: return 9; case 5: return 11; default: return x; } }' % k,
             '%s UL s%d(%s k) {' % (attr, k, t), '    UL r = %d;' % random.randint(1, 99), '    switch (%s) {' % sel]
    for kind, a, b in cases:
        label = 'case %s:' % lit(t, a) if kind == 'v' else 'case %s ... %s:' % (lit(t, a), lit(t, b))
        lines.append('    ' + label)
        lines.append('        ' + body(k, 0))
        end = random.randrange(5)
        if end < 3:
            lines.append('        break;')
        elif end == 3:
            lines.append('        return r + %d;' % random.randint(0, 9))
        # else fall through
    if random.random() < 0.7:
        lines.append('    default:')
        lines.append('        r = r * 5 + 17;')
    lines += ['    }', '    return r;', '}']
    return t, cases, inline, '\n'.join(lines)

def gen_goto(k):
    n = random.randint(3, 9)
    lines = ['__attribute__((noinline)) static UL g%d(unsigned start, unsigned steps) {' % k,
             '    static void *const tbl[] = {%s};' % ', '.join('&&L%d' % i for i in range(n)),
             '    UL r = 1; unsigned i = start %% %d;' % n,
             '    goto *tbl[i];']
    for i in range(n):
        lines.append('L%d:' % i)
        lines.append('    r = r * %d + %d;' % (random.randint(2, 9), i))
        lines.append('    if (steps-- == 0) return r;')
        nxt = R(['(i + %d) %% %d' % (random.randint(1, n), n), '(unsigned)(r %% %d)' % n, '%d' % random.randrange(n)])
        lines.append('    i = %s; goto *tbl[i];' % nxt)
    lines.append('}')
    return '\n'.join(lines)

def program():
    out = [HDR]
    calls = []
    for k in range(random.randint(3, 7)):
        t, cases, inline, text = gen_switch(k)
        out.append(text)
        for v in probes(t, cases):
            arg = lit(t, v) if t != '_Bool' else str(v)
            calls.append('    printf("s%d %%s %%lu\\n", "%s", s%d(%s));' % (k, arg.replace('"', ''), k, arg))
            if not inline:
                continue
            # The same value through a volatile: no constant folding.
            calls.append('    { volatile %s v = %s; printf("s%dv %%s %%lu\\n", "%s", s%d(v)); }' % (t, arg, k, arg.replace('"', ''), k))
    for k in range(random.randint(1, 3)):
        out.append(gen_goto(k))
        for _ in range(4):
            calls.append('    printf("g%d %%lu\\n", g%d(%d, %d));' % (k, k, random.randint(0, 20), random.randint(0, 40)))
    out.append('int main(void) {')
    out += calls
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
        src = os.path.join(WORK, 'w%d_%d.c' % (SEED, it))
        open(src, 'w').write(program())
        rc, _, err = run([CLANG, '-w', OPT, src, '-o', src + '.c.bin'])
        if rc != 0:
            skipped += 1
            print('clang-fail', src, err[:200])
            continue
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
