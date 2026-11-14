"""Wide/UTF string literal fuzzer: L"", u"", U"", u8"" and plain literals in
array, struct, nested-array, pointer and compound-literal initializers, global,
static-local and automatic; every element of every object is printed in hex.
Lines clang rejects are dropped; lines gcc disagrees on are ignored.

usage: wsfz.py CCC CLANG WORK SEED COUNT   (env OPT, default -O1)
"""
import os, random, re, subprocess, sys

CCC, CLANG, WORK, SEED, COUNT = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]), int(sys.argv[5])
OPT = os.environ.get('OPT', '-O1')
GCC = os.environ.get('GCC', '/opt/homebrew/bin/gcc-15')
os.makedirs(WORK, exist_ok=True)

# element type -> prefixes it may be initialized from
ELEMS = {
    'wchar_t': ['L'], 'char16_t': ['u'], 'char32_t': ['U'],
    'char': ['', 'u8'], 'unsigned char': ['', 'u8'], 'signed char': ['', 'u8'],
    'int': ['L'], 'unsigned short': ['u'], 'unsigned int': ['U'],
}
UNITS = {'wchar_t': 4, 'char16_t': 2, 'char32_t': 4, 'char': 1, 'unsigned char': 1, 'signed char': 1,
         'int': 4, 'unsigned short': 2, 'unsigned int': 4}

PIECES = ['a', 'b', 'Z', '0', ' ', '\\n', '\\t', '\\0', '\\\\', '\\"', "\\'", '\\x41', '\\101', '\\7', '\\x7f',
          '\\u00e9', '\\u20ac', '\\U0001F600', '\\U00010000', '\\uFFFD', '\\u0100', '\\x0', '\\1234',
          'é', '€', '\U0001F600', 'ÿ', '\\xff', '\\xFFFF', '\\x10FFFF', '\\xd800', '\\x80',
          '\\377', '\\ud7ff', '\\U0010FFFF', '?', '\\?', '%', 'xyz']

def piece(prefix):
    p = random.choice(PIECES)
    # a 32-bit literal holds chars, so a lone surrogate is not kept (known limit)
    if prefix in ('L', 'U') and p == '\\xd800':
        p = '\\xd7ff'
    return p

def body(prefix, n):
    out = []
    for _ in range(n):
        p = piece(prefix)
        if out and re.search(r'\\x[0-9a-fA-F]+$', out[-1]) and re.match(r'[0-9a-fA-F]', p):
            p = 'g' + p
        if out and re.search(r'\\[0-7]{1,2}$', out[-1]) and re.match(r'[0-7]', p):
            p = 'g' + p
        out.append(p)
    return ''.join(out)

def lit(prefix, maxn=6):
    k = random.randrange(4)
    if k == 0:
        return '%s"%s"' % (prefix, body(prefix, random.randrange(0, maxn)))
    # concatenation: one prefix and plain pieces, or the same prefix twice
    parts = []
    for i in range(random.randrange(2, 4)):
        pf = prefix if (i == 0 or random.random() < 0.5) else ''
        parts.append('%s"%s"' % (pf, body(prefix, random.randrange(0, 4))))
    random.shuffle(parts)
    return ' '.join(parts)

def size_spec():
    return random.choice(['', '', str(random.randrange(1, 12)), str(random.randrange(1, 4)), '20'])

DECL = 0

def decl(scope):
    """Returns (declaration text, printing statements)."""
    global DECL
    DECL += 1
    n = 'v%d' % DECL
    et = random.choice(list(ELEMS))
    pf = random.choice(ELEMS[et])
    st = {'global': '', 'static': 'static ', 'auto': ''}[scope]
    k = random.randrange(9)
    if k <= 2:
        return ('%s%s %s[%s] = %s;' % (st, et, n, size_spec(), lit(pf)), 'PA(%s);' % n)
    if k == 3:
        return ('%s%s %s[%s] = {%s};' % (st, et, n, size_spec(), lit(pf)), 'PA(%s);' % n)
    if k == 4:
        rows = random.randrange(1, 4)
        inits = ', '.join(random.choice([lit(pf, 4), '{%s}' % lit(pf, 4), '[%d] = %s' % (random.randrange(rows + 1), lit(pf, 4))]) for _ in range(random.randrange(1, rows + 1)))
        return ('%s%s %s[%s][%d] = {%s};' % (st, et, n, random.choice(['', str(rows + 1)]), random.randrange(1, 8), inits), 'PA(%s);' % n)
    if k == 5:
        et2 = random.choice(list(ELEMS))
        pf2 = random.choice(ELEMS[et2])
        f1, f2 = random.randrange(1, 9), random.randrange(1, 9)
        init = random.choice(['{%d, %s, %s}' % (DECL, lit(pf, 4), lit(pf2, 4)),
                              '{.b = %s, .c = %s}' % (lit(pf, 4), lit(pf2, 4)),
                              '{%d, {%s}, %s}' % (DECL, lit(pf, 4), lit(pf2, 4)),
                              '{.c = %s, .a = 3}' % lit(pf2, 4)])
        return ('%sstruct { short a; %s b[%d]; %s c[%d]; } %s = %s;' % (st, et, f1, et2, f2, n, init), 'PA(%s.b); PA(%s.c); PV(%s.a);' % (n, n, n))
    if k == 6:
        cq = 'const ' if random.random() < 0.7 else ''
        return ('%s%s%s *%s = %s;' % (st, cq, et, n, lit(pf)), 'PP(%s);' % n)
    if k == 7:
        cnt = random.randrange(1, 4)
        return ('%sconst %s *%s[] = {%s};' % (st, et, n, ', '.join(lit(pf, 4) for _ in range(cnt))), 'for (unsigned i_ = 0; i_ < sizeof %s / sizeof *%s; i_++) PP(%s[i_]);' % (n, n, n))
    if scope == 'auto':
        return ('%s *%s = (%s[%s]){%s};' % (et, n, et, size_spec() or '', lit(pf)), 'PN(%s, 1);' % n)
    return ('static const unsigned long %s = sizeof(%s) * 100 + sizeof(%s[0]);' % (n, lit(pf), lit(pf)), 'PV(%s);' % n)

HDR = r'''#include <stdio.h>
#include <wchar.h>
typedef __CHAR16_TYPE__ char16_t;
typedef __CHAR32_TYPE__ char32_t;
static void pbytes(const char *name, const void *p, unsigned long n, unsigned unit) {
  printf("%s %lu:", name, n);
  const unsigned char *b = p;
  for (unsigned long i = 0; i < n; i += unit) {
    unsigned long v = 0;
    for (unsigned k = 0; k < unit; k++) v |= (unsigned long)b[i + k] << (8 * k);
    printf(" %lx", v);
  }
  printf("\n");
}
#define PA(a) pbytes(#a, (a), sizeof(a), sizeof(*(a)) > 4 ? 1 : (unsigned)sizeof(*(a)))
#define PP(p) do { const void *q_ = (p); unsigned u_ = sizeof(*(p)); unsigned long n_ = 0; \
  if (u_ == 1) while (((const unsigned char *)q_)[n_]) n_++; \
  else if (u_ == 2) while (((const unsigned short *)q_)[n_]) n_++; \
  else while (((const unsigned *)q_)[n_]) n_++; \
  pbytes(#p, q_, (n_ + 1) * u_, u_); } while (0)
#define PN(p, n) pbytes(#p, (p), (n) * sizeof(*(p)), (unsigned)sizeof(*(p)))
#define PV(x) printf("%s %lu\n", #x, (unsigned long)(x))
'''

def build(gl, st, au, path):
    out = [HDR]
    lines = {}
    for d, p in gl:
        lines[len(''.join(out).split('\n'))] = ('g', d)
        out.append(d + '\n')
    out.append('__attribute__((noinline)) static void f(void) {\n')
    for d, p in st + au:
        lines[len(''.join(out).split('\n'))] = ('l', d)
        out.append('  ' + d + '\n')
    for d, p in gl + st + au:
        out.append('  ' + p + '\n')
    out.append('}\nint main(void) { f(); f(); return 0; }\n')
    open(path, 'w', encoding='utf-8').write(''.join(out))
    return lines

def run(cmd):
    try:
        p = subprocess.run(cmd, capture_output=True, timeout=60)
        return p.returncode, p.stdout.decode('latin-1'), p.stderr.decode('latin-1')
    except subprocess.TimeoutExpired:
        return -1, '', 'timeout'

def main():
    global DECL
    bad = skipped = 0
    for it in range(COUNT):
        random.seed(SEED * 100003 + it)
        DECL = 0
        gl = [decl('global') for _ in range(12)]
        st = [decl('static') for _ in range(12)]
        au = [decl('auto') for _ in range(12)]
        src = os.path.join(WORK, 'w%d_%d.c' % (SEED, it))
        for attempt in range(8):
            lines = build(gl, st, au, src)
            rc, _, err = run([CLANG, '-std=c11', '-Wno-everything', '-Werror=excess-initializers', '-c', src, '-o', src + '.o'])
            if rc == 0:
                break
            badl = set(int(m) for m in re.findall(r':(\d+):\d+: error', err))
            badd = set(lines[l][1] for l in badl if l in lines)
            if not badd:
                gl = st = au = []
                continue
            gl = [x for x in gl if x[0] not in badd]
            st = [x for x in st if x[0] not in badd]
            au = [x for x in au if x[0] not in badd]
        else:
            skipped += 1
            continue
        rc, _, _ = run([CLANG, '-std=c11', '-w', OPT, src, '-o', src + '.c.bin'])
        rc2, _, _ = run([GCC, '-std=c11', '-w', OPT, src, '-o', src + '.g.bin'])
        rc_, oc, _ = run([src + '.c.bin'])
        og = run([src + '.g.bin'])[1] if rc2 == 0 else None
        if rc_ != 0:
            skipped += 1
            continue
        rc3, _, err3 = run([CCC, '-w', OPT, src, '-o', src + '.x.bin'])
        if rc3 != 0:
            bad += 1
            print('FAIL-BUILD', src, err3[:300])
            continue
        rcx, ox, _ = run([src + '.x.bin'])
        cl, xl = oc.splitlines(), ox.splitlines()
        gl_ = og.splitlines() if og else None
        diffs = []
        for k, a in enumerate(cl):
            x = xl[k] if k < len(xl) else '<missing>'
            if a != x and (gl_ is None or k >= len(gl_) or gl_[k] == a):
                diffs.append((a, x))
        if rcx != 0:
            diffs.append(('rc 0', 'rc %d' % rcx))
        if diffs:
            bad += 1
            with open(src + '.diff', 'w') as fh:
                for a, x in diffs:
                    fh.write('clang %s\nccc   %s\n' % (a, x))
            print('DIFF', src, len(diffs))
        for ext in ('.o', '.c.bin', '.g.bin', '.x.bin'):
            try: os.remove(src + ext)
            except OSError: pass
        if not diffs:
            os.remove(src)
    print('done %d bad %d skipped %d' % (COUNT, bad, skipped))

main()
