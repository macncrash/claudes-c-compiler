"""Struct copy fuzzer: random struct types of odd sizes and alignments (char,
short, int, long, float, double members, odd-length char and int arrays,
nested structs, bit-fields; plain, packed or over-aligned) held in globals,
locals, global and local arrays and heap memory, and copied every way:
assignment (also through pointers that may point at the same object),
chained assignment, a conditional's struct value, compound literals,
returned structs (noinline and inlinable functions), member-struct copies,
array wrappers, and constant-size memcpy, memmove (overlapping, within one
object) and memset at offsets inside objects, which compilers expand inline.
After each block every object is hashed member by member (never padding,
which assignment need not copy). Programs UBSan or ASan flag are skipped;
lines where gcc disagrees with clang are dropped.

usage: cpfz.py CCC CLANG WORK SEED COUNT   (env OPT, default -O2)
"""
import os, random, resource, subprocess, sys, tempfile

CCC, CLANG, WORK, SEED, COUNT = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]), int(sys.argv[5])
OPT = os.environ.get('OPT', '-O2')
GCC = os.environ.get('GCC', '/opt/homebrew/bin/gcc-15')
os.makedirs(WORK, exist_ok=True)

HDR = r'''#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned long UL;
static UL H;
static void hb(const void *p, unsigned n) { const unsigned char *c = p; while (n--) H = (H ^ *c++) * 1099511628211UL; }
static void hv(UL v) { H = (H ^ v) * 1099511628211UL; }
static void fill(void *p, unsigned n, unsigned seed) { unsigned char *c = p; for (unsigned i = 0; i < n; i++) c[i] = (unsigned char)(seed * 131 + i * 29 + (i >> 3)); }
static volatile int VZ = 0;
'''

SCALARS = ['char', 'unsigned char', 'short', 'int', 'unsigned', 'long', 'float', 'double']

class Struct:
    def __init__(self, k, structs):
        self.name = 'struct T%d' % k
        self.k = k
        self.members = []  # (kind, name, ctype, extra)
        a = random.randrange(6)
        self.attr = {0: ' __attribute__((packed))', 1: ' __attribute__((aligned(16)))', 2: ' __attribute__((aligned(32)))'}.get(a, '')
        # A packed struct gets no struct members: one would sit misaligned, and
        # the hash takes its address.
        if a == 0:
            structs = []
        for m in range(random.randint(1, 7)):
            c = random.randrange(10)
            nm = 'm%d' % m
            if c < 5:
                self.members.append(('s', nm, random.choice(SCALARS), None))
            elif c < 7:
                t = random.choice(['char', 'unsigned char', 'short', 'int', 'double'])
                self.members.append(('a', nm, t, random.choice([1, 2, 3, 5, 7, 9, 13, 16, 31])))
            elif c < 8 and structs:
                inner = random.choice(structs)
                self.members.append(('n', nm, inner.name, inner))
            else:
                self.members.append(('b', nm, random.choice(['unsigned', 'int']), random.randint(1, 17)))

    def decl(self):
        out = ['%s {' % self.name]
        for kind, nm, t, x in self.members:
            if kind in ('s', 'n'):
                out.append('    %s %s;' % (t, nm))
            elif kind == 'a':
                out.append('    %s %s[%d];' % (t, nm, x))
            else:
                out.append('    %s %s : %d;' % (t, nm, x))
        out.append('}%s;' % self.attr)
        return '\n'.join(out)

    def hasher(self):
        out = ['static void h%d(const %s *p) {' % (self.k, self.name)]
        for kind, nm, t, x in self.members:
            if kind == 's':
                out.append('    { %s v = p->%s; hb(&v, sizeof v); }' % (t, nm))
            elif kind == 'a':
                out.append('    for (int i = 0; i < %d; i++) { %s v = p->%s[i]; hb(&v, sizeof v); }' % (x, t, nm))
            elif kind == 'n':
                out.append('    h%d(&p->%s);' % (x.k, nm))
            else:
                out.append('    hv((UL)(long)p->%s);' % nm)
        out.append('}')
        return '\n'.join(out)

    def literal(self):
        """A compound literal with some members designated."""
        parts = []
        for kind, nm, t, x in self.members:
            if random.random() < 0.5:
                continue
            if kind == 's':
                parts.append('.%s = (%s)%d' % (nm, t, random.randint(-99, 99)))
            elif kind == 'a':
                parts.append('.%s = {%s}' % (nm, ', '.join(str(random.randint(0, 99)) for _ in range(random.randint(1, x)))))
            elif kind == 'b':
                parts.append('.%s = %d' % (nm, random.randint(0, 1)))
        return '(%s){%s}' % (self.name, ', '.join(parts))

def program():
    structs = []
    for k in range(random.randint(2, 5)):
        structs.append(Struct(k, list(structs)))
    out = [HDR] + [s.decl() for s in structs] + [s.hasher() for s in structs]
    # Objects: (expr, struct) for each storage kind; arrays of 4.
    objs = []
    body = []
    for s in structs:
        k = s.k
        out.append('static %s g%d, ga%d[4];' % (s.name, k, k))
        out.append('__attribute__((noinline)) static %s mk%d(unsigned seed) { %s r; fill(&r, sizeof r, seed); return r; }' % (s.name, k, s.name))
        out.append('static inline %s id%d(%s v) { return v; }' % (s.name, k, s.name))
        out.append('struct W%d { %s a[3]; };' % (k, s.name))
        out.append('__attribute__((noinline)) static void cp%d(%s *d, const %s *q) { *d = *q; }' % (k, s.name, s.name))
        # aligned_alloc: malloc's 16-byte alignment is too little for aligned(32).
        body.append('    %s l%d, la%d[4]; %s *hp%d = aligned_alloc(64, (3 * sizeof(%s) + 63) / 64 * 64);' % (s.name, k, k, s.name, k, s.name))
        body.append('    struct W%d w%d, wb%d;' % (k, k, k))
        body.append('    fill(&g%d, sizeof g%d, %d); fill(ga%d, sizeof ga%d, %d); fill(&l%d, sizeof l%d, %d); fill(la%d, sizeof la%d, %d); fill(hp%d, 3 * sizeof(%s), %d);'
                    % (k, k, k, k, k, k + 7, k, k, k + 3, k, k, k + 11, k, s.name, k + 5))
        body.append('    fill(&w%d, sizeof w%d, %d); fill(&wb%d, sizeof wb%d, %d);' % (k, k, k + 13, k, k, k + 17))
        names = ['g%d' % k, 'l%d' % k, 'ga%d[%%d]' % k, 'la%d[%%d]' % k, 'hp%d[%%d]' % k, 'w%d.a[%%d]' % k]
        objs.append((s, names))

    def pick(names):
        n = random.choice(names)
        if '%d' in n:
            n = n % random.randrange(4 if 'hp' not in n and 'w' not in n else 3)
        return n

    def hash_all():
        lines = ['    H = 1469598103934665603UL;']
        for s, _ in objs:
            k = s.k
            lines.append('    h%d(&g%d); h%d(&l%d); for (int i = 0; i < 4; i++) { h%d(&ga%d[i]); h%d(&la%d[i]); } for (int i = 0; i < 3; i++) { h%d(&hp%d[i]); h%d(&w%d.a[i]); h%d(&wb%d.a[i]); }'
                         % (k, k, k, k, k, k, k, k, k, k, k, k, k, k))
        return lines

    for blk in range(random.randint(3, 6)):
        for _ in range(random.randint(3, 10)):
            s, names = random.choice(objs)
            k = s.k
            d, a, b = pick(names), pick(names), pick(names)
            c = random.randrange(14)
            if c < 3:
                body.append('    %s = %s;' % (d, a))
            elif c == 3:
                body.append('    %s = %s = %s;' % (d, a, b))
            elif c == 4:
                body.append('    %s = (VZ + %d) & 1 ? %s : %s;' % (d, random.randrange(2), a, b))
            elif c == 5:
                body.append('    %s = %s;' % (d, s.literal()))
            elif c == 6:
                body.append('    %s = mk%d(%d);' % (d, k, random.randrange(99)))
            elif c == 7:
                body.append('    %s = id%d(%s);' % (d, k, a))
            elif c == 8:
                body.append('    cp%d(&%s, &%s);' % (k, d, a))
            elif c == 9:
                body.append('    %s = %s;' % (random.choice(['w%d' % k, 'wb%d' % k]), random.choice(['w%d' % k, 'wb%d' % k])))
            elif c == 10:
                nested = [(nm, x) for kind, nm, t, x in s.members if kind == 'n']
                if nested:
                    nm, x = random.choice(nested)
                    body.append('    %s.%s = %s.%s;' % (d, nm, a, nm))
                else:
                    body.append('    %s = %s;' % (d, a))
            elif c == 11:
                # Constant-size memcpy between objects, at an offset inside them.
                lim = random.randint(1, 48)
                # memcpy between two objects; one object onto itself may overlap.
                fn = 'memmove' if d == a else 'memcpy'
                body.append('    { unsigned o = %d %% sizeof(%s), p = %d %% sizeof(%s), n = sizeof(%s) - (o > p ? o : p); if (n > %d) n = %d; %s((char *)&%s + o, (char *)&%s + p, %d %% (n + 1)); }'
                            % (random.randrange(64), s.name, random.randrange(64), s.name, s.name, lim, lim, fn, d, a, random.randint(1, 48)))
            elif c == 12:
                off = random.randrange(40)
                body.append('    memset((char *)&%s + %d %% sizeof(%s), %d, sizeof(%s) - %d %% sizeof(%s));'
                            % (d, off, s.name, random.randrange(256), s.name, off, s.name))
            else:
                # Overlapping memmove within one object, constant size.
                body.append('    { unsigned n = sizeof(%s); unsigned o = %d %% n, p = %d %% n, m = n - (o > p ? o : p); memmove((char *)&%s + o, (char *)&%s + p, %d %% (m + 1)); }'
                            % (s.name, random.randrange(32), random.randrange(32), d, d, random.randint(1, 64)))
        body += hash_all()
        body.append('    printf("block %d %%lu\\n", H);' % blk)
    out.append('int main(void) {')
    out += body
    out += ['    free(hp%d);' % s.k for s in structs]
    out.append('    return 0;\n}')
    return '\n'.join(out) + '\n'

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
        src = os.path.join(WORK, 'c%d_%d.c' % (SEED, it))
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
