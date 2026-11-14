"""Mixed-type binary expression fuzzer: a OP b for operands of any two of
twelve integer types (_Bool, char types, short, int, long, long long and
__int128, signed and unsigned), on edge values, for the operators whose
result depends on the usual arithmetic conversions: / and % (signed or
unsigned division by the common type), >> and << (the left operand only
promoted, the count of any type), the comparisons, and ?: with arms of
different types (its value has the common type). Each is evaluated on
constants (folded at compile time), on volatile values (at run time),
inside a noinline function taking the operands as parameters, and as a
branch condition. Undefined cases are guarded in the C source (division
by zero, MIN / -1 in the common type, shift counts out of range, left
shifts of negative values), so every program is defined. clang is the
oracle; lines where gcc disagrees are dropped.

usage: binfz.py CCC CLANG WORK SEED COUNT   (env OPT, default -O2)
"""
import os, random, resource, subprocess, sys, tempfile

CCC, CLANG, WORK, SEED, COUNT = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]), int(sys.argv[5])
OPT = os.environ.get('OPT', '-O2')
GCC = os.environ.get('GCC', '/opt/homebrew/bin/gcc-15')
os.makedirs(WORK, exist_ok=True)

# (C type, bits, signed, rank)
TYPES = [('_Bool', 1, False, 0), ('signed char', 8, True, 1), ('unsigned char', 8, False, 1),
         ('short', 16, True, 2), ('unsigned short', 16, False, 2), ('int', 32, True, 3), ('unsigned', 32, False, 3),
         ('long', 64, True, 4), ('unsigned long', 64, False, 4), ('long long', 64, True, 5),
         ('unsigned long long', 64, False, 5), ('__int128', 128, True, 6), ('unsigned __int128', 128, False, 6)]

HDR = r'''#include <stdio.h>
typedef unsigned long long UX;
typedef unsigned __int128 U8;
static void pr(const char *tag, int neg, U8 v) { printf("%s %s%016llx%016llx\n", tag, neg ? "-" : "", (UX)(v >> 64), (UX)v); }
/* Prints any integer value exactly: its sign and magnitude. */
#define P(tag, e) do { __typeof__((e) + 0) v_ = (e); int n_ = v_ < 0; pr(tag, n_, n_ ? -(U8)v_ : (U8)v_); } while (0)
'''

def promoted(t):
    """Integer promotion: types narrower than int become int."""
    return TYPES[5] if t[1] < 32 else t

def common(a, b):
    a, b = promoted(a), promoted(b)
    if a[3] == b[3]:
        return a if not a[2] else b
    hi, lo = (a, b) if a[3] > b[3] else (b, a)
    if not hi[2]:
        return hi
    if hi[1] > lo[1]:
        return hi
    # Same width, the signed one ranks higher: its unsigned counterpart.
    return next(t for t in TYPES if t[1] == hi[1] and not t[2] and t[3] == hi[3])

def rng(t):
    if t[0] == '_Bool':
        return 0, 1
    return (-(1 << (t[1] - 1)), (1 << (t[1] - 1)) - 1) if t[2] else (0, (1 << t[1]) - 1)

def edge(t):
    lo, hi = rng(t)
    k = random.randrange(max(1, t[1]))
    c = [0, 1, lo, hi, lo + 1, hi - 1, -1, 2, -2, 7, -7, 100, -100, 1 << k, (1 << k) - 1, -(1 << k), random.randint(lo, hi)]
    return min(hi, max(lo, random.choice(c)))

def lit(t, v):
    name = t[0]
    if t[1] == 128:
        u = v & ((1 << 128) - 1)
        return '((%s)(((U8)0x%xULL << 64) | 0x%xULL))' % (name, u >> 64, u & ((1 << 64) - 1))
    if v < 0:
        return '((%s)(-%dLL - 1))' % (name, -v - 1)
    return '((%s)%dULL)' % (name, v)

def expr(op, ta, tb, a, b):
    """The C expression for a op b, guarded so it is always defined."""
    ct = common(ta, tb)
    if op in ('/', '%'):
        lo, _ = rng(ct)
        # Zero divisor, or the common type's minimum by -1: 0 instead.
        guard = '(%s) == 0' % b
        if ct[2]:
            guard += ' || ((%s) == -1 && (%s)(%s) == %s)' % (b, ct[0], a, lit(ct, lo))
        return '((%s) ? (%s)0 : (%s) %s (%s))' % (guard, ct[0], a, op, b)
    if op in ('>>', '<<'):
        pa = promoted(ta)
        cnt = '((%s) & %d)' % (b, pa[1] - 1)
        if op == '<<':
            # Left shift on a value converted to the unsigned promoted type.
            ut = next(t for t in TYPES if t[1] == pa[1] and not t[2] and t[3] == pa[3])
            return '((%s)(%s) << %s)' % (ut[0], a, cnt)
        return '((%s) >> %s)' % (a, cnt)
    if op == '?:':
        return '((%s) ? (%s) : (%s))' % (random.choice(['VZ', '!VZ', '(VZ + 1)']), a, b)
    return '((%s) %s (%s))' % (a, op, b)

OPS = ['/', '%', '>>', '<<', '<', '<=', '>', '>=', '==', '!=', '?:']

def program():
    tests, fns = [], []
    for n in range(random.randint(20, 50)):
        ta, tb = random.choice(TYPES), random.choice(TYPES)
        op = random.choice(OPS)
        va, vb = edge(ta), edge(tb)
        la, lb = lit(ta, va), lit(tb, vb)
        tests.append('    P("k%d", %s);' % (n, expr(op, ta, tb, la, lb)))
        tests.append('    { volatile %s a = %s; volatile %s b = %s; P("v%d", %s); }' % (ta[0], la, tb[0], lb, n, expr(op, ta, tb, 'a', 'b')))
        fns.append('__attribute__((noinline)) static void f%d(%s a, %s b) { P("f%d", %s); if (%s) puts("t%d"); else puts("e%d"); }'
                   % (n, ta[0], tb[0], n, expr(op, ta, tb, 'a', 'b'), expr(op, ta, tb, 'a', 'b'), n, n))
        tests.append('    f%d(%s, %s);' % (n, la, lb))
    return HDR + 'static volatile int VZ = %d;\n' % random.randrange(2) + '\n'.join(fns) + '\nint main(void) {\n' + '\n'.join(tests) + '\n    return 0;\n}\n'

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
    check_ub = os.environ.get('BINFZ_UBSAN') == '1'
    for it in range(COUNT):
        random.seed(SEED * 100003 + it)
        src = os.path.join(WORK, 'n%d_%d.c' % (SEED, it))
        open(src, 'w').write(program())
        if check_ub:
            # The guards make every program defined; BINFZ_UBSAN=1 checks that.
            rc, _, err = run([CLANG, '-O0', '-w', '-fsanitize=undefined', '-fno-sanitize-recover=all', src, '-o', src + '.ub'])
            rc2, _, err2 = run([src + '.ub'], cap=True) if rc == 0 else (rc, '', err)
            if rc2 != 0:
                skipped += 1
                print('ub-skip', src, err2[:200])
                continue
        rc, _, err = run([CLANG, '-w', OPT, src, '-o', src + '.c.bin'])
        if rc != 0:
            skipped += 1
            print('clang-fail', src, err[:300])
            continue
        _, oc, _ = run([src + '.c.bin'], cap=True)
        rcg, _, _ = run([GCC, '-w', OPT, src, '-o', src + '.g.bin'])
        og = run([src + '.g.bin'], cap=True)[1] if rcg == 0 else None
        rcx, _, errx = run([CCC, '-w', OPT, src, '-o', src + '.x.bin'])
        if rcx != 0:
            bad += 1
            print('FAIL-BUILD', src, errx[:300])
            continue
        rx, ox, _ = run([src + '.x.bin'], cap=True)
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
