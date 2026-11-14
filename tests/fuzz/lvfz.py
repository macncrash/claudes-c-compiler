"""Lvalue fuzzer: compound assignment (+= -= *= /= %= <<= >>= &= |= ^=),
++ and -- on lvalues of every kind: _Bool, char types, short, int, long,
long long, an enum, float, double, and bit-fields of several widths and
signedness (_Bool:1 included), reached as locals, globals, struct members,
array elements (also with an index that has a side effect, which must
happen once), through pointers and volatiles. Right-hand sides are
constants and values of every type, so an integer lvalue may take a
floating right-hand side (i *= 2.5) and the reverse. The value of each
assignment is used too: it is the value stored, converted to the lvalue's
type (for a bit-field, its width). Every object is printed after each
block. Programs UBSan flags are skipped; lines where gcc disagrees with
clang are dropped.

usage: lvfz.py CCC CLANG WORK SEED COUNT   (env OPT, default -O2)
"""
import os, random, resource, subprocess, sys, tempfile

CCC, CLANG, WORK, SEED, COUNT = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]), int(sys.argv[5])
OPT = os.environ.get('OPT', '-O2')
GCC = os.environ.get('GCC', '/opt/homebrew/bin/gcc-15')
os.makedirs(WORK, exist_ok=True)

# tag -> (C type, kind) kind: b bool, i signed int, u unsigned int, e enum, f floating
TYPES = {
    'B': ('_Bool', 'b'), 'c': ('char', 'i'), 'sc': ('signed char', 'i'), 'uc': ('unsigned char', 'u'),
    'h': ('short', 'i'), 'uh': ('unsigned short', 'u'), 'i': ('int', 'i'), 'u': ('unsigned', 'u'),
    'l': ('long', 'i'), 'ul': ('unsigned long', 'u'), 'x': ('long long', 'i'), 'e': ('enum E', 'e'),
    'f': ('float', 'f'), 'd': ('double', 'f'),
}
# bit-field members: name -> (base type, width, kind)
BITS = {'b1': ('_Bool', 1, 'b'), 'u3': ('unsigned', 3, 'u'), 's5': ('int', 5, 'i'), 'u7': ('unsigned', 7, 'u'),
        's13': ('int', 13, 'i'), 'u31': ('unsigned', 31, 'u'), 'ux40': ('unsigned long long', 40, 'u'),
        'sx33': ('long long', 33, 'i')}

HDR = r'''#include <stdio.h>
enum E { E0, E1, E2, E5 = 5, E9 = 9 };
static volatile int VZ = 0;
/* An assignment's value, of any type, mixed in without undefined conversions. */
static unsigned long long cv(double t) { return t == t && t > -1e18 && t < 1e18 ? (unsigned long long)(long long)t : 7; }
'''

def struct_decl():
    out = ['struct S {']
    for t, (ct, k) in TYPES.items():
        out.append('    %s m_%s;' % (ct, t))
    for n, (bt, w, k) in BITS.items():
        out.append('    %s %s : %d;' % (bt, n, w))
    out.append('};')
    return '\n'.join(out)

def const(kind_hint=None):
    c = random.randrange(10)
    if c < 4:
        return str(random.choice([0, 1, 2, 3, 5, 7, 9, 13, 100, 127, 255, 1000]))
    if c < 6:
        return '-%d' % random.choice([1, 2, 3, 7, 100, 129])
    if c == 6:
        return random.choice(['200u', '65535u', '4294967295u', '3000000000u', '1ul', '7ull'])
    if c == 7:
        return random.choice(['2.5', '0.5', '3.0', '1.25', '0.25', '2.5f', '1.5f'])
    if c == 8:
        return random.choice(['E1', 'E5', 'E9'])
    return random.choice(["'a'", "(char)200", "(short)-3", "(_Bool)1"])

def is_float_text(s):
    return any(ch in s for ch in '.e') and not s.startswith("'") and not s.startswith('E')

class Gen:
    def __init__(self):
        self.lvals = []   # (text, kind, is_bitfield, readable_text)
        for t, (ct, k) in TYPES.items():
            for form in ('g_%s', 'l_%s', 's.m_%s', 'gs.m_%s', 'a_%s[%%d]', '(*p_%s)', 'ps->m_%s', 'v_%s'):
                self.lvals.append((form % t, k, False))
        for n, (bt, w, k) in BITS.items():
            for form in ('s.%s', 'gs.%s', 'ps->%s', 'as[%%d].%s'):
                self.lvals.append((form % n, k, True))

    def lval(self):
        text, kind, bf = random.choice(self.lvals)
        if '%d' in text:
            text = text % random.randrange(3)
        return text, kind, bf

    def rhs(self):
        if random.random() < 0.5:
            return const()
        text, kind, bf = self.lval()
        return text

    def stmt(self):
        lv, kind, bf = self.lval()
        c = random.randrange(10)
        if c < 2:
            op = random.choice(['++', '--'])
            pre = random.random() < 0.5
            e = op + lv if pre else lv + op
            if kind == 'b' and op == '--':  # _Bool -- is fine in C; kept
                pass
            return self.use(e)
        # Index with a side effect, evaluated once.
        if '[' in lv and random.random() < 0.3:
            lv = lv[:lv.index('[')] + '[ix++ % 3]' + lv[lv.index(']') + 1:]
        r = self.rhs()
        rf = is_float_text(r) or r.startswith(('g_f', 'l_f', 's.m_f', 'gs.m_f', 'a_f', '(*p_f', 'ps->m_f', 'v_f', 'g_d', 'l_d', 's.m_d', 'gs.m_d', 'a_d', '(*p_d', 'ps->m_d', 'v_d'))
        if kind == 'f' or rf:
            op = random.choice(['+=', '-=', '*=', '/='])
        else:
            op = random.choice(['+=', '-=', '*=', '/=', '%=', '>>=', '&=', '|=', '^='] + (['<<='] if kind in 'ub' else []))
        if op in ('<<=', '>>='):
            r = '(%s & 7)' % r
        if op in ('/=', '%='):
            # A divisor that is never 0 (NaN-free too): r or 3.
            r = '(%s ? %s : 3)' % (r, r)
        e = '%s %s %s' % (lv, op, r)
        return self.use(e)

    def use(self, e):
        c = random.randrange(3)
        if c == 0:
            return '    %s;' % e
        if c == 1:
            return '    acc = acc * 31 + cv(%s);' % e
        return '    acc ^= cv((%s) * 4.0);' % e

def show():
    out = []
    fmt = {'b': '%d', 'i': '%lld', 'u': '%llu', 'e': '%d', 'f': '%a'}
    cast = {'b': '(int)', 'i': '(long long)', 'u': '(unsigned long long)', 'e': '(int)', 'f': '(double)'}
    for t, (ct, k) in TYPES.items():
        items = ['g_%s' % t, 'l_%s' % t, 's.m_%s' % t, 'gs.m_%s' % t, 'a_%s[0]' % t, 'a_%s[1]' % t, 'a_%s[2]' % t, 'v_%s' % t]
        out.append('    printf("%s %s\\n", %s);' % (t, ' '.join([fmt[k]] * len(items)), ', '.join(cast[k] + x for x in items)))
    for n, (bt, w, k) in BITS.items():
        items = ['s.%s' % n, 'gs.%s' % n, 'as[0].%s' % n, 'as[1].%s' % n, 'as[2].%s' % n]
        out.append('    printf("%s %s\\n", %s);' % (n, ' '.join([fmt[k]] * len(items)), ', '.join(cast[k] + x for x in items)))
    out.append('    printf("acc %llu ix %d\\n", acc, ix);')
    return out

def program():
    g = Gen()
    out = [HDR, struct_decl(), 'static struct S gs, as[3];']
    for t, (ct, k) in TYPES.items():
        out.append('static %s g_%s = %s, a_%s[3] = {%s};' % (ct, t, init(k), t, ', '.join(init(k) for _ in range(3))))
    fns = []
    nf = random.randint(1, 3)
    for f in range(nf):
        body = ['__attribute__((noinline)) static unsigned long long f%d(struct S *ps) {' % f, '    unsigned long long acc = %d; int ix = %d;' % (random.randrange(9), random.randrange(3))]
        body.append('    struct S s = *ps;')
        for t, (ct, k) in TYPES.items():
            body.append('    %s l_%s = %s; volatile %s v_%s = %s; %s *p_%s = &%s;'
                        % (ct, t, init(k), ct, t, init(k), ct, t, random.choice(['l_%s' % t, 'g_%s' % t, 'a_%s[1]' % t, 's.m_%s' % t])))
        for blk in range(random.randint(2, 4)):
            for _ in range(random.randint(4, 12)):
                body.append(g.stmt())
            body.append('    printf("f%d block %d\\n");' % (f, blk))
            body += show()
        body.append('    *ps = s;')
        body.append('    return acc;')
        body.append('}')
        fns.append('\n'.join(body))
    out += fns
    out.append('int main(void) {')
    out.append('    static struct S ms;')
    for f in range(nf):
        out.append('    printf("r%d %%llu\\n", f%d(%s));' % (f, f, random.choice(['&ms', '&gs', '&as[2]'])))
    out.append('    return 0;\n}')
    return '\n'.join(out) + '\n'

def init(k):
    return {'b': random.choice(['0', '1']), 'i': str(random.randint(-50, 50)), 'u': str(random.randint(0, 300)),
            'e': random.choice(['E0', 'E1', 'E2', 'E5', 'E9']), 'f': random.choice(['0.5', '-2.25', '7.0', '100.125', '0.0'])}[k]

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
    for it in range(COUNT):
        random.seed(SEED * 100003 + it)
        src = os.path.join(WORK, 'v%d_%d.c' % (SEED, it))
        open(src, 'w').write(program())
        rc, _, err = run([CLANG, '-O0', '-w', '-fsanitize=undefined,address', '-fsanitize=float-divide-by-zero', '-fno-sanitize-recover=all', src, '-o', src + '.ub'])
        if rc != 0:
            skipped += 1
            print('clang-fail', src, err[:300])
            continue
        rc, _, err = run([src + '.ub'], cap=True)
        os.remove(src + '.ub')
        if rc != 0:
            skipped += 1
            print('ub-skip', src, err[:200])
            continue
        run([CLANG, '-w', OPT, src, '-o', src + '.c.bin'])
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
        for e in ('.c.bin', '.g.bin', '.x.bin'):
            try: os.remove(src + e)
            except OSError: pass
        if not diffs:
            os.remove(src)
    print('done %d bad %d skipped %d' % (COUNT, bad, skipped))

main()
