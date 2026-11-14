#!/usr/bin/env python3
"""Static address-constant fuzzer: file-scope and static-local objects
initialized with addresses of array elements, members of nested structs,
casts through char *, pointer arithmetic, string literals plus offsets,
function pointers, pointer differences, integer casts of addresses, held in
scalars, arrays, structs (braced, designated, elided), unions and const
objects.  Each pointer is printed as a byte offset from its base object
plus the value it points at.
usage: adfz.py CCC CLANG WORK SEED COUNT   (env OPT, default -O2)"""
import os
import random
import subprocess
import sys

CCC, CLANG, WORK, SEED, COUNT = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]), int(sys.argv[5])
OPT = os.environ.get('OPT', '-O2')
os.makedirs(WORK, exist_ok=True)

HDR = r'''#include <stdio.h>
struct In { char c; short s; int a[3]; };
struct S { int x; struct In in[2]; long l; char name[6]; };
union UU { int *p; long l; };
int A[10] = {10, 11, 12, 13, 14, 15, 16, 17, 18, 19};
int M[3][4] = {{20, 21, 22, 23}, {24, 25, 26, 27}, {28, 29, 30, 31}};
struct S SA[3] = {
  {1, {{'a', 2, {3, 4, 5}}, {'b', 6, {7, 8, 9}}}, 100, "abcde"},
  {11, {{'c', 12, {13, 14, 15}}, {'d', 16, {17, 18, 19}}}, 200, "fghij"},
  {21, {{'e', 22, {23, 24, 25}}, {'f', 26, {27, 28, 29}}}, 300, "klmno"},
};
struct S S1 = {41, {{'g', 42, {43, 44, 45}}, {'h', 46, {47, 48, 49}}}, 400, "pqrst"};
char BUF[16] = "0123456789ABCDE";
long L = 0x0102030405060708L;
static int f1(int v) { return v + 1; }
static int f2(int v) { return v * 2; }
int f3(int v) { return v - 3; }
static unsigned long H = 1469598103934665603UL;
static void h(long v) { H = (H ^ (unsigned long)v) * 1099511628211UL; }
#define OFF(p, b) ((long)((const char *)(p) - (const char *)(b)))
'''


def ri(r, n):
    return r.randint(0, n - 1)


def int_ptr(r):
    """(expr, base) for an int * address constant pointing at an int"""
    i, j, k = ri(r, 3), ri(r, 2), ri(r, 3)
    a = ri(r, 10)
    m0, m1 = ri(r, 3), ri(r, 4)
    lin = ri(r, 12)
    c = r.randint(0, 9)
    choices = [
        ('&A[%d]' % a, 'A'),
        ('A + %d' % a, 'A'),
        ('&A[%d] + %d' % (c // 2, a - c // 2) if a >= c // 2 else '&A[%d]' % a, 'A'),
        ('&A[%d] - %d' % (a + (9 - a) // 2, (9 - a) // 2), 'A'),
        ('&M[%d][%d]' % (m0, m1), 'M'),
        ('M[%d] + %d' % (m0, m1), 'M'),
        ('*M + %d' % lin, 'M'),
        ('&M[0][0] + %d' % lin, 'M'),
        ('(int *)(M + %d) + %d' % (m0, m1), 'M'),
        ('*(M + %d) + %d' % (m0, m1), 'M'),
        ('&SA[%d].in[%d].a[%d]' % (i, j, k), 'SA'),
        ('SA[%d].in[%d].a + %d' % (i, j, k), 'SA'),
        ('&SA[%d].x' % i, 'SA'),
        ('&(SA + %d)->in[%d].a[%d]' % (i, j, k), 'SA'),
        ('&S1.in[%d].a[%d]' % (j, k), '&S1'),
        ('&S1.x', '&S1'),
        ('(int *)&S1', '&S1'),
        ('(int *)((char *)A + %d)' % (4 * a), 'A'),
        ('(int *)((char *)&M[%d] + %d)' % (m0, 4 * m1), 'M'),
        ('&(A + %d)[%d]' % (a // 2, a - a // 2), 'A'),
        ('&*(A + %d)' % a, 'A'),
        ('(int *)&M[%d]' % m0, 'M'),
        ('&%d[A]' % a, 'A'),
        ('(int *)((long *)A + %d)' % (a // 2), 'A'),
        ('&((struct In *)SA[%d].in)[%d].a[%d]' % (i, j, k), 'SA'),
        ('(int *)(void *)&SA[%d].in[%d].a[%d]' % (i, j, k), 'SA'),
    ]
    return r.choice(choices)


def char_ptr(r):
    i, j = ri(r, 3), ri(r, 6)
    b = ri(r, 16)
    choices = [
        ('BUF + %d' % b, 'BUF'),
        ('&BUF[%d]' % b, 'BUF'),
        ('(char *)&L + %d' % ri(r, 8), '&L'),
        ('S1.name + %d' % j, '&S1'),
        ('&SA[%d].name[%d]' % (i, j), 'SA'),
        ('(char *)&SA[%d] + %d' % (i, ri(r, 8)), 'SA'),
        ('&SA[%d].in[%d].c' % (i, ri(r, 2)), 'SA'),
        ('(char *)A + %d' % ri(r, 40), 'A'),
        ('(char *)&M[%d][%d] + %d' % (ri(r, 3), ri(r, 4), ri(r, 4)), 'M'),
        ('(char *)(SA + %d) + %d' % (i, ri(r, 40)), 'SA'),
        ('BUF + %d - %d' % (b, b // 2), 'BUF'),
        ('(char *)(unsigned char *)BUF + %d' % b, 'BUF'),
    ]
    return r.choice(choices)


STR_LITS = ['"hello"', '"xyzzy12"', '"a\\tb"', '"" "ab" "cd"', '"q"']


def lit_ptr(r):
    s = r.choice(STR_LITS)
    n = len(eval(s.replace('" "', '')))
    o = ri(r, n + 1)
    return r.choice(['%s + %d' % (s, o), '&%s[%d]' % (s, o), '&(%s)[%d]' % (s, o)])


def short_ptr(r):
    i, j = ri(r, 3), ri(r, 2)
    return r.choice([('&SA[%d].in[%d].s' % (i, j), 'SA'), ('&S1.in[%d].s' % j, '&S1'),
                     ('(short *)A + %d' % ri(r, 20), 'A'), ('(short *)&L + %d' % ri(r, 4), '&L')])


def long_ptr(r):
    i = ri(r, 3)
    return r.choice([('&SA[%d].l' % i, 'SA'), ('&S1.l', '&S1'), ('&L', '&L'), ('(long *)A + %d' % ri(r, 5), 'A'),
                     ('(long *)M + %d' % ri(r, 6), 'M')])


def struct_ptr(r):
    i = ri(r, 3)
    return r.choice([('&SA[%d]' % i, 'SA', 'struct S *'), ('SA + %d' % i, 'SA', 'struct S *'), ('&S1', '&S1', 'struct S *'),
                     ('&SA[%d].in[%d]' % (i, ri(r, 2)), 'SA', 'struct In *'), ('SA[%d].in + %d' % (i, ri(r, 2)), 'SA', 'struct In *'),
                     ('S1.in + 1', '&S1', 'struct In *'), ('&SA[%d].in[1] - 1' % i, 'SA', 'struct In *')])


def row_ptr(r):
    return r.choice(['&M[%d]' % ri(r, 3), 'M + %d' % ri(r, 3), '&M[%d] + %d' % (ri(r, 2), 1), '(int (*)[4])M[0] + %d' % ri(r, 3)])


FNS = ['f1', 'f2', 'f3', '&f1', '&f3', '*f2']


class Gen:
    def __init__(self, r):
        self.r = r
        self.decls = []
        self.checks = []
        self.n = 0

    def name(self):
        self.n += 1
        return 'g%d' % self.n

    def leaf(self):
        """(ctype, init, check(lvalue) -> list of C statements)"""
        r = self.r
        kind = r.choice(['int', 'int', 'char', 'lit', 'short', 'long', 'struct', 'row', 'fn', 'diff', 'iaddr', 'null', 'pp'])
        if kind == 'int':
            e, b = int_ptr(r)
            return 'int *', e, lambda lv, b=b: ['h(OFF(%s, %s)); h(*%s);' % (lv, b, lv)]
        if kind == 'char':
            e, b = char_ptr(r)
            return 'char *', e, lambda lv, b=b: ['h(OFF(%s, %s)); h(*%s);' % (lv, b, lv)]
        if kind == 'lit':
            return 'const char *', lit_ptr(r), lambda lv: ['h(*%s); for (const char *q = %s; *q; q++) h(*q);' % (lv, lv)]
        if kind == 'short':
            e, b = short_ptr(r)
            return 'short *', e, lambda lv, b=b: ['h(OFF(%s, %s)); h(*%s);' % (lv, b, lv)]
        if kind == 'long':
            e, b = long_ptr(r)
            return 'long *', e, lambda lv, b=b: ['h(OFF(%s, %s)); h(*%s);' % (lv, b, lv)]
        if kind == 'struct':
            e, b, t = struct_ptr(r)
            if t == 'struct S *':
                return t, e, lambda lv, b=b: ['h(OFF(%s, %s)); h(%s->x); h(%s->in[1].a[2]);' % (lv, b, lv, lv)]
            return t, e, lambda lv, b=b: ['h(OFF(%s, %s)); h(%s->c); h(%s->a[1]);' % (lv, b, lv, lv)]
        if kind == 'row':
            return 'int (*)[4]', row_ptr(r), lambda lv: ['h(OFF(%s, M)); h((*%s)[3]);' % (lv, lv)]
        if kind == 'fn':
            f = r.choice(FNS)
            return 'int (*)(int)', f, lambda lv: ['h(%s(7));' % lv]
        if kind == 'diff':
            e = r.choice(['&A[%d] - &A[%d]' % (ri(r, 10), ri(r, 10)), '&M[2][1] - &M[0][3]', '(char *)&SA[2] - (char *)&SA[0].in[1]',
                          '&SA[2] - SA', 'M[2] - M[0]', '(long)sizeof(struct S) * 2'])
            return 'long', e, lambda lv: ['h(%s);' % lv]
        if kind == 'iaddr':
            e, b = int_ptr(r)
            t = r.choice(['long', 'unsigned long', '__INTPTR_TYPE__'])
            return t, '(%s)(%s)' % (t, e), lambda lv, b=b: ['h(OFF((int *)%s, %s)); h(*(int *)%s);' % (lv, b, lv)]
        if kind == 'null':
            return r.choice(['int *', 'char *', 'void *']), r.choice(['0', '(void *)0', '(int *)0 + 0' if False else '0']), \
                lambda lv: ['h(%s == 0);' % lv]
        # pointer to a pointer global defined earlier
        e, b = int_ptr(r)
        nm = self.name()
        self.decls.append('int *%s = %s;' % (nm, e))
        return 'int **', '&' + nm, lambda lv, b=b: ['h(OFF(*%s, %s)); h(**%s);' % (lv, b, lv)]

    def declare(self, ctype, name, suffix=''):
        if '(*)' in ctype:
            return ctype.replace('(*)', '(*%s%s)' % (name, suffix), 1)
        return '%s%s%s' % (ctype, name, suffix) if ctype.endswith('*') else '%s %s%s' % (ctype, name, suffix)

    def obj(self, storage):
        r = self.r
        nm = self.name()
        shape = r.choice(['scalar', 'array', 'struct', 'struct', 'nested', 'union', 'array2'])
        q = r.choice(['', '', 'const '])
        if shape == 'scalar':
            t, e, chk = self.leaf()
            dq = self.declare(t, nm) if not q else self.declare(t, ' const ' + nm) if t.endswith('*') or '(*)' in t else q + self.declare(t, nm)
            if '(*)' in t and q:
                dq = self.declare(t, nm)
            return '%s%s = %s;' % (storage, dq, e), chk(nm)
        if shape == 'array':
            t, e, chk = self.leaf()
            n = r.randint(1, 5)
            inits, checks = [], []
            mode = r.choice(['plain', 'desig', 'unsized'])
            for x in range(n):
                tt, ee, cc = self.leaf() if r.random() < 0.5 else (t, e, chk)
                if tt != t:
                    tt, ee, cc = t, e, chk
                if mode == 'desig':
                    inits.append('[%d] = %s' % (x, ee))
                else:
                    inits.append(ee)
                checks += cc('%s[%d]' % (nm, x))
            if mode == 'desig':
                r.shuffle(inits)
            dim = '' if mode == 'unsized' else str(n + r.randint(0, 2))
            for x in range(n, int(dim or n)):
                checks.append('h(%s[%d] == 0);' % (nm, x) if t not in ('long', 'unsigned long', '__INTPTR_TYPE__') else 'h(%s[%d]);' % (nm, x))
            checks.append('h(sizeof %s);' % nm)
            return '%s%s = {%s};' % (storage, self.declare(t, nm, '[%s]' % dim), ', '.join(inits)), checks
        if shape in ('struct', 'nested', 'array2'):
            nf = r.randint(1, 4)
            fields = [self.leaf() for _ in range(nf)]
            sn = 'T' + nm
            body = ' '.join('%s;' % self.declare(t, ' f%d' % x if t.endswith('*') else 'f%d' % x) for x, (t, _, _) in enumerate(fields))
            extra = r.choice(['', 'char pad;', 'short s;', 'int bits : 3;'])
            extra_first = bool(extra) and r.random() < 0.5
            self.decls.append('struct %s { %s %s };' % ((sn, extra, body) if extra_first else (sn, body, extra)))
            def sinit(fl):
                mode = r.choice(['plain', 'desig', 'desig'])
                if mode == 'plain':
                    if extra_first:
                        return '{%s}' % ', '.join(['0'] + [e for _, e, _ in fl])
                    return '{%s}' % ', '.join(e for _, e, _ in fl)
                items = ['.f%d = %s' % (x, e) for x, (_, e, _) in enumerate(fl)]
                r.shuffle(items)
                return '{%s}' % ', '.join(items)
            if shape == 'struct':
                checks = []
                for x, (_, _, c) in enumerate(fields):
                    checks += c('%s.f%d' % (nm, x))
                return '%s%sstruct %s %s = %s;' % (storage, q, sn, nm, sinit(fields)), checks
            if shape == 'array2':
                n = r.randint(1, 3)
                checks, elems = [], []
                elide = r.random() < 0.4 and all('{' not in e for _, e, _ in fields) and not extra
                for x in range(n):
                    fl = [(t, e, c) for (t, e, c) in fields]
                    elems.append(', '.join(e for _, e, _ in fl) if elide else sinit(fl))
                    for y, (_, _, c) in enumerate(fl):
                        checks += c('%s[%d].f%d' % (nm, x, y))
                return '%s%sstruct %s %s[] = {%s};' % (storage, q, sn, nm, ', '.join(elems)), checks + ['h(sizeof %s);' % nm]
            # nested: struct holding the struct and an array of pointers
            on = 'O' + nm
            t2, e2, c2 = self.leaf()
            self.decls.append('struct %s { long tag; struct %s in; %s; };' % (on, sn, self.declare(t2, ' arr[2]' if t2.endswith('*') else 'arr[2]')))
            checks = ['h(%s.tag);' % nm]
            for x, (_, _, c) in enumerate(fields):
                checks += c('%s.in.f%d' % (nm, x))
            checks += c2('%s.arr[0]' % nm) + c2('%s.arr[1]' % nm)
            init = r.choice(['{%d, %s, {%s, %s}}' % (r.randint(0, 9), sinit(fields), e2, e2),
                             '{.arr = {%s, [1] = %s}, .in = %s, .tag = %d}' % (e2, e2, sinit(fields), r.randint(0, 9))])
            return '%sstruct %s %s = %s;' % (storage, on, nm, init), checks
        # union whose first member is a pointer
        e, b = int_ptr(r)
        return '%sunion UU %s = {%s};' % (storage, nm, r.choice([e, '.p = ' + e])), ['h(OFF(%s.p, %s)); h(*%s.p);' % (nm, b, nm)]


def gen_file(r):
    g = Gen(r)
    globs, gchecks = [], []
    for _ in range(r.randint(3, 10)):
        d, c = g.obj(r.choice(['', 'static ']))
        globs.append(d)
        gchecks += c
    locs, lchecks = [], []
    for _ in range(r.randint(0, 5)):
        d, c = g.obj('static ')
        locs.append(d)
        lchecks += c
    out = [HDR] + g.decls + globs
    out.append('int main(void) { setvbuf(stdout, 0, _IONBF, 0);')
    out += ['  ' + d for d in locs]
    for c in gchecks + lchecks:
        out.append('  ' + c + ' printf("%d %lx\\n", __LINE__, H);')
    out.append('  printf("%lx\\n", H);')
    out.append('  return 0;')
    out.append('}')
    return '\n'.join(out) + '\n'


def run(cmd, timeout=60):
    try:
        return subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return None


bad = skipped = 0
for t in range(COUNT):
    r = random.Random(SEED * 100003 + t)
    f = os.path.join(WORK, 'a%d_%d.c' % (SEED, t))
    with open(f, 'w') as fh:
        fh.write(gen_file(r))
    ub = run([CLANG, '-O0', '-w', '-Werror=incompatible-pointer-types', '-Werror=int-conversion', '-fsanitize=undefined,address',
              '-fno-sanitize-recover=all', f, '-o', f + '.ub'])
    if ub is None or ub.returncode != 0:
        skipped += 1
        print('clang-fail', f, (ub.stderr[:300] if ub else 'timeout'))
        continue
    u = run([f + '.ub'])
    os.remove(f + '.ub')
    if u is None or u.returncode != 0:
        skipped += 1
        print('skip', f, (u.stderr[:200] if u else 'timeout'))
        continue
    cc = run([CCC, OPT, '-w', f, '-o', f + '.ccc'])
    if cc is None or cc.returncode != 0:
        print('ccc-fail', f, (cc.stderr[:300] if cc else 'timeout'))
        bad += 1
        continue
    b = run([f + '.ccc'])
    if b is None or b.returncode != 0 or u.stdout != b.stdout:
        print('MISMATCH', f)
        bad += 1
    else:
        os.remove(f + '.ccc')
        os.remove(f)
print('done', COUNT, 'bad', bad, 'skipped', skipped)
