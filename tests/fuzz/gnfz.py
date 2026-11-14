"""_Generic / typeof fuzzer: random expressions over variables of many
qualified types; each line prints the association _Generic picks, sizeof,
and the typeof-preserved qualifiers. clang is the oracle, lines where gcc
disagrees are dropped, lines clang rejects are dropped.

usage: gnfz.py CCC CLANG WORK SEED COUNT   (env OPT, default -O1)
"""
import os, random, re, subprocess, sys

CCC, CLANG, WORK, SEED, COUNT = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]), int(sys.argv[5])
OPT = os.environ.get('OPT', '-O1')
GCC = os.environ.get('GCC', '/opt/homebrew/bin/gcc-15')
os.makedirs(WORK, exist_ok=True)

SCAL = ['_Bool', 'char', 'signed char', 'unsigned char', 'short', 'unsigned short', 'int', 'unsigned',
        'long', 'unsigned long', 'long long', 'unsigned long long', 'float', 'double', 'long double',
        'enum E', 'enum EN', 'struct S', 'union U', '__int128', 'unsigned __int128', '_Complex double']
PTRB = ['char', 'signed char', 'unsigned char', 'short', 'int', 'unsigned', 'long', 'long long',
        'unsigned long', 'float', 'double', 'void', 'struct S', 'union U']

def assoc_types():
    out = []
    for t in SCAL:
        if not t.startswith('enum'):
            out.append(t)
    for b in PTRB:
        for q in ('', 'const '):
            out.append(q + b + ' *')
            if b != 'void':
                out.append(q + b + ' **')
                out.append(q + b + ' *const *')
    out += ['int (*)[3]', 'const int (*)[3]', 'int (*)[4]', 'int (*)[2][3]', 'char (*)[8]', 'int (**)[3]',
            'int (*)(void)', 'int (*)(int)', 'long (*)(int, long)', 'void (*)(void)', 'int *(*)(int)',            'int ***', 'const char ***']
    return out

ASSOC = assoc_types()
NAMES = {t: 't%d' % i for i, t in enumerate(ASSOC)}

HDR = r'''#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
enum E { E0, E1 = 5 };
enum EN { EN0 = -1, EN1 = 3 };
struct S { int a; signed char sc; unsigned char uc; const int ci; unsigned u3:3; int s5:5; unsigned long ul; const char *cp; int arr[3]; struct { short h; } in; };
union U { int i; float f; };
typedef const int CI;
typedef CI *PCI;
typedef signed char SC;
typedef int *IP;
typedef const IP CIP;
typedef int A3[3];
typedef const A3 CA3;
static int fi(void) { return 1; }
static int fii(int x) { return x; }
static long fl(int a, long b) { return a + b; }
static void fv(void) {}
static int *fip(int x) { static int z; return &z + 0 * x; }
static const char *fcs(void) { return "s"; }
static signed char fsc(void) { return 1; }
static int ncalls;
static int sidef(int x) { ncalls++; return x; }
'''

VARS = [
    ('int', 'i'), ('unsigned', 'u'), ('long', 'l'), ('unsigned long', 'ul'), ('long long', 'll'),
    ('short', 'sh'), ('unsigned short', 'us'), ('char', 'c'), ('signed char', 'sc'), ('unsigned char', 'uc'),
    ('_Bool', 'b'), ('float', 'f'), ('double', 'd'), ('long double', 'ld'), ('enum E', 'e'), ('enum EN', 'en'),
    ('const int', 'ci'), ('const signed char', 'csc'), ('const char', 'cc'), ('const double', 'cd'),
    ('int *', 'ip'), ('const int *', 'cip'), ('int *const', 'ipc'), ('const int *const', 'cipc'),
    ('int **', 'ipp'), ('const int **', 'cipp'), ('int *const *', 'ipcp'), ('const int *const *', 'cipcp'),
    ('char *', 'cp'), ('const char *', 'ccp'), ('signed char *', 'scp'), ('unsigned char *', 'ucp'),
    ('void *', 'vp'), ('const void *', 'cvp'), ('struct S', 's'), ('const struct S', 'cs'), ('struct S *', 'sp'),
    ('const struct S *', 'csp'), ('union U', 'un'), ('SC', 'tsc'), ('PCI', 'pci'), ('CIP', 'cipt'), ('CI', 'tci'),
    ('IP', 'tip'), ('__int128', 'i128'), ('unsigned __int128', 'u128'), ('_Complex double', 'cxd'),
    ('int8_t', 'i8'), ('uint8_t', 'u8'), ('int64_t', 'i64'), ('size_t', 'sz'), ('ptrdiff_t', 'pd'),
    ('long *', 'lp'), ('enum E *', 'ep'), ('double *', 'dp'), ('const double *', 'cdp'),
]
ARRS = [('int', 'a3', '[3]'), ('const int', 'ca3', '[3]'), ('int', 'm23', '[2][3]'), ('const int', 'cm23', '[2][3]'),
        ('char', 'buf', '[8]'), ('const char', 'cbuf', '[8]'), ('signed char', 'sca', '[4]'), ('A3', 'ta3', ''),
        ('CA3', 'tca3', ''), ('int *', 'pa', '[2]'), ('const int *', 'cpa', '[2]'), ('int', 'a2', '[2][2][3]')]
PTA = [('int (*', 'pa3', ')[3]'), ('const int (*', 'cpa3', ')[3]'), ('int (*', 'pm', ')[2][3]')]
FUNCS = ['fi', 'fii', 'fl', 'fv', 'fip', 'fsc']

NUM = [v for t, v in VARS if '*' not in t and t not in ('struct S', 'const struct S', 'union U', 'PCI', 'CIP', 'IP')]
INTV = [v for t, v in VARS if v in ('i', 'u', 'l', 'ul', 'll', 'sh', 'us', 'c', 'sc', 'uc', 'b', 'e', 'en', 'ci', 'csc', 'cc', 'tsc', 'tci', 'i8', 'u8', 'i64', 'sz', 'pd', 'i128', 'u128')]
PTRV = [v for t, v in VARS if '*' in t or t in ('PCI', 'CIP', 'IP')] + ['a3', 'ca3', 'buf', 'cbuf', 'sca', 'ta3', 'tca3', 'pa3', 'cpa3', 'pm', 'm23', 'cm23']
LITS = ['0', '1', '-1', '1u', '1L', '1UL', '1LL', '1ULL', '0x7fffffff', '0x80000000', '2147483648', '0xffffffff',
        '4294967296', '0x8000000000000000', "'a'", "L'a'", "u'a'", "U'a'", "u8'a'", '1.0', '1.0f', '1.0L', '(char)1',
        '(short)1', 'sizeof(int)', '_Alignof(long)', '(void *)0', '0x7fffffffffffffff', '9223372036854775807',
        '07777777777', '1 == 1', 'E1', 'EN0', '"str"', 'u8"s"', 'NULL', '(size_t)1']

def R(xs):
    return random.choice(xs)

def ptrexpr(d):
    k = random.randrange(14)
    if k < 4 or d > 2:
        return R(PTRV)
    if k == 4: return '&' + R([v for t, v in VARS if v not in ('ipc',)] + ['a3', 'ca3', 'ta3', 'tca3', 'm23', 'cm23', 's.a', 's.sc', 'cs.a', 'cs.cp', 's.arr', 'cs.arr', 'sp->ci', 'csp->in', 'm23[1]', 'cm23[0]', 'buf[1]', 'cbuf[2]'])
    if k == 5: return '(%s + %s)' % (ptrexpr(d + 1), R(['1', 'i', 'sc', 'uc', 'l', 'b']))
    if k == 6: return '(%s ? %s : %s)' % (R(['i', 'c', '1']), ptrexpr(d + 1), R([ptrexpr(d + 1), '0', '(void *)0', 'NULL']))
    if k == 7: return '((%s)%s)' % (R(ASSOC[len(SCAL):len(SCAL) + 40] + ['CI *', 'const SC *', 'PCI', 'IP', 'const IP', 'CIP']), R(['vp', 'ip', '0', 'cvp']))
    if k == 8: return R(['s.arr', 'cs.arr', 'sp->arr', 'csp->arr', 's.cp', 'cs.cp', 'm23[i & 1]', 'cm23[0]', '*pm', 'pa[1]', 'cpa[0]', 'a2[1]', '*cpa3'])
    if k == 9: return '(%s, %s)' % (R(NUM), ptrexpr(d + 1))
    if k == 10: return R(['fip(1)', '(&*cip)', '&cip[1]', '(cip++)', '(--cp)', '(ccp = cp)', '(cip += 1)'])
    if k == 11: return R(FUNCS + ['&' + R(FUNCS), '*fi', '**fii'])
    if k == 12: return '(%s){0}' % R(['int *', 'const int *', 'char *', 'const char *', 'int [2]', 'struct S *'])
    return R(['"lit"', 'u8"lit"', '&"lit"[1]', '__func__', '(int[]){1, 2}'])

def numexpr(d):
    k = random.randrange(16)
    if k < 4 or d > 2:
        return R(NUM)
    if k == 4: return R(LITS)
    if k == 5: return '(%s %s %s)' % (numexpr(d + 1), R(['+', '-', '*', '/', '%', '&', '|', '^', '<<', '>>', '<', '==', '&&', '||']), numexpr(d + 1))
    if k == 6: return '(%s%s)' % (R(['-', '+', '~', '!']), numexpr(d + 1))
    if k == 7: return '(%s ? %s : %s)' % (numexpr(d + 1), numexpr(d + 1), numexpr(d + 1))
    if k == 8: return '((%s)%s)' % (R(SCAL[:15] + ['const int', 'SC', 'CI', 'int8_t', 'const signed char', 'volatile int', 'enum E']), numexpr(d + 1))
    if k == 9: return '(%s, %s)' % (numexpr(d + 1), numexpr(d + 1))
    if k == 10: return R(['s.a', 's.sc', 's.uc', 'cs.ci', 's.u3', 's.s5', '(s.u3 + 0)', '(+s.s5)', 'sp->ul', 'csp->sc', 's.in.h', 'un.f', 's.arr[1]', 'cs.arr[0]', '(s.u3 << 1)'])
    if k == 11: return '*' + R(['ip', 'cip', 'pci', 'scp', 'ccp', 'cipc', '*ipp', '*cipp', 'lp', 'ep', 'cdp', 'pa3[0]', 'cpa3[0]', 'm23'])
    if k == 12: return R(['a3[1]', 'ca3[i & 1]', 'sca[0]', 'cbuf[1]', 'm23[1][2]', 'cm23[0][0]', '1[a3]', 'ta3[0]', 'tca3[1]', 'pa3[0][1]'])
    if k == 13: return R(['(%s)++' % R(['i', 'sc', 'uc', 'b', 'e', 'f', 'sh', 'ld']), '++%s' % R(['c', 'u', 'tsc', 'i8']),
                         '(%s = %s)' % (R(['i', 'sc', 'u', 'd', 'b', 'e', 'c']), numexpr(d + 1)),
                         '(%s %s= %s)' % (R(['i', 'sc', 'u', 'd', 'uc', 'sh', 'l']), R(['+', '-', '*', '<<', '|']), R(['1', '2', 'i', 'd', 'f']))])
    if k == 14: return R(['fi()', 'fii(2)', 'fl(1, 2)', 'fsc()', 'sidef(3)', '(ptrdiff_t)(ip - ip)', '(cip - cip)', '(cp - cp)', 'sizeof i', 'sizeof(a3)', '_Alignof(double)', '(ip == cip)', '!ip', '(i ? 1.0f : 1)', '(c ? sc : uc)', '(b ? b : b)', '(e ? e : 1u)'])
    return R(['cxd', '(cxd + 1)', '__real__ cxd', 'i128', '(i128 + 1)', 'u128', '(u128 >> 1)', '(cs.ci)', '(tci + 0)', 'csc', '(+csc)', '(-uc)'])

def expr():
    return ptrexpr(0) if random.random() < 0.45 else numexpr(0)

def build(lines, path):
    out = [HDR]
    ts = ASSOC
    out.append('#define T(e) _Generic((e), ' + ', '.join('%s: "%s"' % (t, t.replace('"', '')) for t in ts) + ', default: "other")\n')
    out.append('#define TQ(e) _Generic((__typeof__(e) *)0, ' + ', '.join('%s *: "%s"' % (q + t, q + t) for t in ['int', 'char', 'signed char', 'long', 'double', 'unsigned', 'struct S', 'int *', 'char *', 'int *const', 'char *const'] for q in ('', 'const ')) + ', default: "other")\n')
    out.append('#define P(e) printf("%d %s %s %zu %s\\n", __LINE__, #e, T(e), sizeof(e), TQ(e));\n')
    out.append('int main(void) {\n')
    for t, v in VARS:
        out.append('  %s %s = {0};\n' % (t, v) if t in ('struct S', 'const struct S', 'union U') or t.startswith('const') or t in ('PCI', 'CIP', 'CI') else '  %s %s = 0;\n' % (t, v))
    out.append('  sp = &s; csp = &cs; ip = &i; cp = (char *)&i; scp = &sc; ucp = &uc; lp = &l; ep = &e; dp = &d; ipp = &ip; vp = ip; tip = ip;\n')
    for t, v, sfx in ARRS:
        out.append('  %s %s%s = {0};\n' % (t, v, sfx))
    for a, v, b in PTA:
        out.append('  %s%s%s = 0;\n' % (a, v, b))
    out.append('  (void)fv; (void)fl; (void)fip; (void)fcs; (void)fsc; (void)fii; (void)fi;\n')
    first = len(''.join(out).split('\n'))
    for ln in lines:
        out.append('  P(%s)\n' % ln)
    out.append('  printf("calls %d\\n", ncalls);\n  return 0;\n}\n')
    open(path, 'w').write(''.join(out))
    return first

def run(cmd):
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
        return p.returncode, p.stdout, p.stderr
    except subprocess.TimeoutExpired:
        return -1, '', 'timeout'

def runbin(path):
    rc, out, err = run([path])
    return out if rc == 0 else None

def main():
    bad = skipped = 0
    for it in range(COUNT):
        random.seed(SEED * 100003 + it)
        lines = [expr() for _ in range(150)]
        src = os.path.join(WORK, 'g%d_%d.c' % (SEED, it))
        for attempt in range(6):
            first = build(lines, src)
            rc, _, err = run([CLANG, '-std=gnu17', '-Wno-everything', '-Werror=pointer-type-mismatch', '-Werror=conditional-type-mismatch', '-c', src, '-o', src + '.o'])
            if rc == 0:
                break
            badl = set(int(m) for m in re.findall(r':(\d+):\d+: error', err))
            keep = [ln for k, ln in enumerate(lines) if (first + k) not in badl]
            if len(keep) == len(lines):
                keep = []
            lines = keep
        else:
            skipped += 1
            continue
        rc, _, err = run([CLANG, '-std=gnu17', '-w', OPT, src, '-o', src + '.c.bin'])
        rc2, _, _ = run([GCC, '-std=gnu17', '-w', OPT, src, '-o', src + '.g.bin'])
        oc = runbin(src + '.c.bin')
        og = runbin(src + '.g.bin') if rc2 == 0 else None
        if oc is None:
            skipped += 1
            continue
        rc3, _, err3 = run([CCC, '-w', OPT, src, '-o', src + '.x.bin'])
        ox = runbin(src + '.x.bin') if rc3 == 0 else None
        if ox is None:
            bad += 1
            print('FAIL-BUILD', src, err3[:300])
            continue
        cl, xl = oc.splitlines(), ox.splitlines()
        gl = og.splitlines() if og else None
        diffs = []
        for k, a in enumerate(cl):
            x = xl[k] if k < len(xl) else '<missing>'
            if a != x and (gl is None or k >= len(gl) or gl[k] == a):
                diffs.append((a, x))
        if diffs:
            bad += 1
            with open(src + '.diff', 'w') as fh:
                for a, x in diffs:
                    fh.write('clang %s\nccc   %s\n' % (a, x))
            print('DIFF', src, len(diffs))
        for ext in ('.o', '.c.bin', '.g.bin', '.x.bin'):
            try: os.remove(src + ext)
            except OSError: pass
        if not diffs:
            os.remove(src)
    print('done %d bad %d skipped %d' % (COUNT, bad, skipped))

main()
