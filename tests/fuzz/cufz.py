"""Scope cleanup fuzzer: nested blocks whose variables (ints, structs,
arrays, with VLAs beside them) carry __attribute__((cleanup(fn))), left by
falling off the end, return (the value computed before the cleanups run),
break and continue (a loop body's variables cleaned up each iteration),
goto forward to the function's exit, and goto backward to the top of a
block. Each cleanup records which variable it ran for and its value; the
trace, in order, is compared with clang's. Jumps only leave scopes, never
enter them. Programs UBSan or ASan flag are skipped.

usage: cufz.py CCC CLANG WORK SEED COUNT   (env OPT, default -O2)
"""
import os, random, resource, subprocess, sys, tempfile

CCC, CLANG, WORK, SEED, COUNT = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]), int(sys.argv[5])
OPT = os.environ.get('OPT', '-O2')
GCC = os.environ.get('GCC', '/opt/homebrew/bin/gcc-15')
os.makedirs(WORK, exist_ok=True)

HDR = r'''#include <stdio.h>
#include <string.h>
typedef unsigned long UL;
struct S { int a; UL b; char t[5]; };
static UL TR = 1469598103934665603UL;
static int NT;
static void trace(int id, UL v) { TR = (TR ^ ((UL)id << 32 ^ v)) * 1099511628211UL; NT++; printf(" c%d:%lu", id, v); }
#define CL_INT(id) static void cl_i##id(int *p) { trace(id, (UL)*p); *p = -1; }
#define CL_S(id) static void cl_s##id(struct S *p) { trace(id, p->a + p->b + (UL)p->t[1]); p->a = 0; }
#define CL_A(id) static void cl_a##id(unsigned (*p)[4]) { trace(id, (*p)[0] + (*p)[3] * 7); }
static volatile int VZ = 0;
'''

class Gen:
    def __init__(self):
        self.nid = 0
        self.helpers = []

    def fresh(self, kind):
        self.nid += 1
        i = self.nid
        self.helpers.append({'i': 'CL_INT(%d)', 's': 'CL_S(%d)', 'a': 'CL_A(%d)'}[kind] % i)
        return i

    def decl(self, ind, vars_):
        kind = random.choice('iiisa')
        i = self.fresh(kind)
        if kind == 'i':
            init = random.choice(['%d' % random.randint(0, 99), 'n', 'acc & 255', 'x%d + 1' % vars_[-1][0] if vars_ and vars_[-1][1] == 'i' else '3'])
            vars_.append((i, 'i'))
            return '%sint x%d __attribute__((cleanup(cl_i%d))) = %s;' % (ind, i, i, init)
        if kind == 's':
            vars_.append((i, 's'))
            return '%sstruct S x%d __attribute__((cleanup(cl_s%d))) = { %d, acc, "ab%d" };' % (ind, i, i, random.randint(0, 9), random.randint(0, 9))
        vars_.append((i, 'a'))
        return '%sunsigned x%d[4] __attribute__((cleanup(cl_a%d))) = { %d, (unsigned)n, 2, %d };' % (ind, i, i, random.randint(0, 9), random.randint(0, 9))

    def use(self, vars_):
        if not vars_:
            return 'acc = acc * 3 + 1;'
        i, k = random.choice(vars_)
        if k == 'i':
            return random.choice(['x%d += (int)(acc & 7);' % i, 'acc = acc * 31 + (UL)x%d;' % i, 'x%d = x%d * 2 + 1;' % (i, i)])
        if k == 's':
            return random.choice(['x%d.a += 3; acc += x%d.b;' % (i, i), 'x%d.t[1] = (char)acc;' % i])
        return random.choice(['x%d[3] += (unsigned)acc; acc += x%d[0];' % (i, i), 'x%d[0] = x%d[3] ^ 5;' % (i, i)])

    def block(self, ind, depth, vars_, in_loop, top_label):
        """A block's lines. vars_ is the enclosing variables in scope."""
        out = []
        mine = list(vars_)
        label = None
        if depth > 0 and random.random() < 0.25:
            # A backward goto target, before this block's declarations.
            label = 'again%d' % self.fresh_label()
            out.append('%s%s: (void)0;' % (ind, label))
        for _ in range(random.randint(0, 3)):
            out.append(self.decl(ind, mine))
        if random.random() < 0.2:
            out.append('%s{ unsigned vla[n + 1]; vla[n] = (unsigned)acc; acc += vla[n] + n; }' % ind)
        for _ in range(random.randint(1, 4)):
            c = random.randrange(10)
            if c < 3:
                out.append(ind + self.use(mine))
            elif c == 3 and depth < 3:
                out.append(ind + '{')
                out += self.block(ind + '    ', depth + 1, mine, in_loop, top_label)
                out.append(ind + '}')
            elif c == 4 and depth < 3:
                lv = 'i%d' % self.fresh_label()
                out.append('%sfor (int %s = 0; %s < %d; %s++) {' % (ind, lv, lv, random.randint(1, 4), lv))
                out += self.block(ind + '    ', depth + 1, mine, True, top_label)
                out.append(ind + '}')
            elif c == 5:
                out.append('%sif ((acc & 7) == %d) return (UL)%s;' % (ind, random.randrange(8), self.retval(mine)))
            elif c == 6 and in_loop:
                out.append('%sif ((acc & 3) == %d) %s;' % (ind, random.randrange(4), random.choice(['break', 'continue'])))
            elif c == 7:
                out.append('%sif ((acc & 15) == %d) goto %s;' % (ind, random.randrange(16), top_label))
            elif c == 8 and label:
                out.append('%sif (back++ < 2 && (acc & 1)) goto %s;' % (ind, label))
            else:
                out.append(ind + 'acc = acc * 5 + (UL)VZ + %d;' % random.randint(0, 9))
        return out

    def retval(self, vars_):
        if not vars_:
            return 'acc'
        i, k = random.choice(vars_)
        # The value is taken before the cleanups run (they set ints to -1).
        return {'i': 'x%d' % i, 's': 'x%d.a + x%d.b' % (i, i), 'a': 'x%d[0] + x%d[3]' % (i, i)}[k]

    nlab = 0

    def fresh_label(self):
        Gen.nlab += 1
        return Gen.nlab

def program():
    g = Gen()
    funcs = []
    nf = random.randint(2, 5)
    for f in range(nf):
        body = ['__attribute__((noinline)) static UL f%d(int n, UL acc) {' % f, '    int back = 0;']
        top = []
        for _ in range(random.randint(0, 2)):
            top.append(g.decl('    ', []))
        body += top
        vars_top = []
        # Re-derive the top variables for use (decl appended them to a throwaway list).
        for line in top:
            i = int(line.split(' x')[1].split()[0].split('[')[0])
            k = 'i' if line.strip().startswith('int') else 's' if 'struct' in line else 'a'
            vars_top.append((i, k))
        body.append('    {')
        body += g.block('        ', 1, vars_top, False, 'out%d' % f)
        body.append('    }')
        body.append('out%d:' % f)
        body.append('    return acc + (UL)back;')
        body.append('}')
        funcs.append('\n'.join(body))
    out = [HDR] + g.helpers + funcs + ['int main(void) {', '    UL acc = 7;']
    for _ in range(random.randint(4, 10)):
        f = random.randrange(nf)
        out.append('    printf("f%d", %d); acc = acc * 31 + f%d(%d, acc); printf(" -> %%lu\\n", acc);' % (f, f, f, random.randint(0, 5)))
    out.append('    printf("trace %lu %d\\n", TR, NT);')
    out.append('    return 0;\n}')
    return '\n'.join(out) + '\n'

CAP = 1 << 20

def _limit():
    # A file-size limit: a child writing more than CAP bytes to its output files
    # gets SIGXFSZ, so a generated loop that never ends cannot fill the disk.
    resource.setrlimit(resource.RLIMIT_FSIZE, (CAP, CAP))

def run(cmd, timeout=60, cap=False):
    """Run cmd with its output in temporary files limited to CAP bytes each."""
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
        Gen.nlab = 0
        src = os.path.join(WORK, 'u%d_%d.c' % (SEED, it))
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
