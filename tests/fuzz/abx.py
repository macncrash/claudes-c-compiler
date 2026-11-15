"""Call-boundary fuzzer: caller and callee in two files built by ccc and
clang in all four mixes, each printing what clang + clang prints. It checks
what abifuzz and sfz cannot see:

- narrow arguments (_Bool, char, short) and returns computed from runtime
  values, so the upper bits of the register depend on the extension the
  ABI requires (Apple arm64: the caller extends arguments to 32 bits); the
  receiving side uses them in wider arithmetic;
- by-value struct parameters that the callee modifies, while the caller
  passes locals, globals, *p and array elements and prints them after the
  call (they must not change);
- float/double aggregates and mixed 16-byte structs pushed to the stack by
  earlier floating-point arguments, and more than eight mixed arguments
  with narrow ones on the stack (Apple packs those to their natural size);
- callbacks from the callee file into the caller file with the same mix.

usage: abx.py CCC CLANG WORK SEED COUNT   (env OPT, default -O2)
"""
import os, random, resource, subprocess, sys, tempfile

CCC, CLANG, WORK, SEED, COUNT = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]), int(sys.argv[5])
OPT = os.environ.get('OPT', '-O2')
os.makedirs(WORK, exist_ok=True)

NARROW = ['_Bool', 'signed char', 'unsigned char', 'short', 'unsigned short', 'char']
WIDE = ['int', 'unsigned', 'long', 'long long', 'float', 'double']
STRUCTS = {
    'S1': 'struct S1 { float a, b, c; };',
    'S2': 'struct S2 { double a, b, c, d; };',
    'S3': 'struct S3 { long a; double b; };',
    'S4': 'struct S4 { float a; int b; double c; };',
    'S5': 'struct S5 { long a[5]; };',
    'S6': 'struct S6 { char c; short s; int i; };',
    'S7': 'struct S7 { double a; float b; };',
    'S8': 'struct S8 { char c[19]; };',
}
FIELDS = {'S1': ['a', 'b', 'c'], 'S2': ['a', 'b', 'c', 'd'], 'S3': ['a', 'b'], 'S4': ['a', 'b', 'c'],
          'S5': ['a[0]', 'a[4]'], 'S6': ['c', 's', 'i'], 'S7': ['a', 'b'], 'S8': ['c[0]', 'c[18]']}

HDR = '#include <stdio.h>\n' + '\n'.join(STRUCTS.values()) + '\n'

def pr_scalar(t, e):
    if t in ('float', 'double'):
        return 'printf(" %%g", (double)(%s));' % e
    if t in ('long', 'long long'):
        return 'printf(" %%lld", (long long)(%s));' % e
    if t == 'unsigned':
        return 'printf(" %%u", (unsigned)(%s));' % e
    return 'printf(" %%d", (int)(%s));' % e

def pr_struct(s, e):
    return ' '.join(pr_scalar('double' if f in ('a', 'b', 'c', 'd') and s in ('S1', 'S2', 'S7') or (s == 'S3' and f == 'b') or (s == 'S4' and f in ('a', 'c')) else 'long', '%s.%s' % (e, f)) for f in FIELDS[s])

def narrow_val(t):
    """A runtime value of narrow type t computed from a volatile int."""
    return '(%s)(VI * %d + %d)' % (t, random.choice([1, 3, 70001, 1000003, -7]), random.randint(-300, 300))

def program():
    nf = random.randint(3, 7)
    callee, caller = [HDR], [HDR, 'static volatile int VI = %d;' % random.randint(-999, 999)]
    callee.append('static volatile int VJ = %d;' % random.randint(-999, 999))
    protos = []
    for s in STRUCTS:
        caller.append('struct %s G%s = { %s };' % (s, s, ', '.join(str(random.randint(1, 50)) for _ in FIELDS[s])))
    main = ['int main(void) {']
    for k in range(nf):
        params = []
        nfp = random.randint(0, 9)  # leading FP args push later aggregates to the stack
        for i in range(nfp):
            params.append(random.choice(['float', 'double']))
        for i in range(random.randint(1, 10)):
            c = random.randrange(10)
            params.append(random.choice(NARROW) if c < 4 else random.choice(WIDE) if c < 6 else 'struct ' + random.choice(list(STRUCTS)))
        random.shuffle(params)
        ret = random.choice(NARROW + WIDE + ['struct S3', 'struct S5', 'void'])
        plist = ', '.join('%s p%d' % (t, i) for i, t in enumerate(params))
        use_cb = random.random() < 0.3
        cbt = random.choice(NARROW)
        if use_cb:
            plist += ', int (*cb)(%s, double, %s)' % (cbt, 'struct S6')
        proto = '%s f%d(%s)' % (ret, k, plist)
        protos.append(proto + ';')
        body = [proto + ' {', '    printf("f%d");' % k]
        for i, t in enumerate(params):
            if t.startswith('struct'):
                s = t.split()[1]
                body.append('    ' + pr_struct(s, 'p%d' % i))
                # Modify the parameter: the caller's object must not change.
                f = random.choice(FIELDS[s])
                body.append('    p%d.%s = 0;' % (i, f))
            else:
                # Narrow parameters used in wider arithmetic.
                body.append('    ' + pr_scalar('long' if t in NARROW else t, '(long)p%d * 3 + 1' % i if t in NARROW else 'p%d' % i))
        if use_cb:
            body.append('    { struct S6 s6 = { 1, 2, 3 }; int r = cb(%s, 2.5, s6); printf(" cb=%%d", r); }' % narrow_val(cbt).replace('VI', 'VJ'))
        body.append('    printf("\\n");')
        if ret == 'void':
            pass
        elif ret in NARROW:
            body.append('    return (%s)(VJ * %d + %d);' % (ret, random.choice([3, 70001, -9]), random.randint(-500, 500)) if not params[0].startswith('struct') else '    return (%s)(VJ * 70001 + 5);' % ret)
        elif ret in WIDE:
            body.append('    return (%s)(VJ * %d);' % (ret, random.randint(1, 9)))
        else:
            s = ret.split()[1]
            body.append('    { %s r = { %s }; return r; }' % (ret, ', '.join(str(random.randint(1, 9)) for _ in FIELDS[s])))
        body.append('}')
        callee.append('\n'.join(body))
        # The call: lvalue struct arguments, printed again after the call.
        decls, args, after = [], [], []
        for i, t in enumerate(params):
            if t.startswith('struct'):
                s = t.split()[1]
                where = random.randrange(4)
                if where == 0:
                    decls.append('%s a%d_%d = { %s };' % (t, k, i, ', '.join(str(random.randint(1, 50)) for _ in FIELDS[s])))
                    lv = 'a%d_%d' % (k, i)
                elif where == 1:
                    lv = 'G%s' % s
                elif where == 2:
                    decls.append('%s arr%d_%d[2] = { { %s } };' % (t, k, i, ', '.join(str(random.randint(1, 50)) for _ in FIELDS[s])))
                    lv = 'arr%d_%d[0]' % (k, i)
                else:
                    decls.append('%s *pp%d_%d = &G%s;' % (t, k, i, s))
                    lv = '(*pp%d_%d)' % (k, i)
                args.append(lv)
                after.append(pr_struct(s, lv))
            elif t in NARROW:
                args.append(narrow_val(t))
            elif t in ('float', 'double'):
                args.append('(%s)(VI * 0.25 + %d)' % (t, random.randint(-9, 9)))
            else:
                args.append('(%s)(VI * %d)' % (t, random.randint(1, 99)))
        if use_cb:
            args.append('cbk%d' % k)
            caller.append('%s;' % proto)
            caller.append('static int cbk%d(%s c, double d, struct S6 s) { printf(" [cb %%d %%g %%d %%d %%d]", (int)c * 5, d, s.c, s.s, s.i); s.i = 0; return (int)c - 1; }' % (k, cbt))
        else:
            caller.append('%s;' % proto)
        call = 'f%d(%s)' % (k, ', '.join(args))
        main.append('    { ' + ' '.join(decls))
        if ret == 'void':
            main.append('      %s;' % call)
        elif ret in NARROW or ret in WIDE:
            main.append('      %s r = %s; %s printf("\\n");' % (ret, call, pr_scalar('long' if ret in NARROW else ret, '(long)r * 5 + 2' if ret in NARROW else 'r')))
        else:
            main.append('      %s r = %s; %s printf("\\n");' % (ret, call, pr_struct(ret.split()[1], 'r')))
        main.append('      printf("after"); %s printf("\\n"); }' % ' '.join(after))
    main.append('    return 0;\n}')
    return {'callee': '\n'.join(callee) + '\n', 'caller': '\n'.join(caller + main) + '\n'}

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
        d = os.path.join(WORK, 'x%d_%d' % (SEED, it))
        os.makedirs(d, exist_ok=True)
        for f, text in program().items():
            open(os.path.join(d, f + '.c'), 'w').write(text)
        objs, ok = {}, True
        for comp, tag in ((CLANG, 'c'), (CCC, 'k')):
            for f in ('caller', 'callee'):
                o = os.path.join(d, '%s.%s.o' % (f, tag))
                rc, _, err = run([comp, '-w', OPT, '-c', os.path.join(d, f + '.c'), '-o', o])
                if rc != 0:
                    ok = False
                    print('%s %s %s' % ('clang-fail' if tag == 'c' else 'FAIL-BUILD', d, f), err[:300])
                objs[f + tag] = o
        if not ok:
            bad += 1
            continue
        outs = {}
        for mix in ('cc', 'kk', 'kc', 'ck'):
            exe = os.path.join(d, 'x_' + mix)
            rc, _, err = run([CLANG, objs['caller' + mix[0]], objs['callee' + mix[1]], '-o', exe])
            if rc != 0:
                outs[mix] = 'LINK ' + err[:200]
                continue
            rc, out, _ = run([exe], cap=True)
            outs[mix] = out + 'rc %d\n' % rc
        wrong = [m for m in ('kk', 'kc', 'ck') if outs[m] != outs['cc']]
        if wrong:
            bad += 1
            with open(os.path.join(d, 'diff'), 'w') as fh:
                for m in wrong:
                    fh.write('== %s (caller %s, callee %s)\n%s\n' % (m, m[0], m[1], outs[m][:4000]))
                fh.write('== cc\n%s\n' % outs['cc'][:4000])
            print('DIFF', d, ' '.join(wrong))
        else:
            for name in os.listdir(d):
                os.remove(os.path.join(d, name))
            os.rmdir(d)
    print('done %d bad %d skipped %d' % (COUNT, bad, skipped))

main()
