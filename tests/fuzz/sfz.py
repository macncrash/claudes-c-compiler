"""Struct passing/return ABI fuzzer: random struct types (scalars of every
kind, arrays, nested structs, unions, bit-fields, packed and over-aligned
layouts, float/double HFAs incl. nested, long double, __int128, _Complex)
passed and returned by value with up to ~14 mixed parameters, through direct
calls, volatile function pointers and callbacks (the callee calling back into
the caller's file). callee.c and caller.c are built by ccc and clang in all
four mixes and must print what clang + clang prints.

usage: sfz.py CCC CLANG WORK SEED COUNT   (env OPT, default -O2)
"""
import os, random, subprocess, sys

CCC, CLANG, WORK, SEED, COUNT = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]), int(sys.argv[5])
OPT = os.environ.get('OPT', '-O2')
os.makedirs(WORK, exist_ok=True)

SCAL = ['char', 'unsigned char', 'short', 'int', 'unsigned', 'long', 'long long', 'float', 'double',
        'long double', '_Bool', '__int128', '_Complex float', '_Complex double']
FLOATY = {'float', 'double', 'long double', '_Complex float', '_Complex double'}

def R(xs):
    return random.choice(xs)

class T:
    """A struct/union type: name, fields [(kind, ctype, name, extra)]."""
    pass

TYPES = []
NID = [0]

def lit(ct):
    if ct in ('float', 'double', 'long double'):
        return '%r' % (random.randint(-4000, 4000) / 16.0) + ('f' if ct == 'float' else 'L' if ct == 'long double' else '')
    if ct == '_Complex float':
        return '(%r + %r * 1.0fi)' % (random.randint(-99, 99) / 4.0, random.randint(-99, 99) / 4.0)
    if ct == '_Complex double':
        return '(%r + %r * 1.0i)' % (random.randint(-99, 99) / 4.0, random.randint(-99, 99) / 4.0)
    if ct == '_Bool':
        return str(random.randint(0, 1))
    if ct == '__int128':
        return '((__int128)%d << 64 | %dULL)' % (random.randint(-999, 999), random.randint(0, 2**63))
    if ct in ('char', 'unsigned char'):
        return str(random.randint(0, 120))
    if ct == 'short':
        return str(random.randint(-30000, 30000))
    return str(random.randint(-2**30, 2**30))

def new_type(depth):
    t = T()
    NID[0] += 1
    t.name = 'T%d' % NID[0]
    t.union = random.random() < 0.12 and depth > 0
    t.packed = random.random() < 0.12
    t.align = R([0] * 9 + [16, 32])
    t.fields = []
    hfa = random.random() < 0.3
    hct = R(['float', 'double'])
    n = random.randint(1, 4 if hfa else 6)
    for i in range(n):
        fname = 'f%d' % i
        k = random.random()
        if hfa:
            if k < 0.25:
                t.fields.append(('arr', hct, fname, random.randint(1, 3)))
            elif k < 0.35 and depth < 2:
                sub = new_hfa(hct)
                t.fields.append(('struct', sub, fname, None))
            else:
                t.fields.append(('scalar', hct, fname, None))
            continue
        if k < 0.55:
            t.fields.append(('scalar', R(SCAL), fname, None))
        elif k < 0.7:
            t.fields.append(('arr', R(['char', 'short', 'int', 'float', 'double', 'unsigned char']), fname, random.randint(1, 7)))
        elif k < 0.82 and depth < 2:
            t.fields.append(('struct', new_type(depth + 1), fname, None))
        elif not t.union:
            bt = R(['int', 'unsigned', 'unsigned char', 'long'])
            t.fields.append(('bits', bt, fname, random.randint(1, 8 if bt == 'unsigned char' else 20)))
        else:
            t.fields.append(('scalar', R(SCAL), fname, None))
    if t.union:
        t.packed = False
    TYPES.append(t)
    return t

def new_hfa(ct):
    t = T()
    NID[0] += 1
    t.name = 'T%d' % NID[0]
    t.union = False
    t.packed = False
    t.align = 0
    t.fields = [('scalar', ct, 'f%d' % i, None) for i in range(random.randint(1, 2))]
    TYPES.append(t)
    return t

def decl(t):
    out = ['%s %s {' % ('union' if t.union else 'struct', t.name)]
    for kind, ct, fn, ex in t.fields:
        if kind == 'scalar':
            out.append('  %s %s;' % (ct, fn))
        elif kind == 'arr':
            out.append('  %s %s[%d];' % (ct, fn, ex))
        elif kind == 'struct':
            out.append('  %s %s %s;' % ('union' if ct.union else 'struct', ct.name, fn))
        else:
            out.append('  %s %s : %d;' % (ct, fn, ex))
    attrs = []
    if t.packed:
        attrs.append('packed')
    if t.align:
        attrs.append('aligned(%d)' % t.align)
    out.append('}%s;' % (' __attribute__((%s))' % ', '.join(attrs) if attrs else ''))
    return '\n'.join(out)

def tname(t):
    return '%s %s' % ('union' if t.union else 'struct', t.name)

def init(t):
    parts = []
    for kind, ct, fn, ex in (t.fields[:1] if t.union else t.fields):
        if kind == 'scalar':
            parts.append(lit(ct))
        elif kind == 'arr':
            parts.append('{%s}' % ', '.join(lit(ct) for _ in range(ex)))
        elif kind == 'struct':
            parts.append(init(ct))
        else:
            parts.append(str(random.randint(0, 2**(ex - 1) - 1)))
    return '{%s}' % ', '.join(parts)

def value(t):
    return '((%s)%s)' % (tname(t), init(t))

def pr_scalar(ct, e):
    if ct in ('float', 'double'):
        return 'printf(" %%.9g", (double)(%s));' % e
    if ct == 'long double':
        return 'printf(" %%.17Lg", %s);' % e
    if ct.startswith('_Complex'):
        return 'printf(" %%g,%%g", (double)__real__ (%s), (double)__imag__ (%s));' % (e, e)
    if ct == '__int128':
        return 'printf(" %%llx:%%llx", (unsigned long long)((unsigned __int128)(%s) >> 64), (unsigned long long)(%s));' % (e, e)
    return 'printf(" %%lld", (long long)(%s));' % e

def printer(t):
    out = ['static void pr_%s(%s v) {' % (t.name, tname(t)), '  printf(" {");']
    for kind, ct, fn, ex in (t.fields[:1] if t.union else t.fields):
        if kind == 'scalar' or kind == 'bits':
            out.append('  ' + pr_scalar(ct, 'v.' + fn))
        elif kind == 'arr':
            for j in range(ex):
                out.append('  ' + pr_scalar(ct, 'v.%s[%d]' % (fn, j)))
        else:
            out.append('  pr_%s(v.%s);' % (ct.name, fn))
    out.append('  printf(" }");')
    out.append('}')
    return '\n'.join(out)

def arg_types(tops):
    n = random.randint(1, 14)
    out = []
    for _ in range(n):
        out.append(R(tops) if random.random() < 0.6 else R(['int', 'double', 'float', 'long', 'char', 'long double', '_Complex double']))
    return out

def is_struct(a):
    return isinstance(a, T)

def ctext(a):
    return tname(a) if is_struct(a) else a

def val(a):
    return value(a) if is_struct(a) else lit(a)

def pr(a, e):
    return 'pr_%s(%s);' % (a.name, e) if is_struct(a) else pr_scalar(a, e)

def build(d):
    del TYPES[:]
    NID[0] = 0
    tops = [new_type(0) for _ in range(random.randint(3, 6))]
    funcs, calls, cbs, protos = [], [], [], []
    for k in range(random.randint(4, 8)):
        params = arg_types(tops)
        ret = R(tops + ['void', 'double', 'int']) if random.random() < 0.85 else R(tops)
        name = 'g%d' % k
        use_cb = random.random() < 0.3
        cbret = R(tops)
        cbarg = R(tops)
        plist = ['%s p%d' % (ctext(a), i) for i, a in enumerate(params)]
        if use_cb:
            plist.append('%s (*cb)(%s, int, %s)' % (ctext(cbret), ctext(cbarg), 'double'))
        proto = '%s %s(%s)' % (ctext(ret), name, ', '.join(plist))
        protos.append(proto + ';')
        body = [proto + ' {', '  printf("%s");' % name]
        for i, a in enumerate(params):
            body.append('  ' + pr(a, 'p%d' % i))
        if use_cb:
            body.append('  { %s r_ = cb(%s, %d, 2.5); printf(" cb->"); %s }' % (ctext(cbret), val(cbarg), random.randint(0, 99), pr(cbret, 'r_')))
        body.append('  printf("\\n");')
        if ret != 'void':
            same = [i for i, a in enumerate(params) if a is ret]
            if same and random.random() < 0.5:
                body.append('  return p%d;' % R(same))
            else:
                body.append('  return %s;' % val(ret))
        body.append('}')
        funcs.append('\n'.join(body))
        args = [val(a) for a in params]
        if use_cb:
            cbn = 'cb%d' % k
            cbs.append('%s %s(%s a, int b, double c) { printf("  %s"); %s printf(" %%d %%g\\n", b, c); return %s; }'
                       % (ctext(cbret), cbn, ctext(cbarg), cbn, pr(cbarg, 'a'), val(cbret)))
            args.append(cbn)
        callexpr = '%s(%s)' % (name, ', '.join(args))
        if random.random() < 0.3:
            callexpr = '((%s (*volatile)(%s))%s)(%s)' % (ctext(ret), ', '.join(p.rsplit(' ', 1)[0] if '(*cb)' not in p else '%s (*)(%s, int, double)' % (ctext(cbret), ctext(cbarg)) for p in plist), name, ', '.join(args))
        if ret == 'void':
            calls.append('  %s;' % callexpr)
        else:
            calls.append('  { %s r_ = %s; printf("ret"); %s printf("\\n"); }' % (ctext(ret), callexpr, pr(ret, 'r_')))
            if is_struct(ret) and not ret.union and ret.fields[0][0] == 'scalar':
                calls.append('  { printf("mem"); %s printf("\\n"); }' % pr_scalar(ret.fields[0][1], '%s.%s' % (callexpr, ret.fields[0][2])))
    hdr = ['#include <stdio.h>'] + [decl(t) for t in TYPES] + [printer(t) for t in TYPES]
    callee = hdr + funcs
    caller = hdr + protos + cbs + ['int main(void) {'] + calls + ['  return 0;', '}']
    open(os.path.join(d, 'callee.c'), 'w').write('\n'.join(callee) + '\n')
    open(os.path.join(d, 'caller.c'), 'w').write('\n'.join(caller) + '\n')

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
        d = os.path.join(WORK, 's%d_%d' % (SEED, it))
        os.makedirs(d, exist_ok=True)
        build(d)
        objs = {}
        ok = True
        for cname, cc in (('c', CLANG), ('x', CCC)):
            for f in ('callee', 'caller'):
                o = os.path.join(d, '%s.%s.o' % (f, cname))
                rc, _, err = run([cc, '-w', OPT, '-c', os.path.join(d, f + '.c'), '-o', o])
                if rc != 0:
                    print(('SKIP' if cname == 'c' else 'FAIL-BUILD'), d, f, err[:300])
                    if cname == 'c':
                        skipped += 1
                    else:
                        bad += 1
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
            rc, _, _ = run([CLANG, objs[('caller', combo[0])], objs[('callee', combo[1])], '-o', exe])
            rc2, out, _ = run([exe]) if rc == 0 else (1, '', '')
            outs['%s%s' % combo] = out if rc2 == 0 else '<rc %d>' % rc2
        ref = outs['cc']
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
                            fh.write('%s\n  clang %s\n  this  %s\n' % (c, a, b))
            print('DIFF', d, ' '.join(diffs))
        else:
            for f in os.listdir(d):
                os.remove(os.path.join(d, f))
            os.rmdir(d)
    print('done %d bad %d skipped %d' % (COUNT, bad, skipped))

main()
