"""Over-aligned object fuzzer: functions whose frames need realigning
(locals aligned to 16 .. 4096 with __attribute__((aligned)) and _Alignas,
over-aligned struct types), mixed with VLAs, alloca, calls with many
arguments (some on the stack), variadic calls, over-aligned structs passed
and returned by value, recursion through realigned frames, and globals and
statics aligned to 4096. Every object is filled with a pattern, and after
the calls each one's alignment and contents are checked and hashed.
Programs UBSan or ASan flag are skipped.

usage: alfz.py CCC CLANG WORK SEED COUNT   (env OPT, default -O2)
"""
import os, random, resource, subprocess, sys, tempfile

CCC, CLANG, WORK, SEED, COUNT = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]), int(sys.argv[5])
OPT = os.environ.get('OPT', '-O2')
GCC = os.environ.get('GCC', '/opt/homebrew/bin/gcc-15')
os.makedirs(WORK, exist_ok=True)

HDR = r'''#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <alloca.h>
typedef unsigned long UL;
static UL H;
static void hb(const void *p, unsigned n) { const unsigned char *c = p; while (n--) H = (H ^ *c++) * 1099511628211UL; }
static void hv(UL v) { H = (H ^ v) * 1099511628211UL; }
static void fill(void *p, unsigned n, unsigned s) { unsigned char *c = p; for (unsigned i = 0; i < n; i++) c[i] = (unsigned char)(s * 37 + i * 11); }
static int check(const void *p, unsigned n, unsigned s) { const unsigned char *c = p; for (unsigned i = 0; i < n; i++) if (c[i] != (unsigned char)(s * 37 + i * 11)) return 0; return 1; }
#define AL(p, n) ((int)(((uintptr_t)(p)) % (n) == 0))
static volatile int VZ = 0;
'''

ALIGNS = [16, 32, 64, 128, 256, 512, 1024, 4096]

def program():
    out = [HDR]
    # Over-aligned struct types.
    nst = random.randint(1, 3)
    st = []
    for k in range(nst):
        a = random.choice(ALIGNS[:5])
        n = random.choice([1, 3, 8, 13, 24])
        how = random.randrange(3)
        if how == 0:
            out.append('struct A%d { char c[%d]; } __attribute__((aligned(%d)));' % (k, n, a))
            out.append('static void hA%d(const struct A%d *p) { hb(p->c, %d); }' % (k, k, n))
        elif how == 1:
            out.append('struct A%d { char h; long long m __attribute__((aligned(%d))); char t[%d]; };' % (k, a, n))
            out.append('static void hA%d(const struct A%d *p) { hv(p->h); hv(p->m); hb(p->t, %d); }' % (k, k, n))
        else:
            out.append('struct A%d { _Alignas(%d) char c[%d]; int x; };' % (k, a, n))
            out.append('static void hA%d(const struct A%d *p) { hb(p->c, %d); hv(p->x); }' % (k, k, n))
        st.append((k, a))
    out.append('static char G4k[%d] __attribute__((aligned(4096)));' % random.randint(1, 64))
    out.append('_Alignas(%d) static int Gal[%d] = { %d };' % (random.choice(ALIGNS), random.randint(1, 9), random.randint(1, 99)))
    # Callees: many-argument, variadic, struct by value.
    out.append('__attribute__((noinline)) static UL many(int a, long b, double c, int d, char e, long f, float g, int h, long i, double j, int k, long l, char m) '
               '{ return (UL)a + b * 3 + (UL)(long)(c * 4) + d + e + f + (UL)(long)(g * 2) + h + i + (UL)(long)(j * 8) + k + l + m; }')
    out.append('__attribute__((noinline)) static UL va(int n, ...) { va_list ap; va_start(ap, n); UL s = 0; for (int i = 0; i < n; i++) s = s * 31 + (UL)va_arg(ap, long); va_end(ap); return s; }')
    for k, a in st:
        out.append('__attribute__((noinline)) static struct A%d pass%d(struct A%d v, int t) { hv(AL(&v, %d)); hA%d(&v); fill(&v, sizeof v, t); return v; }' % (k, k, k, a, k))
    nf = random.randint(2, 4)
    for f in range(nf):
        body = ['__attribute__((noinline)) static UL f%d(int n, int depth) {' % f, '    UL r = n;']
        objs = []
        for j in range(random.randint(2, 6)):
            c = random.randrange(6)
            nm = 'o%d' % j
            if c == 0:
                a = random.choice(ALIGNS)
                body.append('    char %s[%d] __attribute__((aligned(%d)));' % (nm, random.randint(1, 200), a))
                objs.append((nm, a, 'sizeof %s' % nm))
            elif c == 1:
                a = random.choice(ALIGNS)
                body.append('    _Alignas(%d) long long %s;' % (a, nm))
                objs.append((nm, a, 'sizeof %s' % nm))
            elif c == 2 and st:
                k, a = random.choice(st)
                body.append('    struct A%d %s[%d];' % (k, nm, random.randint(1, 3)))
                objs.append((nm, a, 'sizeof %s' % nm))
            elif c == 3:
                body.append('    char %s[n + %d];' % (nm, random.randint(1, 40)))
                objs.append((nm, 1, 'sizeof %s' % nm))
            elif c == 4:
                body.append('    char *%s = alloca(n * %d + 1);' % (nm, random.randint(1, 16)))
                objs.append(('*' + nm, 16, '(unsigned)(n * 1 + 1)'))
            else:
                a = random.choice(ALIGNS)
                body.append('    static char %s[%d] __attribute__((aligned(%d)));' % (nm, random.randint(1, 64), a))
                objs.append((nm, a, 'sizeof %s' % nm))
        for i, (nm, a, sz) in enumerate(objs):
            p = nm[1:] if nm.startswith('*') else '&' + nm if not nm.startswith('*') else nm
            ptr = nm[1:] if nm.startswith('*') else '(void *)&%s' % nm
            body.append('    fill(%s, %s, %d + n);' % (ptr, sz, i))
        for _ in range(random.randint(1, 4)):
            c = random.randrange(5)
            if c == 0:
                body.append('    r += many(n, %d, %s, n, (char)%d, depth, 1.5f, %d, n * 2L, 0.25, %d, -1L, (char)n);'
                            % (random.randint(-9, 99), random.choice(['2.5', '-0.75']), random.randint(0, 99), random.randint(0, 9), random.randint(0, 9)))
            elif c == 1:
                k = random.randint(1, 9)
                body.append('    r += va(%d, %s);' % (k, ', '.join('(long)(n + %d)' % random.randint(0, 99) for _ in range(k))))
            elif c == 2 and st:
                k, a = random.choice(st)
                body.append('    { struct A%d v; fill(&v, sizeof v, n); struct A%d w = pass%d(v, n + 1); hv(AL(&w, %d)); hA%d(&w); }' % (k, k, k, a, k))
            elif c == 3 and f > 0:
                body.append('    if (depth > 0) r += f%d(n + 1, depth - 1);' % random.randrange(f))
            else:
                body.append('    if (depth > 0) r += f%d(n + 2, depth - 1);' % f)
        for i, (nm, a, sz) in enumerate(objs):
            ptr = nm[1:] if nm.startswith('*') else '(void *)&%s' % nm
            body.append('    hv(AL(%s, %d)); hv(check(%s, %s, %d + n));' % (ptr, a, ptr, sz, i))
        body.append('    hv(AL(G4k, 4096)); hb(Gal, sizeof Gal);')
        body.append('    return r ^ H;')
        body.append('}')
        out.append('\n'.join(body))
    out.append('int main(void) {')
    for f in range(nf):
        out.append('    printf("f%d %%lu %%lu\\n", f%d(%d, %d), H);' % (f, f, random.randint(1, 20), random.randint(0, 3)))
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
        src = os.path.join(WORK, 'a%d_%d.c' % (SEED, it))
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
