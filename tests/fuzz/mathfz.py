"""Math library and floating-point builtin fuzzer: calls to the <math.h>
functions whose results IEEE 754 / C pin down exactly (sqrt, fma, floor,
ceil, trunc, round, rint, nearbyint, lround, lrint, fmod, remainder,
remquo, frexp, ldexp, scalbn, ilogb, logb, modf, nextafter, fmin, fmax,
fdim, copysign, fabs, classification macros, __builtin_ forms), in float,
double and long double, on edge values (+-0, +-inf, NaN, subnormals,
halfway cases, huge values), once with constant arguments (which a
compiler may fold) and once with values it cannot see. Each result is
printed exactly (%a; NaN as "nan": its sign and payload are unspecified).
clang is the oracle; lines where gcc disagrees are dropped.

usage: mathfz.py CCC CLANG WORK SEED COUNT   (env OPT, default -O2)
"""
import os, random, subprocess, sys

CCC, CLANG, WORK, SEED, COUNT = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]), int(sys.argv[5])
OPT = os.environ.get('OPT', '-O2')
GCC = os.environ.get('GCC', '/opt/homebrew/bin/gcc-15')
os.makedirs(WORK, exist_ok=True)

HDR = r'''#include <stdio.h>
#include <math.h>
#include <float.h>
static void pd(int line, double v) { if (isnan(v)) printf("%d nan\n", line); else printf("%d %a\n", line, v); }
static void pf(int line, float v) { if (isnan(v)) printf("%d nan\n", line); else printf("%d %a\n", line, (double)v); }
static void pl(int line, long double v) { if (isnan(v)) printf("%d nan\n", line); else printf("%d %La\n", line, v); }
static void pi(int line, long v) { printf("%d %ld\n", line, v); }
static volatile double VD[1]; static volatile float VF[1]; static volatile long double VL[1];
static double D(double x) { VD[0] = x; return VD[0]; }
static float F(float x) { VF[0] = x; return VF[0]; }
static long double L(long double x) { VL[0] = x; return VL[0]; }
'''

VALS = ['0.0', '-0.0', '1.0', '-1.0', '0.5', '-0.5', '1.5', '2.5', '-2.5', '3.5', '0.49999999999999994', '1e308',
        '-1e308', 'DBL_MIN', 'DBL_TRUE_MIN', '-DBL_TRUE_MIN', 'DBL_MAX', 'INFINITY', '-INFINITY', 'NAN', '7.0',
        '-7.25', '1e-310', '4503599627370496.5', '4503599627370497.0', '9007199254740993.0', '123456.789',
        '0x1.fffffffffffffp+52', '-0x1p-1022', '3.0', '10.0', '0.1']
FVALS = ['0.0f', '-0.0f', '1.0f', '-1.5f', '2.5f', '-3.5f', '1e38f', 'FLT_MIN', 'FLT_TRUE_MIN', 'FLT_MAX', 'INFINITY',
         '-INFINITY', 'NAN', '8388608.5f', '0.1f', '16777217.0f', '-7.25f', '1e-40f', '3.0f']
SMALL = ['0.0', '-0.0', '0.5', '-0.5', '1.5', '2.5', '-2.5', '3.49', '-1e15', '1e15', '4503599627370496.5', '7.0', '-7.25']

# name, args, kind ('d' result double, 'i' integer), arg kinds
DFUN = [
    ('sqrt', 1, 'd'), ('fabs', 1, 'd'), ('floor', 1, 'd'), ('ceil', 1, 'd'), ('trunc', 1, 'd'), ('round', 1, 'd'),
    ('rint', 1, 'd'), ('nearbyint', 1, 'd'), ('logb', 1, 'd'), ('fmod', 2, 'd'), ('remainder', 2, 'd'),
    ('fmin', 2, 'd'), ('fmax', 2, 'd'), ('fdim', 2, 'd'), ('copysign', 2, 'd'), ('nextafter', 2, 'd'), ('fma', 3, 'd'),
]

def R(xs):
    return random.choice(xs)

def wrap(v, rt, w):
    return '%s(%s)' % (w, v) if rt else v

def line(n):
    rt = random.random() < 0.5
    k = random.randrange(10)
    if k < 4:
        name, na, _ = R(DFUN)
        suf, vals, w, pr = R([('', VALS, 'D', 'pd'), ('f', FVALS, 'F', 'pf'), ('l', VALS, 'L', 'pl')])
        if suf == 'l':
            vals = [v + 'L' if v[0].isdigit() or v[0] == '-' and v[1].isdigit() else v for v in VALS]
        args = ', '.join(wrap(R(vals), rt, w) for _ in range(na))
        fn = name + suf
        if random.random() < 0.3:
            fn = '__builtin_' + fn
        return '%s(__LINE__, %s(%s));' % (pr, fn, args)
    if k == 4:
        fn, w = R([('lround', 'D'), ('lrint', 'D'), ('llround', 'D'), ('llrint', 'D'), ('lroundf', 'F'), ('lrintf', 'F')])
        v = R(SMALL)
        if w == 'F':
            v = v + 'f' if 'e' not in v and v[-1].isdigit() else '2.5f'
        return 'pi(__LINE__, (long)%s(%s));' % (fn, wrap(v, rt, w))
    if k == 5:
        v = R(VALS)
        return 'pi(__LINE__, ilogb(%s) == FP_ILOGB0 ? -99999 : ilogb(%s) == FP_ILOGBNAN ? -88888 : ilogb(%s));' % ((wrap(v, rt, 'D'),) * 3)
    if k == 6:
        v = R(VALS)
        return '{ int e = 0; double m = frexp(%s, &e); pd(__LINE__, m); if (isfinite(m)) pi(__LINE__, e); }' % wrap(v, rt, 'D')
    if k == 7:
        v = R(VALS)
        e = R(['0', '1', '-1', '1023', '-1074', '-1075', '2000', '-2000', '52', '-1022'])
        return 'pd(__LINE__, %s(%s, %s));' % (R(['ldexp', 'scalbn', '__builtin_ldexp']), wrap(v, rt, 'D'), e)
    if k == 8:
        v = R(VALS)
        return '{ double ip = 0; double fr = modf(%s, &ip); pd(__LINE__, fr); pd(__LINE__, ip); }' % wrap(v, rt, 'D')
    v = R(VALS + ['-NAN'])
    m = R(['isnan', 'isinf', 'isfinite', 'isnormal', 'signbit', 'fpclassify', '__builtin_isnan',
           '__builtin_signbit'])
    if m == 'fpclassify':
        return 'pi(__LINE__, fpclassify(%s) == FP_NAN ? 1 : fpclassify(%s) == FP_INFINITE ? 2 : fpclassify(%s) == FP_ZERO ? 3 : fpclassify(%s) == FP_SUBNORMAL ? 4 : 5);' % ((wrap(v, rt, 'D'),) * 4)
    if m in ('signbit', '__builtin_signbit') and 'NAN' in v:
        v = '1.0'
    return 'pi(__LINE__, !!%s(%s));' % (m, wrap(v, rt, 'D'))

def program():
    out = [HDR, 'int main(void) {']
    for i in range(random.randint(40, 90)):
        out.append('    ' + line(i))
    out.append('    { int q = 0; double r = remquo(D(7.5), D(2.0), &q); pd(__LINE__, r); pi(__LINE__, q & 7); }')
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
        src = os.path.join(WORK, 'f%d_%d.c' % (SEED, it))
        open(src, 'w').write(program())
        rc, _, err = run([CLANG, '-w', OPT, src, '-o', src + '.c.bin', '-lm'])
        if rc != 0:
            skipped += 1
            print('clang-fail', src, err[:200])
            continue
        _, oc, _ = run([src + '.c.bin'])
        rcg, _, _ = run([GCC, '-w', OPT, src, '-o', src + '.g.bin', '-lm'])
        og = run([src + '.g.bin'])[1] if rcg == 0 else None
        rcx, _, errx = run([CCC, '-w', OPT, src, '-o', src + '.x.bin', '-lm'])
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
