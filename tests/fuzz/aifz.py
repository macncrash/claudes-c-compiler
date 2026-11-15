"""Automatic aggregate initializer fuzzer: local structs, arrays and unions
initialized from runtime values: designators in any order and designated
again (the later one wins), a braced initializer of a member after a
designator into it (the braces replace the whole member), brace elision,
string literals in char arrays of exact and larger sizes, compound
literals as member values, arrays of structs with designators and GNU
ranges, arrays sized by their initializer, and objects re-initialized in
loops (every member not named must be zero again each time). Every member
is hashed, never padding. Union members all span the union's 8 bytes, so
whichever member is initialized defines every byte.

usage: aifz.py CCC CLANG WORK SEED COUNT   (env OPT, default -O2)
"""
import os, random, resource, subprocess, sys, tempfile

CCC, CLANG, WORK, SEED, COUNT = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]), int(sys.argv[5])
OPT = os.environ.get('OPT', '-O2')
GCC = os.environ.get('GCC', '/opt/homebrew/bin/gcc-15')
os.makedirs(WORK, exist_ok=True)

HDR = r'''#include <stdio.h>
typedef unsigned long UL;
static UL H;
static void hb(const void *p, unsigned n) { const unsigned char *c = p; while (n--) H = (H ^ *c++) * 1099511628211UL; }
static void hv(UL v) { H = (H ^ v) * 1099511628211UL; }
/* Every member spans all 8 bytes: initializing any one defines the union. */
union U { unsigned long long q; unsigned u[2]; unsigned char c[8]; double d; struct { unsigned lo, hi; } s; };
static void hU(const union U *p) { hb(p->c, 8); }
'''

SCALARS = ['char', 'unsigned char', 'short', 'int', 'unsigned', 'long', 'float', 'double']

class T:
    def __init__(self, k, prev):
        self.k = k
        self.name = 'struct S%d' % k
        self.m = []   # (kind, name, ctype, extra)
        for i in range(random.randint(2, 7)):
            c = random.randrange(12)
            nm = 'm%d' % i
            if c < 5:
                self.m.append(('s', nm, random.choice(SCALARS), None))
            elif c < 7:
                self.m.append(('a', nm, random.choice(['int', 'short', 'unsigned char', 'double']), random.choice([1, 2, 3, 5, 7])))
            elif c == 7:
                self.m.append(('c', nm, 'char', random.choice([1, 3, 4, 5, 8, 13])))
            elif c == 8 and prev:
                self.m.append(('n', nm, None, random.choice(prev)))
            elif c == 9:
                self.m.append(('u', nm, 'union U', None))
            else:
                self.m.append(('b', nm, random.choice(['unsigned', 'int']), random.randint(1, 15)))

    def decl(self):
        out = ['%s {' % self.name]
        for kind, nm, t, x in self.m:
            if kind == 's' or kind == 'u':
                out.append('    %s %s;' % (t, nm))
            elif kind in ('a', 'c'):
                out.append('    %s %s[%d];' % (t, nm, x))
            elif kind == 'n':
                out.append('    %s %s;' % (x.name, nm))
            else:
                out.append('    %s %s : %d;' % (t, nm, x))
        out.append('};')
        return '\n'.join(out)

    def hasher(self):
        out = ['static void h%d(const %s *p) {' % (self.k, self.name)]
        for kind, nm, t, x in self.m:
            if kind == 's':
                out.append('    { %s v = p->%s; hb(&v, sizeof v); }' % (t, nm))
            elif kind in ('a', 'c'):
                out.append('    for (int i = 0; i < %d; i++) { %s v = p->%s[i]; hb(&v, sizeof v); }' % (x, t, nm))
            elif kind == 'n':
                out.append('    h%d(&p->%s);' % (x.k, nm))
            elif kind == 'u':
                out.append('    hU(&p->%s);' % nm)
            else:
                out.append('    hv((UL)(long)p->%s);' % nm)
        out.append('}')
        return '\n'.join(out)

def val(t):
    """A value expression for a scalar of type t, from the runtime parameters."""
    c = random.randrange(6)
    if c == 0:
        return str(random.randint(-9, 99))
    if c == 1:
        return 'a'
    if c == 2:
        return '(b + %d)' % random.randint(0, 9)
    if c == 3:
        return '(%s)(c * %d)' % (t if t else 'int', random.randint(1, 5))
    if c == 4 and t in ('float', 'double'):
        return 'd'
    return '(a ^ %d)' % random.randint(1, 255)

def bitval(w, t):
    # A value that fits the field (a signed field's range too).
    if t == 'unsigned':
        return '(%s & %d)' % (random.choice(['a', 'b', 'c']), (1 << w) - 1)
    return '((%s & %d) - %d)' % (random.choice(['a', 'b', 'c']), (1 << w) - 1, 1 << (w - 1)) if w > 1 else '0'

def union_init():
    c = random.randrange(5)
    if c == 0:
        return '{ .q = (unsigned long long)a * %dULL + b }' % random.randint(1, 99999)
    if c == 1:
        return '{ .u = { b, %s } }' % random.choice(['a', 'c', '7'])
    if c == 2:
        return '{ .c = { %s } }' % ', '.join(random.choice(['a', 'b', 'c', '9', '200']) for _ in range(random.randint(1, 8)))
    if c == 3:
        return '{ .d = %s }' % random.choice(['d', '(double)a', '1.5'])
    return '{ .s = { .hi = a, .lo = c } }'

def member_init(kind, t, x, depth):
    if kind == 's':
        return val(t)
    if kind == 'a':
        n = random.randint(1, x)
        if random.random() < 0.4:
            return '{ %s }' % ', '.join('[%d] = %s' % (i, val(t)) for i in random.sample(range(x), n))
        return '{ %s }' % ', '.join(val(t) for _ in range(n))
    if kind == 'c':
        # A string literal that fits: up to x chars (the NUL dropped when it fills it).
        s = ''.join(random.choice('abcxyz019') for _ in range(random.randint(0, x)))
        if random.random() < 0.3:
            return '{ %s }' % ', '.join(random.choice(['a', "'q'", '(char)b']) for _ in range(random.randint(1, x)))
        return '"%s"' % s
    if kind == 'n':
        if random.random() < 0.3 and depth < 3:
            return '(%s)%s' % (x.name, init(x, depth + 1))
        return init(x, depth + 1)
    if kind == 'u':
        return union_init()
    return bitval(x, t)

def init(st, depth=0):
    """A braced initializer for struct type st."""
    c = random.randrange(4)
    if c == 0 and depth < 3:
        # Positional, possibly with elided braces for leading scalars.
        items = []
        for kind, nm, t, x in st.m[:random.randint(0, len(st.m))]:
            items.append(member_init(kind, t, x, depth))
        return '{ %s }' % ', '.join(items)
    # Designated, in shuffled order, some designated twice.
    ms = list(st.m)
    random.shuffle(ms)
    items = []
    for kind, nm, t, x in ms[:random.randint(0, len(ms))]:
        if kind == 'n' and random.random() < 0.3:
            inner = random.choice(x.m)
            if inner[0] == 's':
                # A designator into the member, then braces for the whole member:
                # the braces replace it, the earlier value is gone.
                items.append('.%s.%s = %s' % (nm, inner[1], val(inner[2])))
                items.append('.%s = %s' % (nm, init(x, depth + 1)))
                continue
        items.append('.%s = %s' % (nm, member_init(kind, t, x, depth)))
        if kind == 's' and random.random() < 0.25:
            items.append('.%s = %s' % (nm, val(t)))   # designated again: this one wins
    return '{ %s }' % ', '.join(items)

def program():
    types = []
    for k in range(random.randint(2, 5)):
        types.append(T(k, list(types)))
    out = [HDR] + [t.decl() for t in types] + [t.hasher() for t in types]
    fns = []
    for f in range(random.randint(2, 4)):
        body = ['__attribute__((noinline)) static void f%d(int a, int b, int c, double d) {' % f]
        for n in range(random.randint(3, 8)):
            st = random.choice(types)
            k = st.k
            c = random.randrange(6)
            if c < 2:
                body.append('    { %s x = %s; H = 1469598103934665603UL; h%d(&x); printf("o%d.%d %%lu\\n", H); }' % (st.name, init(st), k, f, n))
            elif c == 2:
                # An array of structs: designators and a GNU range.
                lo = random.randint(0, 2)
                hi = lo + random.randint(0, 2)
                body.append('    { %s x[5] = { [%d] = %s, [%d ... %d] = %s, [%d].%s = %s }; H = 1469598103934665603UL; for (int i = 0; i < 5; i++) h%d(&x[i]); printf("o%d.%d %%lu\\n", H); }'
                            % (st.name, random.randrange(5), init(st), lo, hi, init(st), random.randrange(5), st.m[0][1],
                               member_init(st.m[0][0], st.m[0][2], st.m[0][3], 2) if st.m[0][0] not in ('a', 'c', 'n', 'u') else '{0}' if st.m[0][0] != 'b' else bitval(st.m[0][3], st.m[0][2]), k, f, n))
            elif c == 3:
                # Sized by its initializer.
                idx = sorted(random.sample(range(9), random.randint(1, 4)))
                body.append('    { int x[] = { %s }; H = 1469598103934665603UL; hb(x, sizeof x); printf("o%d.%d %%lu %%zu\\n", H, sizeof x); }'
                            % (', '.join('[%d] = %s' % (i, val('int')) for i in idx), f, n))
            elif c == 4:
                # Re-initialized each iteration: the members not named are zero again.
                body.append('    H = 1469598103934665603UL; for (int i = 0; i < 3; i++) { %s x = %s; h%d(&x); a += i; b ^= i; } printf("o%d.%d %%lu\\n", H);'
                            % (st.name, init(st), k, f, n))
            else:
                # An initializer that reads an earlier object.
                body.append('    { %s y = %s; %s x = y; %s z = { 0 }; z = x; H = 1469598103934665603UL; h%d(&x); h%d(&z); printf("o%d.%d %%lu\\n", H); }'
                            % (st.name, init(st), st.name, st.name, k, k, f, n))
        body.append('}')
        fns.append('\n'.join(body))
    main = ['int main(void) {']
    for f in range(len(fns)):
        main.append('    f%d(%d, %d, %d, %s);' % (f, random.randint(-50, 300), random.randint(-50, 300), random.randint(-9, 9), random.choice(['2.5', '-0.75', '1e3'])))
    main.append('    return 0;\n}')
    return '\n'.join(out + fns + main) + '\n'

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
        src = os.path.join(WORK, 'i%d_%d.c' % (SEED, it))
        open(src, 'w').write(program())
        rc, _, err = run([CLANG, '-O0', '-w', '-fsanitize=undefined,address', '-fno-sanitize-recover=all', src, '-o', src + '.ub'])
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
