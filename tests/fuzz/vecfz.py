"""Vector extension fuzzer: __attribute__((vector_size(N))) vectors of 8,
16, 32 and 64 bytes (wider ones are split across registers) with char,
short, int, long long, float and double lanes, signed and unsigned: + - *
/ % (divisors kept nonzero and positive), << >> by a vector and by a
scalar (counts masked), & | ^ ~, unary -, comparisons into the matching
integer mask vector, vector-scalar broadcasts, element reads and writes
with runtime indexes, ++ on an element, compound assignment,
__builtin_shufflevector with constant indexes, __builtin_convertvector,
reinterpreting casts between vector types of one size, vectors passed to
and returned from noinline functions, in struct members and arrays, and
loaded from unaligned memory with memcpy. Every vector is hashed by its
bytes. Signed lanes stay small (no lane overflow); both compilers get
-ffp-contract=off. clang is the oracle; lines where gcc disagrees are
dropped.

usage: vecfz.py CCC CLANG WORK SEED COUNT   (env OPT, default -O2)
"""
import os, random, resource, subprocess, sys, tempfile

CCC, CLANG, WORK, SEED, COUNT = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]), int(sys.argv[5])
OPT = os.environ.get('OPT', '-O2')
GCC = os.environ.get('GCC', '/opt/homebrew/bin/gcc-15')
os.makedirs(WORK, exist_ok=True)
FLAGS = ['-ffp-contract=off']

# lane type -> (bytes, signed, float, mask lane type)
LANES = {
    'signed char': (1, True, False, 'signed char'), 'unsigned char': (1, False, False, 'signed char'),
    'short': (2, True, False, 'short'), 'unsigned short': (2, False, False, 'short'),
    'int': (4, True, False, 'int'), 'unsigned': (4, False, False, 'int'),
    'long long': (8, True, False, 'long long'), 'unsigned long long': (8, False, False, 'long long'),
    'float': (4, True, True, 'int'), 'double': (8, True, True, 'long long'),
}
SIZES = [8, 16, 16, 32, 64]

HDR = r'''#include <stdio.h>
#include <string.h>
typedef unsigned long long UX;
static UX H;
static void hb(const void *p, unsigned n) { const unsigned char *c = p; while (n--) H = (H ^ *c++) * 1099511628211ULL; }
static volatile int VZ = 0;
static unsigned char RAW[256];
'''

class VT:
    def __init__(self, lane, size):
        self.lane, self.size = lane, size
        b, s, f, m = LANES[lane]
        self.n, self.signed, self.flt = size // b, s, f
        self.name = 'v_%s_%d' % (lane.replace(' ', ''), size)
        self.mask = VTmask(m, size)

class VTmask:
    def __init__(self, lane, size):
        self.lane, self.size = lane, size
        self.name = 'v_%s_%d' % (lane.replace(' ', ''), size)

def lane_val(vt):
    if vt.flt:
        return random.choice(['0.5', '-1.25', '3.0', '100.75', '-0.0', '7.5', '1e-3', '-2.0'])
    b = LANES[vt.lane][0]
    if vt.signed:
        return str(random.randint(-9, 9) if b == 1 else random.randint(-99, 99))
    return str(random.randint(0, (1 << (8 * b)) - 1 if b < 8 else 1 << 40))

def vlit(vt):
    return '(%s){ %s }' % (vt.name, ', '.join(lane_val(vt) for _ in range(vt.n)))

def program():
    vts = {}
    for _ in range(random.randint(2, 4)):
        lane = random.choice(list(LANES))
        size = random.choice(SIZES)
        if size // LANES[lane][0] < 1:
            continue
        v = VT(lane, size)
        vts[v.name] = v
    vts = list(vts.values())
    tds = {}
    for v in vts:
        tds[v.name] = 'typedef %s %s __attribute__((vector_size(%d)));' % (v.lane, v.name, v.size)
        tds[v.mask.name] = 'typedef %s %s __attribute__((vector_size(%d)));' % (v.mask.lane, v.mask.name, v.size)
    # Same-size reinterpret targets and same-lane-count convert targets.
    extra = []
    for v in vts:
        for lane in LANES:
            b = LANES[lane][0]
            if v.size % b == 0 and random.random() < 0.15:
                t = VT(lane, v.size)
                tds.setdefault(t.name, 'typedef %s %s __attribute__((vector_size(%d)));' % (t.lane, t.name, t.size))
                extra.append(t)
    out = [HDR] + list(tds.values())
    allv = vts + extra
    fns = []
    body = []
    k = 0
    for v in vts:
        out.append('__attribute__((noinline)) static %s pass_%s(%s a, %s b, int i) { a[i & %d] += b[(i + 1) & %d]; return a * (%s)2 - b; }'
                   % (v.name, v.name, v.name, v.name, v.n - 1, v.n - 1, v.lane))
        out.append('struct box_%s { char c; %s v; %s arr[2]; };' % (v.name, v.name, v.name))
    for _ in range(random.randint(8, 20)):
        v = random.choice(vts)
        k += 1
        lines = ['{ %s x = %s, y = %s, z = x;' % (v.name, vlit(v), vlit(v))]
        if random.random() < 0.5:
            lines.append('volatile int ri = VZ + %d;' % random.randrange(v.n))
        else:
            lines.append('int ri = %d;' % random.randrange(v.n))
        for _ in range(random.randint(1, 5)):
            c = random.randrange(15)
            if c < 3:
                op = random.choice(['+', '-', '*'])
                lines.append('z = x %s y;' % op)
            elif c == 3:
                if v.flt:
                    lines.append('z = x / y;')
                else:
                    # A positive nonzero divisor in every lane.
                    lines.append('z = x %s ((y & (%s)7) + (%s)1);' % (random.choice(['/', '%']), v.lane, v.lane))
            elif c == 4 and not v.flt:
                bits = 8 * LANES[v.lane][0]
                sh = random.choice(['<<', '>>']) if not v.signed else '>>'
                if random.random() < 0.5:
                    lines.append('z = x %s (y & (%s)%d);' % (sh, v.lane, bits - 1))
                else:
                    lines.append('z = x %s %d;' % (sh, random.randrange(bits)))
            elif c == 5 and not v.flt:
                lines.append('z = %s;' % random.choice(['x & y', 'x | y', 'x ^ y', '~x', 'x & (%s)%s' % (v.lane, lane_val(v))]))
            elif c == 6:
                lines.append('z = -x;' if v.signed or v.flt else 'z = x + y;')
            elif c == 7:
                op = random.choice(['<', '<=', '>', '>=', '==', '!='])
                lines.append('{ %s m = x %s y; hb(&m, sizeof m); }' % (v.mask.name, op))
            elif c == 8:
                lines.append('z = x + (%s)%s;' % (v.lane, lane_val(v)))
            elif c == 9:
                lines.append('z[ri] = (%s)%s; z[(ri + 1) %% %d]++; y[ri] = z[(ri + 2) %% %d];' % (v.lane, lane_val(v), v.n, v.n))
            elif c == 10:
                idx = ', '.join(str(random.randrange(2 * v.n)) for _ in range(v.n))
                lines.append('z = __builtin_shufflevector(x, y, %s);' % idx)
            elif c == 11:
                # A same-lane-count target. Floating lanes (at most 100.75 in
                # magnitude) convert to signed integer lanes only: a negative
                # value converted to an unsigned lane is undefined.
                same_n = [t for t in allv if t.n == v.n and t.name != v.name
                          and not (v.flt and not t.flt and not t.signed)]
                if same_n:
                    t = random.choice(same_n)
                    lines.append('{ %s c = __builtin_convertvector(x, %s); hb(&c, sizeof c); }' % (t.name, t.name))
                else:
                    lines.append('z += y;')
            elif c == 12:
                same_size = [t for t in allv if t.size == v.size and t.name != v.name and not t.flt]
                if same_size and not v.flt:
                    t = random.choice(same_size)
                    lines.append('{ %s r = (%s)x; r ^= (%s)%s; z = (%s)r; }' % (t.name, t.name, t.lane, lane_val(t), v.name))
                else:
                    lines.append('z -= x;')
            elif c == 13:
                lines.append('z = pass_%s(x, y, ri);' % v.name)
            else:
                off = random.randrange(1, 32)
                lines.append('{ memcpy(RAW + %d, &x, sizeof x); struct box_%s bx; memset(&bx, 0, sizeof bx); memcpy(&bx.v, RAW + %d, sizeof bx.v); bx.arr[1] = bx.v + y; z = bx.arr[1] - bx.v; }'
                             % (off, v.name, off))
            lines.append('x = z;')
        lines.append('hb(&z, sizeof z); hb(&y, sizeof y); printf("t%d %%llu\\n", H); }' % k)
        body.append('    ' + ' '.join(l for l in lines if l))
    out.append('int main(void) {')
    out += body
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
        src = os.path.join(WORK, 'q%d_%d.c' % (SEED, it))
        open(src, 'w').write(program())
        rc, _, err = run([CLANG, '-O0', '-w', '-fsanitize=undefined', '-fno-sanitize-recover=all', src, '-o', src + '.ub'] + FLAGS)
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
        run([CLANG, '-w', OPT, src, '-o', src + '.c.bin'] + FLAGS)
        _, oc, _ = run([src + '.c.bin'], cap=True)
        rcg, _, _ = run([GCC, '-w', OPT, src, '-o', src + '.g.bin'] + FLAGS)
        og = run([src + '.g.bin'], cap=True)[1] if rcg == 0 else None
        rcx, _, errx = run([CCC, '-w', OPT, src, '-o', src + '.x.bin'] + FLAGS)
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
