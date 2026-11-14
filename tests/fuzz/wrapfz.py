"""Wrapping loop counter fuzzer: loops whose counter wraps around, which
trip-count analysis, strength reduction, rotation and unrolling must not
assume away. Counters of unsigned types of every width, and signed char
and short (whose ++ converts back with wraparound), start near a limit or
anywhere, step up or down by constant strides, and stop at != ends, at
<, <=, >, >= bounds of another type (usual arithmetic conversions), or
with i-- forms passing 0. Each loop is simulated in Python with C's
semantics first; one that would not end within MAXTRIP trips gets an
iteration guard, the rest run unguarded so their trip counts stay
computable. Bodies hash the counter, index arrays with it, branch on it,
break and continue; the counter is printed after the loop. Bounds are
constants or runtime values. clang is the oracle; lines where gcc
disagrees are dropped.

usage: wrapfz.py CCC CLANG WORK SEED COUNT   (env OPT, default -O2)
"""
import os, random, resource, subprocess, sys, tempfile

CCC, CLANG, WORK, SEED, COUNT = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]), int(sys.argv[5])
OPT = os.environ.get('OPT', '-O2')
GCC = os.environ.get('GCC', '/opt/homebrew/bin/gcc-15')
os.makedirs(WORK, exist_ok=True)
MAXTRIP = 3000

# (C type, bits, signed, rank) -- counters exclude wide signed types (overflow is undefined).
CTR = [('unsigned char', 8, False, 1), ('signed char', 8, True, 1), ('unsigned short', 16, False, 2),
       ('short', 16, True, 2), ('unsigned', 32, False, 3), ('unsigned long', 64, False, 4),
       ('unsigned long long', 64, False, 5)]
BND = CTR + [('int', 32, True, 3), ('long', 64, True, 4), ('long long', 64, True, 5)]
INT = ('int', 32, True, 3)

HDR = r'''#include <stdio.h>
typedef unsigned long long UX;
static UX H;
static unsigned A[64];
static void mix(UX v) { H = (H ^ v) * 1099511628211ULL; }
'''

def wrap(t, v):
    m = 1 << t[1]
    v &= m - 1
    if t[2] and v >= m >> 1:
        v -= m
    return v

def promoted(t):
    return INT if t[1] < 32 else t

def common(a, b):
    a, b = promoted(a), promoted(b)
    if a[3] == b[3]:
        return a if not a[2] else b
    hi, lo = (a, b) if a[3] > b[3] else (b, a)
    if not hi[2] or hi[1] > lo[1]:
        return hi
    return next(t for t in BND if t[1] == hi[1] and not t[2] and t[3] == hi[3])

def rng(t):
    return (-(1 << (t[1] - 1)), (1 << (t[1] - 1)) - 1) if t[2] else (0, (1 << t[1]) - 1)

def near(t):
    lo, hi = rng(t)
    return random.choice([lo, lo + 1, lo + 3, hi, hi - 1, hi - 5, 0, 1, 2, -1 if t[2] else 3, 7,
                          random.randint(lo, hi), hi - random.randint(0, 40), lo + random.randint(0, 40)])

def lit(t, v):
    if v < 0:
        return '((%s)(-%dLL - 1))' % (t[0], -v - 1)
    return '((%s)%dULL)' % (t[0], v)

def cmp(op, a, ta, b, tb):
    ct = common(ta, tb)
    x, y = wrap(ct, a), wrap(ct, b)
    return {'<': x < y, '<=': x <= y, '>': x > y, '>=': x >= y, '!=': x != y}[op]

def simulate(t, start, step, form, op, end, te):
    """Trips of the loop, or None past MAXTRIP."""
    i = start
    for trips in range(MAXTRIP + 1):
        if form == 'for':
            if not cmp(op, i, t, end, te):
                return trips
            i = wrap(t, i + step)
        else:  # 'postdec': while (i-- OP end)
            old = i
            i = wrap(t, i - 1)
            if not cmp(op, old, t, end, te):
                return trips
    return None

class Gen:
    def __init__(self):
        self.n = 0

    def loop(self, ind):
        self.n += 1
        k = self.n
        t = random.choice(CTR)
        form = random.choice(['for', 'for', 'for', 'postdec'])
        start = near(t)
        if form == 'for':
            if random.random() < 0.5:
                op, te = '!=', t
                step = random.choice([1, 1, -1, 2, -2, 3, 5, -7, 16, -1 << (t[1] - 2)])
                # An end the stride reaches: start + trips * step, wrapped.
                end = wrap(t, start + step * random.randint(0, 40 if random.random() < 0.7 else 2000))
            else:
                op = random.choice(['<', '<=', '>', '>='])
                te = random.choice(BND)
                end = near(te)
                step = random.choice([1, 1, 2, 3, -1, -2, 5]) if op in ('<', '<=') else random.choice([-1, -1, -2, -3, 1, 2])
        else:
            op = random.choice(['!=', '>', '>='])
            te = random.choice([t] + BND)
            end = near(te)
            step = -1
        trips = simulate(t, start, step, form, op, end, te)
        guard = trips is None
        out = []
        sv = lit(t, start)
        ev = lit(te, end)
        if random.random() < 0.5:
            # Runtime bounds: no folding of the trip count.
            out.append('%s{ volatile %s s%d = %s; volatile %s e%d = %s; %s i%d = s%d; %s E%d = e%d; int g%d = 0;'
                       % (ind, t[0], k, sv, te[0], k, ev, t[0], k, k, te[0], k, k, k))
            sv, ev = 'i%d' % k, 'E%d' % k
            init = ''
        else:
            out.append('%s{ %s i%d; int g%d = 0;' % (ind, t[0], k, k))
            init = 'i%d = %s' % (k, sv)
        g = ' && g%d++ < %d' % (k, MAXTRIP) if guard else ''
        if form == 'for':
            stepx = 'i%d += %d' % (k, step) if step >= 0 else 'i%d -= %d' % (k, -step)
            if step in (1, -1) and random.random() < 0.5:
                stepx = ('++i%d' if step == 1 else 'i%d--') % k
            out.append('%s    for (%s; i%d %s %s%s; %s) {' % (ind, init, k, op, ev, g, stepx))
        else:
            if init:
                out.append('%s    %s;' % (ind, init))
            out.append('%s    while (i%d-- %s %s%s) {' % (ind, k, op, ev, g))
        out += self.body(ind + '        ', k, t)
        out.append('%s    }' % ind)
        out.append('%s    mix((UX)i%d); mix((UX)g%d); printf("L%d %%llu %%lld\\n", H, (long long)i%d); }' % (ind, k, k, k, k))
        return out

    def body(self, ind, k, t):
        out = []
        for _ in range(random.randint(1, 4)):
            c = random.randrange(8)
            if c < 3:
                out.append('%smix((UX)i%d * %d);' % (ind, k, random.randint(1, 99)))
            elif c == 3:
                out.append('%sA[i%d & 63] += (unsigned)i%d;' % (ind, k, k))
            elif c == 4:
                out.append('%sif ((i%d & %d) == %d) continue;' % (ind, k, random.choice([3, 7, 15]), random.randrange(4)))
            elif c == 5:
                out.append('%sif (i%d == %s) break;' % (ind, k, lit(t, near(t))))
            elif c == 6:
                out.append('%sA[(unsigned char)i%d %% 64] ^= (unsigned)(i%d >> 1);' % (ind, k, k))
            else:
                out.append('%smix(A[(i%d + %d) & 63]);' % (ind, k, random.randint(0, 9)))
        return out

def program():
    g = Gen()
    fns = []
    nf = random.randint(2, 4)
    for f in range(nf):
        body = ['__attribute__((noinline)) static void f%d(void) {' % f]
        for _ in range(random.randint(3, 8)):
            body += g.loop('    ')
        body.append('}')
        fns.append('\n'.join(body))
    main = ['int main(void) {'] + ['    f%d();' % f for f in range(nf)]
    main.append('    for (int k = 0; k < 64; k++) mix(A[k]);')
    main.append('    printf("end %llu\\n", H);')
    main.append('    return 0;\n}')
    return HDR + '\n'.join(fns) + '\n' + '\n'.join(main) + '\n'

CAP = 1 << 20

def _limit():
    # A file-size limit: a generated program writing more than CAP bytes of
    # output gets SIGXFSZ instead of filling the disk.
    resource.setrlimit(resource.RLIMIT_FSIZE, (CAP, CAP))

def run(cmd, timeout=60, cap=False):
    with tempfile.TemporaryFile() as out, tempfile.TemporaryFile() as err:
        try:
            rc = subprocess.run(cmd, stdout=out, stderr=err, timeout=timeout, preexec_fn=_limit if cap else None).returncode
        except subprocess.TimeoutExpired:
            return -1, '', 'timeout'
        out.seek(0)
        err.seek(0)
        return rc, out.read().decode('utf-8', 'replace'), err.read().decode('utf-8', 'replace')

def main():
    bad = skipped = 0
    check_ub = os.environ.get('WRAPFZ_UBSAN') == '1'
    for it in range(COUNT):
        random.seed(SEED * 100003 + it)
        src = os.path.join(WORK, 'w%d_%d.c' % (SEED, it))
        open(src, 'w').write(program())
        if check_ub:
            rc, _, err = run([CLANG, '-O0', '-w', '-fsanitize=undefined', '-fno-sanitize-recover=all', src, '-o', src + '.ub'])
            rc2, _, err2 = run([src + '.ub'], timeout=20, cap=True) if rc == 0 else (rc, '', err)
            if rc2 != 0:
                skipped += 1
                print('ub-skip', src, err2[:200])
                continue
        rc, _, err = run([CLANG, '-w', OPT, src, '-o', src + '.c.bin'])
        if rc != 0:
            skipped += 1
            print('clang-fail', src, err[:300])
            continue
        rcc, oc, _ = run([src + '.c.bin'], timeout=20, cap=True)
        if rcc != 0:
            skipped += 1
            print('clang-run-fail', src, rcc)
            continue
        rcg, _, _ = run([GCC, '-w', OPT, src, '-o', src + '.g.bin'])
        og = run([src + '.g.bin'], timeout=20, cap=True)[1] if rcg == 0 else None
        rcx, _, errx = run([CCC, '-w', OPT, src, '-o', src + '.x.bin'])
        if rcx != 0:
            bad += 1
            print('FAIL-BUILD', src, errx[:300])
            continue
        rx, ox, _ = run([src + '.x.bin'], timeout=20, cap=True)
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
        for e in ('.c.bin', '.g.bin', '.x.bin', '.ub'):
            try: os.remove(src + e)
            except OSError: pass
        if not diffs:
            os.remove(src)
    print('done %d bad %d skipped %d' % (COUNT, bad, skipped))

main()
