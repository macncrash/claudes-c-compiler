"""Variadic function fuzzer: random variadic callees (fixed params of many
types, then variadic args of every scalar kind, structs/unions/HFAs of 1-32
bytes, __int128, _Complex, long double, pointers), read with va_arg, va_copy,
a va_list * helper and vprintf. Callee and caller are separate files, built
as ccc+ccc, ccc caller + clang callee, clang caller + ccc callee; each must
print what clang+clang prints.

usage: vafz.py CCC CLANG WORK SEED COUNT   (env OPT, default -O1)
"""
import os, random, subprocess, sys

CCC, CLANG, WORK, SEED, COUNT = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]), int(sys.argv[5])
OPT = os.environ.get('OPT', '-O1')
os.makedirs(WORK, exist_ok=True)

HDR = r'''#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <complex.h>
struct S1 { char a; };
struct S2 { short a; };
struct S3 { char a[3]; };
struct S4 { int a; };
struct S5 { char a; int b; };
struct S8 { long a; };
struct S12 { int a[3]; };
struct S16 { long a, b; };
struct S24 { long a[3]; };
struct S32 { long a[4]; };
struct F1 { float a; };
struct F2 { float a, b; };
struct F3 { float a[3]; };
struct F4 { float a[4]; };
struct D1 { double a; };
struct D2 { double a, b; };
struct D3 { double a[3]; };
struct D4 { double a[4]; };
struct M1 { char c; double d; };
struct M2 { float f; int i; };
struct M3 { short s; char c; long l; };
struct BF { unsigned a : 3; int b : 9; unsigned c : 20; };
union U8 { long l; double d; char c[8]; };
union U12 { int i[3]; float f; };
struct E { int z[0]; };
extern char base_[64];
'''

# kind: (C type, va_arg type, value generator, print format pieces)
def rint(lo, hi):
    return random.randint(lo, hi)

def lit_ld():
    return R(['1.5L', '-2.25L', '1e300L', '3.141592653589793L', '-0.0L', '7.0L'])

SCALARS = {
    'char': ('char', 'int', lambda: '(char)%d' % rint(-128, 127), 'int'),
    'signed char': ('signed char', 'int', lambda: '(signed char)%d' % rint(-128, 127), 'int'),
    'unsigned char': ('unsigned char', 'int', lambda: '(unsigned char)%d' % rint(0, 255), 'int'),
    'short': ('short', 'int', lambda: '(short)%d' % rint(-32768, 32767), 'int'),
    'unsigned short': ('unsigned short', 'int', lambda: '(unsigned short)%d' % rint(0, 65535), 'int'),
    '_Bool': ('_Bool', 'int', lambda: '(_Bool)%d' % rint(0, 1), 'int'),
    'int': ('int', 'int', lambda: '%d' % rint(-2**31, 2**31 - 1), 'int'),
    'unsigned': ('unsigned', 'unsigned', lambda: '%du' % rint(0, 2**32 - 1), 'unsigned'),
    'long': ('long', 'long', lambda: '%dL' % rint(-2**62, 2**62), 'long'),
    'unsigned long': ('unsigned long', 'unsigned long', lambda: '%dUL' % rint(0, 2**64 - 1), 'unsigned long'),
    'long long': ('long long', 'long long', lambda: '%dLL' % rint(-2**62, 2**62), 'long long'),
    'float': ('float', 'double', lambda: '%ff' % (rint(-10**6, 10**6) / 64.0), 'double'),
    'double': ('double', 'double', lambda: '%r' % (rint(-10**9, 10**9) / 1024.0), 'double'),
    'long double': ('long double', 'long double', lambda: R(['1.5L', '-2.25L', '1e300L', '3.141592653589793L', '-0.0L', '7.0L']), 'long double'),
    'void *': ('void *', 'void *', lambda: '(void *)(base_ + %d)' % rint(0, 63), 'void *'),
    'char *': ('char *', 'char *', lambda: '(base_ + %d)' % rint(0, 63), 'void *'),
    '__int128': ('__int128', '__int128', lambda: '((__int128)%dLL << 64 | %dULL)' % (rint(-2**40, 2**40), rint(0, 2**64 - 1)), '__int128'),
    '_Complex double': ('_Complex double', '_Complex double', lambda: '(%r + %r * I)' % (rint(-1000, 1000) / 8.0, rint(-1000, 1000) / 8.0), '_Complex double'),
    '_Complex float': ('_Complex float', '_Complex float', lambda: '((_Complex float)(%r + %r * I))' % (rint(-1000, 1000) / 8.0, rint(-1000, 1000) / 8.0), '_Complex float'),
}

def iv():
    return str(rint(-1000, 1000))

def fv():
    return '%r' % (rint(-4000, 4000) / 16.0)

STRUCTS = {
    'struct S1': ('{%s}' , ['(char)%s'], ['a'], 'int'),
    'struct S2': ('{%s}', ['(short)%s'], ['a'], 'int'),
    'struct S3': ('{{%s, %s, %s}}', ['(char)%s'] * 3, ['a[0]', 'a[1]', 'a[2]'], 'int'),
    'struct S4': ('{%s}', ['%s'], ['a'], 'int'),
    'struct S5': ('{%s, %s}', ['(char)%s', '%s'], ['a', 'b'], 'int'),
    'struct S8': ('{%s}', ['%sL'], ['a'], 'long'),
    'struct S12': ('{{%s, %s, %s}}', ['%s'] * 3, ['a[0]', 'a[1]', 'a[2]'], 'int'),
    'struct S16': ('{%s, %s}', ['%sL'] * 2, ['a', 'b'], 'long'),
    'struct S24': ('{{%s, %s, %s}}', ['%sL'] * 3, ['a[0]', 'a[1]', 'a[2]'], 'long'),
    'struct S32': ('{{%s, %s, %s, %s}}', ['%sL'] * 4, ['a[0]', 'a[1]', 'a[2]', 'a[3]'], 'long'),
    'struct F1': ('{%s}', ['F%s'], ['a'], 'double'),
    'struct F2': ('{%s, %s}', ['F%s'] * 2, ['a', 'b'], 'double'),
    'struct F3': ('{{%s, %s, %s}}', ['F%s'] * 3, ['a[0]', 'a[1]', 'a[2]'], 'double'),
    'struct F4': ('{{%s, %s, %s, %s}}', ['F%s'] * 4, ['a[0]', 'a[1]', 'a[2]', 'a[3]'], 'double'),
    'struct D1': ('{%s}', ['D%s'], ['a'], 'double'),
    'struct D2': ('{%s, %s}', ['D%s'] * 2, ['a', 'b'], 'double'),
    'struct D3': ('{{%s, %s, %s}}', ['D%s'] * 3, ['a[0]', 'a[1]', 'a[2]'], 'double'),
    'struct D4': ('{{%s, %s, %s, %s}}', ['D%s'] * 4, ['a[0]', 'a[1]', 'a[2]', 'a[3]'], 'double'),
    'struct M1': ('{%s, %s}', ['(char)%s', 'D%s'], ['c', 'd'], None),
    'struct M2': ('{%s, %s}', ['F%s', '%s'], ['f', 'i'], None),
    'struct M3': ('{%s, %s, %s}', ['(short)%s', '(char)%s', '%sL'], ['s', 'c', 'l'], 'long'),
    'struct BF': ('{%s, %s, %s}', ['(unsigned)%s & 7', '%s & 255', '(unsigned)%s & 0xfffff'], ['a', 'b', 'c'], 'long'),
    'union U8': ('{%s}', ['%sL'], ['l'], 'long'),
    'union U12': ('{{%s, %s, %s}}', ['%s'] * 3, ['i[0]', 'i[1]', 'i[2]'], 'int'),
}

def R(xs):
    return random.choice(xs)

def struct_value(t):
    fmt, parts, _, _ = STRUCTS[t]
    vals = []
    for p in parts:
        if p.startswith('F') or p.startswith('D'):
            vals.append(p[1:] % fv())
        else:
            vals.append(p % iv())
    return '((%s)%s)' % (t, fmt % tuple(vals))

def print_stmts(t, v):
    """C statements printing a value v of type t (as read back)."""
    if t in STRUCTS:
        _, _, fields, _ = STRUCTS[t]
        out = []
        for f in fields:
            out.append('printf(" %%.9g", (double)%s.%s);' % (v, f))
        return ' '.join(out)
    pt = SCALARS[t][3]
    if pt in ('int', 'unsigned', 'long', 'unsigned long', 'long long'):
        return 'printf(" %%lld", (long long)%s);' % v
    if pt == 'double':
        return 'printf(" %%.17g", (double)%s);' % v
    if pt == 'long double':
        return 'printf(" %%.17Lg", (long double)%s);' % v
    if pt == 'void *':
        return 'printf(" %%ld", (long)((char *)%s - base_));' % v
    if pt == '__int128':
        return 'printf(" %%llx:%%llx", (unsigned long long)((unsigned __int128)%s >> 64), (unsigned long long)%s);' % (v, v)
    return 'printf(" %%.9g,%%.9g", (double)__real__ %s, (double)__imag__ %s);' % (v, v)

ALL = list(SCALARS) + list(STRUCTS)
FIXED = ['int', 'long', 'double', 'float', 'char', '_Complex float', '_Complex double', 'struct F2', 'struct S12', 'struct D2', 'struct F3', 'struct S24', 'long double', 'struct M1', '__int128', 'void *', 'short']

def value_of(t):
    return struct_value(t) if t in STRUCTS else SCALARS[t][2]()

def vatype(t):
    return t if t in STRUCTS else SCALARS[t][1]

def gen_func(idx):
    nfixed = random.choice([1, 1, 2, 3, 5, 8, 9, 10])
    fixed = [R(FIXED) for _ in range(nfixed - 1)]
    nvar = random.randint(0, 14)
    var = [R(ALL) for _ in range(nvar)]
    name = 'vf%d' % idx
    params = ['int n'] + ['%s x%d' % (t, i) for i, t in enumerate(fixed)]
    proto = 'void %s(%s, ...)' % (name, ', '.join(params))
    body = ['%s {' % proto, '  printf("%s");' % name]
    for i, t in enumerate(fixed):
        body.append('  ' + print_stmts(t, 'x%d' % i))
    body.append('  va_list ap;')
    body.append('  va_start(ap, x%d);' % (len(fixed) - 1) if fixed else '  va_start(ap, n);')
    mode = random.randrange(5)
    copy_at = random.randint(0, nvar) if mode == 1 else -1
    helper_at = random.randint(0, nvar) if mode == 2 else -1
    i = 0
    while i < nvar:
        if i == copy_at:
            body.append('  { va_list cp; va_copy(cp, ap);')
            for j in range(i, nvar):
                body.append('    { %s v_ = va_arg(cp, %s); printf(" c"); %s }' % (vatype(var[j]), vatype(var[j]), print_stmts(var[j], 'v_')))
            body.append('    va_end(cp); }')
        if i == helper_at:
            k = random.randint(1, nvar - i)
            hn = 'vh%d_%d' % (idx, i)
            HELPERS.append((hn, var[i:i + k]))
            body.append('  %s(&ap);' % hn)
            i += k
            continue
        t = var[i]
        body.append('  { %s v_ = va_arg(ap, %s); %s }' % (vatype(t), vatype(t), print_stmts(t, 'v_')))
        i += 1
    if mode == 3:
        # rest through vprintf: ints and doubles only, appended by the caller
        body.append('  vprintf(" %d %g %ld %.17g", ap);')
    body.append('  va_end(ap);')
    body.append('  printf("\\n");')
    body.append('}')
    call_args = [str(rint(0, 9))] + [value_of(t) for t in fixed] + [value_of(t) for t in var]
    if mode == 3:
        call_args += [str(rint(-99, 99)), fv(), '%dL' % rint(-10**12, 10**12), fv()]
    if random.random() < 0.35:
        ptype = 'void (*volatile %s)(%s, ...)' % ('fp_', ', '.join(['int'] + fixed))
        return proto, '\n'.join(body), '  { %s = %s; fp_(%s); }' % (ptype, name, ', '.join(call_args))
    return proto, '\n'.join(body), '  %s(%s);' % (name, ', '.join(call_args))

HELPERS = []

def build(dirn):
    del HELPERS[:]
    funcs = [gen_func(i) for i in range(random.randint(4, 8))]
    callee = [HDR]
    for hn, ts in HELPERS:
        callee.append('static void %s(va_list *ap) {' % hn)
        for t in ts:
            callee.append('  { %s v_ = va_arg(*ap, %s); printf(" h"); %s }' % (vatype(t), vatype(t), print_stmts(t, 'v_')))
        callee.append('}')
    for proto, body, _ in funcs:
        callee.append(body)
    # A variadic function reached through a pointer, and one forwarding to vprintf.
    callee.append('int vsum(int n, ...) { va_list ap; va_start(ap, n); long s = 0; for (int i = 0; i < n; i++) s += va_arg(ap, int) * (i + 1); va_end(ap); return (int)s; }')
    caller = [HDR, 'char base_[64];']
    for proto, _, _ in funcs:
        caller.append(proto + ';')
    caller.append('int vsum(int n, ...);')
    caller.append('int main(void) {')
    for _, _, call in funcs:
        caller.append(call)
    caller.append('  int (*fp)(int, ...) = vsum; printf("%%d %%d\\n", fp(3, 1, 2, 3), vsum(%d, %s));' % (5, ', '.join(str(rint(-50, 50)) for _ in range(5))))
    caller.append('  return 0;\n}')
    open(os.path.join(dirn, 'callee.c'), 'w').write('\n'.join(callee) + '\n')
    open(os.path.join(dirn, 'caller.c'), 'w').write('\n'.join(caller) + '\n')

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
        d = os.path.join(WORK, 'a%d_%d' % (SEED, it))
        os.makedirs(d, exist_ok=True)
        build(d)
        objs = {}
        ok = True
        for cname, cc in (('c', CLANG), ('x', CCC)):
            for f in ('callee', 'caller'):
                o = os.path.join(d, '%s.%s.o' % (f, cname))
                rc, _, err = run([cc, '-w', OPT, '-c', os.path.join(d, f + '.c'), '-o', o])
                if rc != 0:
                    if cname == 'c':
                        skipped += 1
                        print('SKIP', d, err[:300])
                        ok = False
                        break
                    bad += 1
                    print('FAIL-BUILD', d, f, err[:300])
                    ok = False
                    break
                objs[(f, cname)] = o
            if not ok:
                break
        if not ok:
            continue
        outs = {}
        for combo in (('c', 'c'), ('x', 'x'), ('x', 'c'), ('c', 'x')):
            exe = os.path.join(d, 'run_%s%s' % combo)
            rc, _, err = run([CLANG, objs[('caller', combo[0])], objs[('callee', combo[1])], '-o', exe])
            rc2, out, _ = run([exe]) if rc == 0 else (1, '', '')
            outs[combo] = out if rc2 == 0 else '<rc %d>' % rc2
        ref = outs[('c', 'c')]
        diffs = [c for c in outs if outs[c] != ref]
        if diffs:
            bad += 1
            with open(os.path.join(d, 'diff.txt'), 'w') as fh:
                for c in diffs:
                    rl, xl = ref.splitlines(), outs[c].splitlines()
                    for k in range(max(len(rl), len(xl))):
                        a = rl[k] if k < len(rl) else '<none>'
                        b = xl[k] if k < len(xl) else '<none>'
                        if a != b:
                            fh.write('caller=%s callee=%s\n  clang %s\n  this  %s\n' % (c[0], c[1], a, b))
            print('DIFF', d, ' '.join('%s%s' % c for c in diffs))
        else:
            for f in os.listdir(d):
                os.remove(os.path.join(d, f))
            os.rmdir(d)
    print('done %d bad %d skipped %d' % (COUNT, bad, skipped))

main()
