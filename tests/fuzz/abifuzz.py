"""Calling-convention fuzzer: random signatures, caller and callee split
across two files and compiled by every pairing of ccc and clang.

usage: abifuzz.py CCC CLANG WORKDIR SEED COUNT [FUNCS_PER_FILE]

Parameters and returns mix integers of every width, float, double, small
and large structs, homogeneous float aggregates, structs with arrays, and
variadic tails. The callee prints every argument it received and returns a
value built from them; the caller prints the returned value. Reference is
clang+clang; each of ccc+ccc, ccc caller/clang callee and clang caller/ccc
callee must print the same.
"""
import os
import random
import subprocess
import sys

CCC, CLANG, W = sys.argv[1], sys.argv[2], sys.argv[3]
seed, count = int(sys.argv[4]), int(sys.argv[5])
per = int(sys.argv[6]) if len(sys.argv) > 6 else 10
os.makedirs(W, exist_ok=True)

INTS = [('signed char', 'd'), ('unsigned char', 'u'), ('short', 'd'), ('unsigned short', 'u'),
        ('int', 'd'), ('unsigned', 'u'), ('long', 'ld'), ('unsigned long', 'lu'), ('_Bool', 'd')]
FLOATS = [('float', 'a'), ('double', 'a'), ('long double', 'La')]


class Ctx:
    def __init__(self, r):
        self.r = r
        self.structs = []    # (name, [(member type, member name, fmt)], C decl)

    def scalar(self):
        r = self.r
        if r.random() < 0.6:
            return r.choice(INTS)
        return r.choice(FLOATS)

    def new_struct(self):
        r = self.r
        name = 'S%d' % len(self.structs)
        kind = r.random()
        members = []
        if kind < 0.3:
            # Homogeneous float aggregate of 1-4 members, or one array of them.
            t = r.choice(['float', 'double'])
            n = r.randint(1, 4)
            if r.random() < 0.3:
                members = [('%s[%d]' % (t, n), 'h', 'a')]
            else:
                members = [(t, 'm%d' % i, 'a') for i in range(n)]
        else:
            for i in range(r.randint(1, 6)):
                t, f = self.scalar()
                if t == '_Bool':
                    t, f = 'char', 'd'
                if r.random() < 0.15:
                    members.append(('%s[%d]' % (t, r.randint(1, 5)), 'm%d' % i, f))
                else:
                    members.append((t, 'm%d' % i, f))
        decl = 'struct %s { %s };' % (name, ' '.join(
            ('%s %s%s;' % (t.split('[')[0], m, '[' + t.split('[')[1] if '[' in t else ''))
            for t, m, f in members))
        attr = r.random()
        if attr < 0.08:
            decl = decl.replace('struct %s {' % name, 'struct __attribute__((packed)) %s {' % name)
        self.structs.append((name, members, decl))
        return name

    def type(self):
        r = self.r
        if r.random() < 0.35:
            if self.structs and r.random() < 0.5:
                return ('struct', r.choice(self.structs)[0])
            return ('struct', self.new_struct())
        t, f = self.scalar()
        return ('scalar', t, f)


def ctext(t):
    return t[1] if t[0] == 'scalar' else 'struct %s' % t[1]


def members_of(ctx, name):
    return next(s for s in ctx.structs if s[0] == name)[1]


def leaves(ctx, t, expr):
    """(C expression, printf format) for every scalar leaf of a value."""
    if t[0] == 'scalar':
        return [(expr, t[2], t[1])]
    out = []
    for mt, m, f in members_of(ctx, t[1]):
        base = mt.split('[')[0]
        if '[' in mt:
            n = int(mt.split('[')[1].rstrip(']'))
            out += [('%s.%s[%d]' % (expr, m, i), f, base) for i in range(n)]
        else:
            out.append(('%s.%s' % (expr, m), f, base))
    return out


def lit(r, ctype):
    if ctype in ('float', 'double', 'long double'):
        v = r.choice(['1.5', '-2.25', '3e10', '0.1', '-7', '65504.5', '1e-3'])
        return v + ('f' if ctype == 'float' else 'L' if ctype == 'long double' else '')
    if ctype == '_Bool':
        return r.choice(['0', '1'])
    v = r.choice([0, 1, -1, 7, 127, -128, 255, 300, 65535, -32768, 0x7fffffff, -5, 123456789])
    if ctype.startswith('unsigned') and v < 0:
        return '(%s)%d' % (ctype, v)
    if ctype == 'long':
        v = r.choice([v, 0x123456789ab, -0x7000000000000001])
        return '(long)%dLL' % v
    if ctype == 'unsigned long':
        return r.choice(['(unsigned long)%dLL' % v, '0x123456789abULL', '0xfedcba9876543210ULL'])
    return '(%s)%d' % (ctype, v)


def value(ctx, t):
    r = ctx.r
    if t[0] == 'scalar':
        return lit(r, t[1])
    parts = []
    for mt, m, f in members_of(ctx, t[1]):
        base = mt.split('[')[0]
        if '[' in mt:
            n = int(mt.split('[')[1].rstrip(']'))
            parts.append('{ %s }' % ', '.join(lit(r, base) for _ in range(n)))
        else:
            parts.append(lit(r, base))
    return '(struct %s){ %s }' % (t[1], ', '.join(parts))


def fmt_cast(f, base):
    if f == 'a':
        return '%a', '(double)'
    if f == 'La':
        return '%La', '(long double)'
    if f in ('ld', 'lu'):
        return '%' + f, '(long)' if f == 'ld' else '(unsigned long)'
    return '%' + ('d' if f == 'd' else 'u'), '(int)' if f == 'd' else '(unsigned)'


def program(r, n):
    ctx = Ctx(r)
    funcs = []
    for i in range(n):
        ret = r.choice([('void',), ctx.type(), ctx.type()])
        params = [ctx.type() for _ in range(r.randint(0, 14))]
        variadic = r.random() < 0.25
        vtypes = []
        if variadic:
            # Variadic arguments after default promotion: no float, char, short, _Bool.
            for _ in range(r.randint(1, 6)):
                if r.random() < 0.3:
                    vtypes.append(('struct', ctx.new_struct() if r.random() < 0.5 or not ctx.structs else r.choice(ctx.structs)[0]))
                else:
                    vtypes.append(('scalar',) + r.choice([('int', 'd'), ('unsigned', 'u'), ('long', 'ld'), ('double', 'a'), ('long double', 'La')]))
            if not params:
                params = [('scalar', 'int', 'd')]
        funcs.append((i, ret, params, variadic, vtypes))

    head = ['#include <stdio.h>', '#include <stdarg.h>'] + [s[2] for s in ctx.structs]

    def proto(f):
        i, ret, params, variadic, vtypes = f
        rt = 'void' if ret[0] == 'void' else ctext(ret)
        ps = ', '.join('%s p%d' % (ctext(p), k) for k, p in enumerate(params)) or 'void'
        if variadic:
            ps += ', ...'
        return '%s f%d(%s)' % (rt, i, ps)

    callee = list(head)
    for f in funcs:
        i, ret, params, variadic, vtypes = f
        body = [proto(f), '{']
        prints = []
        for k, p in enumerate(params):
            prints += leaves(ctx, p, 'p%d' % k)
        if variadic:
            body.append('    va_list ap; va_start(ap, p%d);' % (len(params) - 1))
            for k, v in enumerate(vtypes):
                body.append('    %s v%d = va_arg(ap, %s);' % (ctext(v), k, ctext(v)))
                prints += leaves(ctx, v, 'v%d' % k)
            body.append('    va_end(ap);')
        for j in range(0, len(prints), 6):
            chunk = prints[j:j + 6]
            fs, args = [], []
            for e, fm, base in chunk:
                pf, cast = fmt_cast(fm, base)
                fs.append(pf)
                args.append(cast + e)
            body.append('    printf("f%d %s\\n", %s);' % (i, ' '.join(fs), ', '.join(args)))
        if ret[0] != 'void':
            body.append('    %s r = %s;' % (ctext(ret), value(ctx, ret)))
            body.append('    return r;')
        body.append('}')
        callee.append('\n'.join(body))

    caller = list(head)
    for f in funcs:
        caller.append(proto(f) + ';')
    caller.append('int main(void)\n{')
    for f in funcs:
        i, ret, params, variadic, vtypes = f
        args = [value(ctx, p) for p in params] + [value(ctx, v) for v in vtypes]
        call = 'f%d(%s)' % (i, ', '.join(args))
        if ret[0] == 'void':
            caller.append('    %s;' % call)
        else:
            caller.append('    { %s r = %s;' % (ctext(ret), call))
            ls = leaves(ctx, ret, 'r')
            for j in range(0, len(ls), 6):
                chunk = ls[j:j + 6]
                fs, a = [], []
                for e, fm, base in chunk:
                    pf, cast = fmt_cast(fm, base)
                    fs.append(pf)
                    a.append(cast + e)
                caller.append('      printf("r%d %s\\n", %s);' % (i, ' '.join(fs), ', '.join(a)))
            caller.append('    }')
    caller.append('    return 0;\n}')
    return '\n'.join(callee) + '\n', '\n'.join(caller) + '\n'


def build(cc_callee, cc_caller, a, b, exe):
    oa, ob = exe + '_a.o', exe + '_b.o'
    for cc, src, obj in ((cc_callee, a, oa), (cc_caller, b, ob)):
        c = subprocess.run([cc, os.environ.get('OPT', '-O2'), '-w', '-c', '-o', obj, src], capture_output=True, text=True)
        if c.returncode != 0:
            return 'COMPILE FAILED (%s): %s' % (os.path.basename(cc), c.stderr[:300])
    c = subprocess.run([CLANG, '-o', exe, oa, ob], capture_output=True, text=True)
    if c.returncode != 0:
        return 'LINK FAILED: ' + c.stderr[:300]
    p = subprocess.run([exe], capture_output=True, text=True, timeout=20)
    return p.stdout + ('' if p.returncode == 0 else 'EXIT %d' % p.returncode)


def main():
    rnd = random.Random(seed)
    found = 0
    for k in range(count):
        a = os.path.join(W, 'a%d_%d.c' % (seed, k))
        b = os.path.join(W, 'b%d_%d.c' % (seed, k))
        ta, tb = program(rnd, per)
        open(a, 'w').write(ta)
        open(b, 'w').write(tb)
        x = os.path.join(W, 'x')
        ref = build(CLANG, CLANG, a, b, x + 'kk')
        if ref.startswith(('COMPILE', 'LINK')):
            print('clang rejected', a, ref[:300])
            continue
        bad = []
        for tag, ca, cb in (('cc', CCC, CCC), ('ck', CCC, CLANG), ('kc', CLANG, CCC)):
            got = build(ca, cb, a, b, x + tag)
            if got != ref:
                diff = [(p, q) for p, q in zip(ref.split('\n'), got.split('\n')) if p != q]
                bad.append((tag, diff[:2] if diff else got[:200]))
        if bad:
            found += 1
            print('MISMATCH', a, bad)
        else:
            os.remove(a)
            os.remove(b)
    print('seed %d: %d of %d files mismatched' % (seed, found, count))


main()
