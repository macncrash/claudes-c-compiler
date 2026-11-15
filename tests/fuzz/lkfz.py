"""Linkage and function attribute fuzzer: two translation units built by ccc
and clang in all four mixes, each mix printing what clang + clang prints.
Weak functions (with jump-table switches) and weak variables, randomly
replaced by strong definitions in the other file; constructor(N) and
destructor(N) priorities and unprioritized ones in both files (each file's
order is defined; files run in link order); pure and const functions called
around writes to the globals they read (a call may be merged only when
nothing they read has changed); C99 inline functions defined in both files
with one extern inline definition, also compared by address across files;
section("__DATA,...") and used statics; static functions of one name in
both files. Destructors print, so their order is compared too.

usage: lkfz.py CCC CLANG WORK SEED COUNT   (env OPT, default -O2)
"""
import os, random, resource, subprocess, sys, tempfile

CCC, CLANG, WORK, SEED, COUNT = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]), int(sys.argv[5])
OPT = os.environ.get('OPT', '-O2')
os.makedirs(WORK, exist_ok=True)

def program():
    nw = random.randint(1, 5)
    ni = random.randint(1, 3)
    shared = ['#include <stdio.h>', 'extern int G[4];']
    for k in range(nw):
        shared.append('int wf%d(int);' % k)
        shared.append('extern int wv%d;' % k)
    for k in range(ni):
        e = random.choice(['x * %d + G[%d]' % (random.randint(1, 9), random.randrange(4)), '(x ^ %d) - G[%d]' % (random.randint(1, 99), random.randrange(4)), 'x > %d ? x - %d : x + %d' % (random.randint(0, 50), random.randint(1, 9), random.randint(1, 9))])
        shared.append('inline int il%d(int x) { return %s; }' % (k, e))
    shared.append('__attribute__((pure)) int pf(int i);')
    shared.append('__attribute__((const)) int cf(int x);')
    shared.append('int (*ilptr_b(int k))(int);')
    shared.append('void bump(int i, int v);')
    files = {'a': list(shared), 'b': list(shared)}
    files['a'].append('int G[4] = { %s };' % ', '.join(str(random.randint(-9, 99)) for _ in range(4)))
    # Weak definitions in a; some replaced in b.
    for k in range(nw):
        cases = random.randint(4, 9)
        sw = ' '.join('case %d: return %d;' % (c, random.randint(-99, 999)) for c in range(cases))
        files['a'].append('__attribute__((weak)) int wf%d(int x) { switch (x & 15) { %s default: return x; } }' % (k, sw))
        files['a'].append('__attribute__((weak)) int wv%d = %d;' % (k, random.randint(-99, 99)))
        if random.random() < 0.5:
            sw2 = ' '.join('case %d: return %d;' % (c, random.randint(-99, 999)) for c in range(random.randint(4, 9)))
            files['b'].append('int wf%d(int x) { switch (x & 15) { %s default: return -x; } }' % (k, sw2))
        if random.random() < 0.5:
            files['b'].append('int wv%d = %d;' % (k, random.randint(100, 999)))
    # The external definitions of the inline functions, in one file.
    for k in range(ni):
        files[random.choice('ab')].append('extern inline int il%d(int x);' % k)
    files['b'].append('int (*ilptr_b(int k))(int) { switch (k) { %s default: return 0; } }'
                      % ' '.join('case %d: return il%d;' % (k, k) for k in range(ni)))
    files['b'].append('__attribute__((pure)) int pf(int i) { return G[i & 3] * 3 + 1; }')
    files['b'].append('__attribute__((const)) int cf(int x) { return x * x - %d; }' % random.randint(0, 9))
    files['b'].append('void bump(int i, int v) { G[i & 3] += v; }')
    # Constructors and destructors, with distinct priorities in each file.
    for f in 'ab':
        pr = random.sample(range(101, 1000), 8)  # up to 4 constructors and 3 destructors
        for j in range(random.randint(1, 4)):
            p = pr.pop() if random.random() < 0.7 else None
            attr = 'constructor(%d)' % p if p else 'constructor'
            files[f].append('__attribute__((%s)) static void c_%s%d(void) { G[%d] += %d; printf("ctor %s%d\\n"); }'
                            % (attr, f, j, random.randrange(4), random.randint(1, 9), f, j))
        for j in range(random.randint(0, 3)):
            p = pr.pop() if random.random() < 0.7 else None
            attr = 'destructor(%d)' % p if p else 'destructor'
            files[f].append('__attribute__((%s)) static void d_%s%d(void) { printf("dtor %s%d %%d\\n", G[%d]); }'
                            % (attr, f, j, f, j, random.randrange(4)))
        files[f].append('__attribute__((section("__DATA,__lkfz"), used)) static int sv_%s = %d;' % (f, random.randint(0, 99)))
        files[f].append('static int helper(int x) { return x + %d; }' % random.randint(1, 9))
        files[f].append('int use_%s(int x) { return helper(x) + sv_%s; }' % (f, f))
    files['a'].append('int use_b(int x);')
    main = ['int main(void) {', '    unsigned long h = 1469598103934665603UL;', '#define H(v) (h = (h ^ (unsigned long)(v)) * 1099511628211UL)']
    for _ in range(random.randint(8, 20)):
        c = random.randrange(7)
        if c == 0:
            k = random.randrange(nw)
            main.append('    for (int i = 0; i < 12; i++) H(wf%d(i)); H(wv%d);' % (k, k))
        elif c == 1:
            i = random.randrange(4)
            main.append('    { int x = pf(%d); bump(%d, %d); int y = pf(%d); int z = pf(%d); H(x); H(y); H(z); }'
                        % (i, i, random.randint(1, 9), i, i))
        elif c == 2:
            main.append('    { int s = 0; for (int i = 0; i < 5; i++) s += cf(i) + cf(i); H(s); }')
        elif c == 3:
            k = random.randrange(ni)
            main.append('    H(il%d(%d)); H(ilptr_b(%d) == il%d); H(ilptr_b(%d)(%d));' % (k, random.randint(-9, 60), k, k, k, random.randint(0, 9)))
        elif c == 4:
            main.append('    H(use_a(%d) + use_b(%d));' % (random.randint(0, 9), random.randint(0, 9)))
        elif c == 5:
            main.append('    bump(%d, %d);' % (random.randrange(4), random.randint(-5, 5)))
        else:
            main.append('    for (int i = 0; i < 4; i++) H(G[i]);')
        main.append('    printf("%%lu\\n", h);')
    main += ['    return 0;', '}']
    files['a'] += main
    return {f: '\n'.join(v) + '\n' for f, v in files.items()}

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
        d = os.path.join(WORK, 'l%d_%d' % (SEED, it))
        os.makedirs(d, exist_ok=True)
        srcs = program()
        for f, text in srcs.items():
            open(os.path.join(d, f + '.c'), 'w').write(text)
        objs = {}
        ok = True
        for comp, tag in ((CLANG, 'c'), (CCC, 'k')):
            for f in 'ab':
                o = os.path.join(d, '%s.%s.o' % (f, tag))
                rc, _, err = run([comp, '-w', '-std=c11', OPT, '-c', os.path.join(d, f + '.c'), '-o', o])
                if rc != 0:
                    ok = False
                    print('%s %s' % ('clang-fail' if tag == 'c' else 'FAIL-BUILD', d), f, err[:300])
                objs[f + tag] = o
        if not ok:
            if any(os.path.exists(objs[f + 'c']) for f in 'ab') and not all(os.path.exists(objs[f + 'k']) for f in 'ab'):
                bad += 1
            else:
                skipped += 1
            continue
        outs = {}
        for mix in ('cc', 'kk', 'kc', 'ck'):
            exe = os.path.join(d, 'x_' + mix)
            rc, _, err = run([CLANG, objs['a' + mix[0]], objs['b' + mix[1]], '-o', exe])
            if rc != 0:
                outs[mix] = 'LINK ' + err.strip().splitlines()[0][:200] if err.strip() else 'LINK'
                continue
            rc, out, _ = run([exe], cap=True)
            outs[mix] = out + 'rc %d\n' % rc
        ref = outs['cc']
        if ref.startswith('LINK'):
            skipped += 1
            print('clang-link-fail', d, ref)
            continue
        wrong = [m for m in ('kk', 'kc', 'ck') if outs[m] != ref]
        if wrong:
            bad += 1
            with open(os.path.join(d, 'diff'), 'w') as fh:
                for m in wrong:
                    fh.write('== %s\n%s\n== cc\n%s\n' % (m, outs[m][:3000], ref[:3000]))
            print('DIFF', d, ' '.join(wrong))
        else:
            for name in os.listdir(d):
                os.remove(os.path.join(d, name))
            os.rmdir(d)
    print('done %d bad %d skipped %d' % (COUNT, bad, skipped))

main()
