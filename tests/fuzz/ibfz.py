"""Integer builtin fuzzer: __builtin_add/sub/mul_overflow with operands and
result of every integer type mixed (signed, unsigned, 8 to 128 bits: the
result is the infinitely precise value, wrapped, with the flag set when it
does not fit), the typed forms (__builtin_sadd_overflow, uaddll, ...), and
the bit builtins clz, ctz, clrsb, popcount, parity, ffs in int, long and
long long widths and bswap16/32/64. Arguments are constants (folded at
compile time) or volatile values (computed at run time), drawn from edge
values: 0, +-1, each type's limits, powers of two and their neighbours.
The overflow result is stored through a pointer to a local, a struct
member or an array element, and the flag is used as a value or branched on.
clang is the oracle; lines where gcc disagrees are dropped.

usage: ibfz.py CCC CLANG WORK SEED COUNT   (env OPT, default -O2)
"""
import os, random, resource, subprocess, sys, tempfile

CCC, CLANG, WORK, SEED, COUNT = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]), int(sys.argv[5])
OPT = os.environ.get('OPT', '-O2')
GCC = os.environ.get('GCC', '/opt/homebrew/bin/gcc-15')
os.makedirs(WORK, exist_ok=True)

# name -> (bits, signed, C spelling, short tag)
TYPES = [
    (8, True, 'signed char', 'sc'), (8, False, 'unsigned char', 'uc'),
    (16, True, 'short', 'ss'), (16, False, 'unsigned short', 'us'),
    (32, True, 'int', 'si'), (32, False, 'unsigned', 'ui'),
    (64, True, 'long', 'sl'), (64, False, 'unsigned long', 'ul'),
    (64, True, 'long long', 'sx'), (64, False, 'unsigned long long', 'ux'),
    (128, True, '__int128', 's8'), (128, False, 'unsigned __int128', 'u8'),
]

HDR = r'''#include <stdio.h>
typedef unsigned long long UX;
typedef unsigned __int128 U8;
static void p128(const char *tag, U8 v) { printf("%s %016llx%016llx\n", tag, (UX)(v >> 64), (UX)v); }
'''

def rng(t):
    bits, signed = t[0], t[1]
    return (-(1 << (bits - 1)), (1 << (bits - 1)) - 1) if signed else (0, (1 << bits) - 1)

def edge(t):
    lo, hi = rng(t)
    bits = t[0]
    k = random.randrange(bits)
    cands = [0, 1, lo, hi, lo + 1, hi - 1, 1 << k, (1 << k) - 1, (1 << k) + 1, -(1 << k), -(1 << k) - 1,
             -1, 2, -2, random.randint(lo, hi), random.randint(-1000, 1000), (1 << (bits // 2)), (1 << (bits // 2)) - 1]
    return min(hi, max(lo, random.choice(cands)))

def lit(t, v):
    """A constant expression of type t with value v."""
    bits, signed, name, _ = t
    if bits == 128:
        u = v & ((1 << 128) - 1)
        return '((%s)(((U8)0x%xULL << 64) | 0x%xULL))' % (name, u >> 64, u & ((1 << 64) - 1))
    if v < 0:
        return '((%s)(-%dLL - 1))' % (name, -v - 1)
    return '((%s)%dULL)' % (name, v)

def operand(t, v, i):
    """Constant, or the same value read back through a volatile."""
    if random.random() < 0.5:
        return lit(t, v), None
    return 'v%d' % i, 'volatile %s v%d = %s;' % (t[2], i, lit(t, v))

def show(tag, t, expr):
    bits, signed, name, _ = t
    if bits == 128:
        return 'p128("%s", (U8)(%s));' % (tag, expr)
    if signed:
        return 'printf("%s %%lld\\n", (long long)(%s));' % (tag, expr)
    return 'printf("%s %%llu\\n", (UX)(%s));' % (tag, expr)

TYPED = {  # builtin -> (operand and result type tag)
    'sadd': 'si', 'saddl': 'sl', 'saddll': 'sx', 'uadd': 'ui', 'uaddl': 'ul', 'uaddll': 'ux',
    'ssub': 'si', 'ssubl': 'sl', 'ssubll': 'sx', 'usub': 'ui', 'usubl': 'ul', 'usubll': 'ux',
    'smul': 'si', 'smull': 'sl', 'smulll': 'sx', 'umul': 'ui', 'umull': 'ul', 'umulll': 'ux',
}
BYTAG = {t[3]: t for t in TYPES}

def overflow_test(n, body):
    if random.random() < 0.25:
        b = random.choice(list(TYPED))
        ta = tb = tr = BYTAG[TYPED[b]]
        call = '__builtin_%s_overflow' % b
    else:
        ta, tb, tr = random.choice(TYPES), random.choice(TYPES), random.choice(TYPES)
        call = '__builtin_%s_overflow' % random.choice(['add', 'sub', 'mul'])
    a, da = operand(ta, edge(ta), 2 * n)
    b_, db = operand(tb, edge(tb), 2 * n + 1)
    lines = ['{'] + [d for d in (da, db) if d]
    where = random.randrange(3)
    if where == 0:
        lines.append('%s r = 0;' % tr[2]); dst, val = '&r', 'r'
    elif where == 1:
        lines.append('struct { char c; %s m; short s; } r = {0};' % tr[2]); dst, val = '&r.m', 'r.m'
    else:
        lines.append('%s r[3] = {0};' % tr[2]); dst, val = '&r[1]', 'r[1]'
    use = random.randrange(3)
    if use == 0:
        lines.append('int o = %s(%s, %s, %s);' % (call, a, b_, dst))
    elif use == 1:
        lines.append('int o = 0; if (%s(%s, %s, %s)) o = 1;' % (call, a, b_, dst))
    else:
        lines.append('int o = !%s(%s, %s, %s) ? 0 : 1;' % (call, a, b_, dst))
    lines.append('printf("o%d %%d ", o);' % n)
    lines.append(show('r%d' % n, tr, val))
    lines.append('}')
    body.append('    ' + ' '.join(lines))

BITS = [  # builtin, argument type tag, needs a nonzero argument
    ('clz', 'ui', True), ('clzl', 'ul', True), ('clzll', 'ux', True),
    ('ctz', 'ui', True), ('ctzl', 'ul', True), ('ctzll', 'ux', True),
    ('clrsb', 'si', False), ('clrsbl', 'sl', False), ('clrsbll', 'sx', False),
    ('popcount', 'ui', False), ('popcountl', 'ul', False), ('popcountll', 'ux', False),
    ('parity', 'ui', False), ('parityl', 'ul', False), ('parityll', 'ux', False),
    ('ffs', 'si', False), ('ffsl', 'sl', False), ('ffsll', 'sx', False),
    ('bswap16', 'us', False), ('bswap32', 'ui', False), ('bswap64', 'ux', False),
]

def bit_test(n, body):
    name, tag, nonzero = random.choice(BITS)
    t = BYTAG[tag]
    v = edge(t)
    if nonzero and v == 0:
        v = 1
    # The argument is sometimes a narrower or wider value converted at the call.
    a, d = operand(t, v, 2 * n)
    rt = t if name.startswith('bswap') else BYTAG['si']
    body.append('    { %s %s }' % (d or '', show('b%d %s' % (n, name), rt, '__builtin_%s(%s)' % (name, a))))

def program():
    body = []
    for n in range(random.randint(20, 60)):
        (overflow_test if random.random() < 0.6 else bit_test)(n, body)
    return HDR + 'int main(void) {\n' + '\n'.join(body) + '\n    return 0;\n}\n'

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
        src = os.path.join(WORK, 'b%d_%d.c' % (SEED, it))
        open(src, 'w').write(program())
        rc, _, err = run([CLANG, '-w', OPT, src, '-o', src + '.c.bin'])
        if rc != 0:
            skipped += 1
            print('clang-fail', src, err[:300])
            continue
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
