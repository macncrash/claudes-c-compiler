"""Address constants in static initializers, ccc against clang.

usage: pfuzz.py CCC CLANG WORKDIR SEED COUNT [PTRS_PER_FILE]

Declares a few objects (arrays of scalars and structs, nested arrays, structs
with array and struct members, string literals), then static pointers whose
initializers are address constants built from them: subscripts, member
access, casts through char *, pointer +/- integer constants. Each pointer's
byte offset from its base object is printed, and the value it points at
where that is in bounds. Every offset stays within its object or one past.
"""
import os
import random
import subprocess
import sys

CCC, CLANG, W = sys.argv[1], sys.argv[2], sys.argv[3]
seed, count = int(sys.argv[4]), int(sys.argv[5])
per = int(sys.argv[6]) if len(sys.argv) > 6 else 40
os.makedirs(W, exist_ok=True)

SCALARS = [('char', 1), ('short', 2), ('int', 4), ('long', 8), ('double', 8), ('unsigned char', 1)]

PRELUDE = r'''#include <stdio.h>
#include <stddef.h>
struct In { char c; short s[3]; int i; };
struct Out { long l; struct In in[2]; char tail[5]; int m[2][3]; };
static char ca[17] = "abcdefghijklmnop";
static short sa[9] = { 1, 2, 3, 4, 5, 6, 7, 8, 9 };
static int ia[4][5] = { { 1, 2, 3, 4, 5 }, { 6, 7, 8, 9, 10 }, { 11, 12, 13, 14, 15 }, { 16, 17, 18, 19, 20 } };
static long la[6] = { 10, 20, 30, 40, 50, 60 };
static double da[5] = { 0.5, 1.5, 2.5, 3.5, 4.5 };
static struct Out so = { 7, { { 'x', { 1, 2, 3 }, 4 }, { 'y', { 5, 6, 7 }, 8 } }, "wxyz", { { 1, 2, 3 }, { 4, 5, 6 } } };
static struct Out sarr[3];
static const char *const str = "0123456789";
'''

# (expression text yielding an lvalue, its C type, size of the element, base object, valid indices)
def lvalues(r):
    c = []
    k = r.randint(0, 16); c.append(('ca[%d]' % k, 'char', 'ca'))
    k = r.randint(0, 8); c.append(('sa[%d]' % k, 'short', 'sa'))
    i, j = r.randint(0, 3), r.randint(0, 4); c.append(('ia[%d][%d]' % (i, j), 'int', 'ia'))
    k = r.randint(0, 5); c.append(('la[%d]' % k, 'long', 'la'))
    k = r.randint(0, 4); c.append(('da[%d]' % k, 'double', 'da'))
    k = r.randint(0, 1); c.append(('so.in[%d].s[%d]' % (k, r.randint(0, 2)), 'short', 'so'))
    c.append(('so.in[%d].i' % r.randint(0, 1), 'int', 'so'))
    c.append(('so.in[%d].c' % r.randint(0, 1), 'char', 'so'))
    c.append(('so.tail[%d]' % r.randint(0, 4), 'char', 'so'))
    c.append(('so.m[%d][%d]' % (r.randint(0, 1), r.randint(0, 2)), 'int', 'so'))
    c.append(('so.l', 'long', 'so'))
    k = r.randint(0, 2); c.append(('sarr[%d].in[%d].i' % (k, r.randint(0, 1)), 'int', 'sarr'))
    c.append(('sarr[%d].m[1][%d]' % (r.randint(0, 2), r.randint(0, 2)), 'int', 'sarr'))
    c.append(('(&so)->in[1].s[1]', 'short', 'so'))
    c.append(('(*ia)[%d]' % r.randint(0, 4), 'int', 'ia'))
    c.append(('ia[%d][0]' % r.randint(0, 3), 'int', 'ia'))
    return c


def expr(r):
    lv, t, base = r.choice(lvalues(r))
    k = r.random()
    if k < 0.3:
        return '&%s' % lv, t, base
    if k < 0.5:
        d = r.randint(-1, 1)
        return '&%s %s %d' % (lv, '+' if d >= 0 else '-', abs(d)), t, base
    if k < 0.7:
        d = r.randint(-2, 2)
        return '(char *)&%s %s %d' % (lv, '+' if d >= 0 else '-', abs(d)), 'char', base
    if k < 0.8:
        return '(%s *)((char *)&%s + 0)' % (t, lv), t, base
    if k < 0.9:
        # Whole subarrays and structs as pointers.
        return r.choice([('ia[%d]' % r.randint(0, 3), 'int', 'ia'), ('so.in', 'struct In', 'so'),
                         ('&so.in[1]', 'struct In', 'so'), ('so.m[1]', 'int', 'so'),
                         ('&sarr[2]', 'struct Out', 'sarr'), ('sarr[1].tail + 2', 'char', 'sarr'),
                         ('&ia[2]', 'int (*)[5]', 'ia'), ('*ia + 7', 'int', 'ia'), ('ca + 16', 'char', 'ca'),
                         ('&*ia', 'int', 'ia'), ('*(ia + %d)' % r.randint(0, 3), 'int', 'ia'),
                         ('&ia[%d][%d] - 3' % (r.randint(1, 3), r.randint(0, 4)), 'int', 'ia'),
                         ('(char *)so.m + %d' % r.randint(0, 23), 'char', 'so'),
                         ('&(sarr + %d)->tail[%d]' % (r.randint(0, 2), r.randint(0, 4)), 'char', 'sarr'),
                         ('ia + %d' % r.randint(0, 4), 'int', 'ia'), ('&so.in[%d] + 1' % r.randint(0, 1), 'int', 'so'),
                         ('&(&sarr[1])->m[%d][%d]' % (r.randint(0, 1), r.randint(0, 2)), 'int', 'sarr'),
                         ('&%d[sa]' % r.randint(0, 8), 'short', 'sa'),
                         ('(void *)&la[%d] + 3' % r.randint(0, 5), 'char', 'la'),
                         ('&((struct In *)&so.in[0])[1].s[2]', 'short', 'so')])
    return '"%s" + %d' % ('literal', r.randint(0, 7)), 'char', None


def program(r, n):
    lines = [PRELUDE]
    body = []
    for i in range(n):
        e, t, base = expr(r)
        decl = 'static const void *p%d = %s;' % (i, e)
        lines.append(decl)
        if base is None:
            body.append('    printf("%d %%s\\n", (const char *)p%d);' % (i, i))
        else:
            body.append('    printf("%d %%ld\\n", (long)((const char *)p%d - (const char *)&%s));' % (i, i, base))
    # Tables of addresses, and a struct holding addresses.
    tab = [expr(r) for _ in range(6)]
    lines.append('static const void *const tab[] = { %s };' % ', '.join(e for e, _, _ in tab))
    for j, (e, t, base) in enumerate(tab):
        if base is None:
            body.append('    printf("t%d %%s\\n", (const char *)tab[%d]);' % (j, j))
        else:
            body.append('    printf("t%d %%ld\\n", (long)((const char *)tab[%d] - (const char *)&%s));' % (j, j, base))
    sv = [expr(r) for _ in range(3)]
    lines.append('static struct { char c; const void *a; short s; const void *b; const void *d; } holder = { 3, %s, 9, %s, %s };'
                 % tuple(e for e, _, _ in sv))
    for j, (e, t, base) in enumerate(sv):
        f = 'abd'[j]
        if base is None:
            body.append('    printf("h%s %%s\\n", (const char *)holder.%s);' % (f, f))
        else:
            body.append('    printf("h%s %%ld\\n", (long)((const char *)holder.%s - (const char *)&%s));' % (f, f, base))
    body.append('    printf("hc %d %d\\n", holder.c, holder.s);')
    lines.append('int main(void)\n{')
    lines += body
    lines.append('    return 0;\n}')
    return '\n'.join(lines) + '\n'


def run(cc, src, exe):
    c = subprocess.run([cc, os.environ.get('OPT', '-O2'), '-w', '-o', exe, src], capture_output=True, text=True)
    if c.returncode != 0:
        return 'COMPILE FAILED: ' + c.stderr[:400]
    p = subprocess.run([exe], capture_output=True, text=True, timeout=20)
    return p.stdout + ('' if p.returncode == 0 else 'EXIT %d' % p.returncode)


def main():
    rnd = random.Random(seed)
    found = 0
    for k in range(count):
        src = os.path.join(W, 'p%d_%d.c' % (seed, k))
        open(src, 'w').write(program(rnd, per))
        ref = run(CLANG, src, os.path.join(W, 'x_k'))
        got = run(CCC, src, os.path.join(W, 'x_c'))
        if ref.startswith('COMPILE FAILED'):
            print('clang rejected', src, ref[:300])
            continue
        if got != ref:
            found += 1
            bad = [(a, b) for a, b in zip(ref.split('\n'), got.split('\n')) if a != b]
            print('MISMATCH', src, bad[:4] if bad else got[:300])
        else:
            os.remove(src)
    print('seed %d: %d of %d files mismatched' % (seed, found, count))


main()
