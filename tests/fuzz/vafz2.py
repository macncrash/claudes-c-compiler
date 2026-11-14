"""Variadic fuzzer, second round (types and printing from vafz.py): shapes of
variadic code. Callees: a format-driven va_arg loop, recursion through
va_list *, a va_list in a global, va_copy re-reads after partial use, alloca
and a VLA before va_start, struct / HFA / long double / complex returns, more
than 8 GP and 8 FP named args. Callers: those, plus snprintf with random
conversions into libc. Built split (callee/caller by ccc and clang in all
mixes) and as one file by ccc (static callees, which ccc may inline).

usage: vafz2.py CCC CLANG WORK SEED COUNT   (env OPT, default -O2)
"""
import os, random, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
src = open(os.path.join(HERE, 'vafz.py')).read().replace('\nmain()\n', '\n')
G = {'__name__': 'vafz'}
_argv = list(sys.argv)
exec(compile(src, 'vafz.py', 'exec'), G)
sys.argv = _argv
CCC, CLANG, WORK, SEED, COUNT = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]), int(sys.argv[5])
OPT = os.environ.get('OPT', '-O2')
R, ALL, value_of, vatype, print_stmts, STRUCTS = G['R'], G['ALL'], G['value_of'], G['vatype'], G['print_stmts'], G['STRUCTS']
rint, fv = G['rint'], G['fv']

HDR = G['HDR'] + '#include <stdlib.h>\n#include <alloca.h>\n'
CODE = {t: 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz'[i] for i, t in enumerate(ALL)}
RETS = ['struct D3', 'struct F4', 'struct S24', 'struct S12', 'long double', '_Complex double', '_Complex float', 'struct M1', 'double', 'long']

def reader(t, ap):
    return '{ %s v_ = va_arg(%s, %s); %s }' % (vatype(t), ap, vatype(t), print_stmts(t, 'v_'))

def ret_init(t, acc):
    """An expression of type t made from the long acc."""
    if t in STRUCTS:
        _, parts, fields, _ = STRUCTS[t]
        return '({ %s r_; memset(&r_, 0, sizeof r_); %s r_; })' % (t, ' '.join('r_.%s = %s + %d;' % (f, acc, k) for k, f in enumerate(fields)))
    if t == '_Complex double':
        return '((double)%s + 0.5 * I)' % acc
    if t == '_Complex float':
        return '((float)%s - 1.5f * I)' % acc
    return '(%s)%s' % (t, acc)

def gen(static):
    S = 'static ' if static else ''
    funcs, helpers, calls = [], [], []
    n = 0
    for _ in range(random.randint(4, 7)):
        n += 1
        name = 'f%d' % n
        kind = random.randrange(7)
        var = [R(ALL) for _ in range(random.randint(1, 10))]
        args = [value_of(t) for t in var]
        if kind == 0:
            # format-driven loop over type codes
            codes = ''.join(CODE[t] for t in var)
            body = ['%svoid %s(const char *fmt, ...) {' % (S, name), '  va_list ap; va_start(ap, fmt); printf("%s");' % name,
                    '  for (const char *p = fmt; *p; p++) switch (*p) {']
            for t in sorted(set(var)):
                body.append("    case '%s': %s break;" % (CODE[t], reader(t, 'ap')))
            body += ['  }', '  va_end(ap); printf("\\n");', '}']
            calls.append('  %s("%s", %s);' % (name, codes, ', '.join(args)))
        elif kind == 1:
            # recursion through va_list *
            hn = 'rec%d' % n
            helpers.append('static void %s(int depth, va_list *ap, const char *codes) {' % hn)
            helpers.append('  if (!*codes) return;')
            helpers.append('  switch (*codes) {')
            for t in sorted(set(var)):
                helpers.append("    case '%s': %s break;" % (CODE[t], reader(t, '*ap')))
            helpers.append('  }')
            helpers.append('  printf(" d%%d", depth); %s(depth + 1, ap, codes + 1);' % hn)
            helpers.append('}')
            codes = ''.join(CODE[t] for t in var)
            body = ['%svoid %s(int n, ...) {' % (S, name), '  va_list ap; va_start(ap, n); printf("%s %%d", n);' % name,
                    '  %s(0, &ap, "%s");' % (hn, codes), '  va_end(ap); printf("\\n");', '}']
            calls.append('  %s(%d, %s);' % (name, rint(0, 9), ', '.join(args)))
        elif kind == 2:
            # va_list kept in a global, read by another function
            gn = 'gap%d' % n
            helpers.append('static va_list %s;' % gn)
            helpers.append('static void rd%d(void) {' % n)
            for t in var:
                helpers.append('  ' + reader(t, gn))
            helpers.append('}')
            body = ['%svoid %s(int n, ...) {' % (S, name), '  va_start(%s, n); printf("%s");' % (gn, name),
                    '  rd%d();' % n, '  va_end(%s); printf("\\n");' % gn, '}']
            calls.append('  %s(%d, %s);' % (name, rint(0, 9), ', '.join(args)))
        elif kind == 3:
            # partial read, va_copy, read the rest twice
            k = random.randint(0, len(var))
            body = ['%svoid %s(int n, ...) {' % (S, name), '  va_list ap, c; va_start(ap, n); printf("%s");' % name]
            body += ['  ' + reader(t, 'ap') for t in var[:k]]
            body.append('  va_copy(c, ap);')
            body += ['  ' + reader(t, 'c') for t in var[k:]]
            body.append('  printf(" |");')
            body += ['  ' + reader(t, 'ap') for t in var[k:]]
            body += ['  va_end(c); va_end(ap); printf("\\n");', '}']
            calls.append('  %s(%d, %s);' % (name, rint(0, 9), ', '.join(args)))
        elif kind == 4:
            # alloca / VLA before va_start
            body = ['%svoid %s(int n, ...) {' % (S, name), '  char *buf = alloca(n * 16 + 8); int vla[n + 3];',
                    '  memset(buf, n, n * 16 + 8); for (int i = 0; i < n + 3; i++) vla[i] = i * n;',
                    '  va_list ap; va_start(ap, n); printf("%s %%d %%d", buf[3], vla[n + 2]);' % name]
            body += ['  ' + reader(t, 'ap') for t in var]
            body += ['  va_end(ap); printf(" %d\\n", vla[1] + buf[n]);', '}']
            calls.append('  %s(%d, %s);' % (name, rint(1, 9), ', '.join(args)))
        elif kind == 5:
            # a return value of a struct / HFA / long double / complex type
            rt = R(RETS)
            body = ['%s%s %s(int n, ...) {' % (S, rt, name), '  va_list ap; va_start(ap, n); printf("%s");' % name, '  long acc = n;']
            for t in var:
                body.append('  { %s v_ = va_arg(ap, %s); %s acc += sizeof v_; }' % (vatype(t), vatype(t), print_stmts(t, 'v_')))
            body += ['  va_end(ap); printf("\\n");', '  return %s;' % ret_init(rt, 'acc'), '}']
            calls.append('  { %s r_ = %s(%d, %s); printf("ret"); %s printf("\\n"); }' % (rt, name, rint(0, 9), ', '.join(args), print_stmts(rt, 'r_') if rt in STRUCTS or rt in G['SCALARS'] else ''))
        else:
            # more than 8 GP and 8 FP named args before ...
            ng, nf = random.randint(8, 11), random.randint(8, 11)
            params = ['long g%d' % i for i in range(ng)] + ['double d%d' % i for i in range(nf)]
            random.shuffle(params)
            body = ['%svoid %s(%s, ...) {' % (S, name, ', '.join(params)), '  va_list ap; va_start(ap, %s); printf("%s");' % (params[-1].split()[1], name)]
            body.append('  printf(" %%ld %%g %%ld %%g", %s, %s, %s, %s);' % ('g0', 'd0', 'g%d' % (ng - 1), 'd%d' % (nf - 1)))
            body += ['  ' + reader(t, 'ap') for t in var]
            body += ['  va_end(ap); printf("\\n");', '}']
            cargs = []
            for p in params:
                cargs.append('%dL' % rint(-10**6, 10**6) if p.startswith('long') else fv())
            calls.append('  %s(%s, %s);' % (name, ', '.join(cargs), ', '.join(args)))
        funcs.append('\n'.join(body))
    # snprintf into libc with random conversions
    for _ in range(random.randint(2, 4)):
        specs, vals = [], []
        for _ in range(random.randint(2, 9)):
            k = random.randrange(10)
            if k == 0: specs.append('%hhd'); vals.append('%d' % rint(-300, 300))
            elif k == 1: specs.append('%-8.3f'); vals.append(fv())
            elif k == 2: specs.append('%Lg'); vals.append(R(['1.5L', '-2.25L', '1e300L', '7.0L']))
            elif k == 3: specs.append('%lld'); vals.append('%dLL' % rint(-2**62, 2**62))
            elif k == 4: specs.append('%a'); vals.append(fv())
            elif k == 5: specs.append('%zu'); vals.append('(size_t)%d' % rint(0, 10**9))
            elif k == 6: specs.append('%c'); vals.append(str(rint(65, 90)))
            elif k == 7: specs.append('%.*e'); vals.append('%d, %s' % (rint(0, 6), fv()))
            elif k == 8: specs.append('%5s'); vals.append('"ab"')
            else: specs.append('%#x'); vals.append('%du' % rint(0, 2**32 - 1))
        calls.append('  { char b_[256]; int r_ = snprintf(b_, sizeof b_, "%s", %s); printf("sn %%d [%%s]\\n", r_, b_); }' % (' '.join(specs), ', '.join(vals)))
    return funcs, helpers, calls

def protos(funcs):
    return [f.split('{')[0].strip() + ';' for f in funcs]

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
        d = os.path.join(WORK, 'b%d_%d' % (SEED, it))
        os.makedirs(d, exist_ok=True)
        state = random.getstate()
        funcs, helpers, calls = gen(False)
        open(os.path.join(d, 'callee.c'), 'w').write(HDR + '\n'.join(helpers + funcs) + '\n')
        caller = [HDR, 'char base_[64];'] + [p for p in protos(funcs) if not p.startswith('static')] + ['int main(void) {'] + calls + ['  return 0;', '}']
        open(os.path.join(d, 'caller.c'), 'w').write('\n'.join(caller) + '\n')
        random.setstate(state)
        funcs1, helpers1, calls1 = gen(True)
        one = [HDR, 'char base_[64];'] + helpers1 + funcs1 + ['int main(void) {'] + calls1 + ['  return 0;', '}']
        open(os.path.join(d, 'one.c'), 'w').write('\n'.join(one) + '\n')
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
        for cname, cc in (('one_c', CLANG), ('one_x', CCC)):
            exe = os.path.join(d, cname)
            rc, _, err = run([cc, '-w', OPT, os.path.join(d, 'one.c'), '-o', exe])
            rc2, out, _ = run([exe]) if rc == 0 else (1, '', '')
            outs[cname] = out if rc2 == 0 else '<rc %d %s>' % (rc2, err[:200])
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
