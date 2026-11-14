"""VLA sizeof/typeof fuzzer: random VLAs (constant and runtime dims), pointers
to them and casts to pointer-to-VLA types; random derived expressions
(subscript, deref, decay, &, comma, statement expressions, ?:) print their
sizeof, pointer step and byte offset from their root; typeof-declared copies
(plain, pointer to, array of, typedef) are memset through their own sizeof and
print their sizes and strides. clang is the oracle; lines where gcc disagrees
are dropped.

usage: vlfz.py CCC CLANG WORK SEED COUNT   (env OPT, default -O1)
"""
import os, random, subprocess, sys

CCC, CLANG, WORK, SEED, COUNT = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]), int(sys.argv[5])
OPT = os.environ.get('OPT', '-O1')
GCC = os.environ.get('GCC', '/opt/homebrew/bin/gcc-15')
os.makedirs(WORK, exist_ok=True)

TYPES = ['char', 'short', 'int', 'long', 'double', 'struct S', 'float', 'long double', 'unsigned char', 'TI']
DIMS = ['n', 'm', 'k', '2', '3', '(n + 1)', '(m * 2)', 'side(k)', '(k - 1)', 'n']

HDR = r'''#include <stdio.h>
#include <string.h>
#include <stddef.h>
struct S { char c[3]; };
typedef int TI;
static int calls;
static int side(int x) { calls++; return x; }
static long double buf[4096];
#define OFF(x, b) ((long)((char *)(x) - (char *)(b)))
'''

class Obj:
    """An expression: kind 'arr' (dims = its remaining dims) or 'ptr'
    (dims = the pointee's dims; [] for a pointer to the element)."""
    def __init__(self, text, kind, dims, elem, root):
        self.text, self.kind, self.dims, self.elem, self.root = text, kind, dims, elem, root

def R(xs):
    return random.choice(xs)

def derive(o, depth=0):
    if depth > 3:
        return o
    k = random.randrange(10)
    if o.kind == 'arr':
        if len(o.dims) > 1 and k < 4:
            return derive(Obj('%s[%s]' % (o.text, R(['0', '1', '(n - 1)', '1'])), 'arr', o.dims[1:], o.elem, o.root), depth + 1)
        if k < 6:
            t = R(['(%s + %s)' % (o.text, R(['0', '1'])), '(0, %s)' % o.text, '({ %s; })' % o.text, '(n ? %s : %s)' % (o.text, o.text)])
            return derive(Obj(t, 'ptr', o.dims[1:], o.elem, o.root), depth + 1)
        if k < 8:
            return derive(Obj('(&%s)' % o.text, 'ptr', o.dims, o.elem, o.root), depth + 1)
        return o
    # ptr
    if k < 3 and o.dims:
        return derive(Obj('(*%s)' % o.text, 'arr', o.dims, o.elem, o.root), depth + 1)
    if k < 5 and o.dims:
        return derive(Obj('%s[%s]' % (o.text, R(['0', '1'])), 'arr', o.dims, o.elem, o.root), depth + 1)
    if k < 7:
        return derive(Obj('(%s + %s)' % (o.text, R(['1', '2', '0'])), 'ptr', o.dims, o.elem, o.root), depth + 1)
    if k < 8:
        return derive(Obj('(1, %s)' % o.text, 'ptr', o.dims, o.elem, o.root), depth + 1)
    return o

def dimstr(dims):
    return ''.join('[%s]' % d for d in dims)

def gen_func(nlines):
    body = []
    roots = []
    nv = [0]
    def fresh(p):
        nv[0] += 1
        return '%s%d' % (p, nv[0])
    for _ in range(random.randint(2, 4)):
        t = R(TYPES)
        dims = [R(DIMS) for _ in range(random.randint(1, 3))]
        name = fresh('a')
        body.append('  %s %s%s; memset(%s, 0, sizeof %s);' % (t, name, dimstr(dims), name, name))
        roots.append(Obj(name, 'arr', dims, t, name))
        if len(dims) > 1 and random.random() < 0.6:
            p = fresh('p')
            body.append('  %s (*%s)%s = %s;' % (t, p, dimstr(dims[1:]), name))
            roots.append(Obj(p, 'ptr', dims[1:], t, name))
        if random.random() < 0.4:
            p = fresh('w')
            body.append('  %s (*%s)%s = &%s;' % (t, p, dimstr(dims), name))
            roots.append(Obj(p, 'ptr', dims, t, name))
    for _ in range(random.randint(1, 2)):
        t = R(TYPES)
        dims = [R(DIMS) for _ in range(random.randint(1, 2))]
        roots.append(Obj('((%s (*)%s)buf)' % (t, dimstr(dims)), 'ptr', dims, t, 'buf'))
    for _ in range(nlines):
        o = derive(R(roots))
        x = o.text
        r = random.randrange(8)
        if r < 3:
            body.append('  printf("%%d %%zu %%ld\\n", __LINE__, sizeof(%s), OFF(%s, %s));' % (x, x, o.root))
            if o.kind == 'ptr':
                body.append('  printf("%%d %%ld\\n", __LINE__, OFF(%s + 1, %s));' % (x, x))
                if o.dims:
                    body.append('  printf("%%d %%zu\\n", __LINE__, sizeof *%s);' % x)
        elif r < 5:
            v = fresh('v')
            if o.kind == 'arr':
                body.append('  __typeof__(%s) %s; memset(%s, 7, sizeof %s); printf("%%d %%zu %%zu %%d\\n", __LINE__, sizeof %s, sizeof(__typeof__(%s)), *(unsigned char *)%s);' % (x, v, v, v, v, x, v))
                if len(o.dims) > 1:
                    body.append('  printf("%%d %%zu %%ld\\n", __LINE__, sizeof %s[0], OFF(&%s[1], %s));' % (v, v, v))
            else:
                body.append('  __typeof__(%s) %s = %s; printf("%%d %%ld %%ld\\n", __LINE__, OFF(%s + 1, %s), OFF(%s, %s));' % (x, v, x, v, v, v, o.root))
                if o.dims:
                    body.append('  printf("%%d %%zu %%ld\\n", __LINE__, sizeof *%s, OFF(&%s[1], %s));' % (v, v, v))
        elif r < 6 and o.kind == 'arr':
            v = fresh('z')
            body.append('  __typeof__(%s) %s[2]; memset(%s, 3, sizeof %s); printf("%%d %%zu %%zu %%ld\\n", __LINE__, sizeof %s, sizeof %s[1], OFF(&%s[1], %s));' % (x, v, v, v, v, v, v, v))
        elif r < 7 and o.kind == 'arr':
            v = fresh('q')
            body.append('  __typeof__(%s) *%s = &%s; printf("%%d %%ld %%zu\\n", __LINE__, OFF(%s + 1, %s), sizeof *%s);' % (x, v, x, v, v, v))
        elif o.kind == 'arr':
            v = fresh('T')
            body.append('  typedef __typeof__(%s) %s; { %s y; memset(y, 1, sizeof y); printf("%%d %%zu %%zu\\n", __LINE__, sizeof(%s), sizeof y); }' % (x, v, v, v))
        else:
            v = fresh('u')
            body.append('  __auto_type %s = %s; printf("%%d %%ld\\n", __LINE__, OFF(%s + 1, %s));' % (v, x, v, v))
    return body

def build(path):
    out = [HDR]
    out.append('__attribute__((noinline)) static void f(int n, int m, int k) {\n')
    out.append('\n'.join(gen_func(40)) + '\n')
    out.append('  printf("calls %d\\n", calls);\n}\n')
    out.append('int main(void) { f(2, 3, 4); f(3, 2, 5); f(4, 4, 2); return 0; }\n')
    open(path, 'w').write(''.join(out))

def run(cmd):
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
        return p.returncode, p.stdout, p.stderr
    except subprocess.TimeoutExpired:
        return -1, '', 'timeout'

def main():
    bad = skipped = 0
    for it in range(COUNT):
        random.seed(SEED * 100003 + it)
        src = os.path.join(WORK, 'v%d_%d.c' % (SEED, it))
        build(src)
        rc, _, err = run([CLANG, '-std=gnu17', '-w', OPT, src, '-o', src + '.c.bin'])
        rc2, _, _ = run([GCC, '-std=gnu17', '-w', OPT, src, '-o', src + '.g.bin'])
        rcc, oc, _ = run([src + '.c.bin']) if rc == 0 else (1, '', '')
        rcg, og, _ = run([src + '.g.bin']) if rc2 == 0 else (1, '', '')
        if rc != 0 or rcc != 0:
            skipped += 1
            print('SKIP', src, err[:200])
            continue
        rc3, _, err3 = run([CCC, '-w', OPT, src, '-o', src + '.x.bin'])
        rcx, ox, _ = run([src + '.x.bin']) if rc3 == 0 else (1, '', '')
        if rc3 != 0:
            bad += 1
            print('FAIL-BUILD', src, err3[:300])
            continue
        cl, xl = oc.splitlines(), ox.splitlines()
        gl = og.splitlines() if rcg == 0 else None
        diffs = []
        for i, a in enumerate(cl):
            x = xl[i] if i < len(xl) else '<missing rc=%d>' % rcx
            if a != x and (gl is None or i >= len(gl) or gl[i] == a):
                diffs.append((a, x))
        if diffs:
            bad += 1
            with open(src + '.diff', 'w') as fh:
                for a, x in diffs:
                    fh.write('clang %s\nccc   %s\n' % (a, x))
            print('DIFF', src, len(diffs))
        for ext in ('.c.bin', '.g.bin', '.x.bin'):
            try: os.remove(src + ext)
            except OSError: pass
        if not diffs:
            os.remove(src)
    print('done %d bad %d skipped %d' % (COUNT, bad, skipped))

main()
