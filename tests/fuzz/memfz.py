"""Memory access and aliasing fuzzer: random statements over a few byte
buffers (static, automatic, heap) seen through a union of every access
width (u8/u16/u32/u64, float, double, a struct view): mixed-width stores
and loads of the same bytes, char * writes into wider objects, unaligned
memcpy loads and stores, partial memset, overlapping memmove, overlapping
copy loops (forward and backward), struct-view assignments, aliasing
pointers into the same buffer passed to noinline functions, volatile
accesses. All defined C (type punning only through the union, char and
memcpy). After each block the buffers' bytes and the loaded values are
hashed and printed. Programs UBSan or ASan flag are skipped; lines where
gcc disagrees with clang are dropped.

usage: memfz.py CCC CLANG WORK SEED COUNT   (env OPT, default -O2)
"""
import os, random, subprocess, sys

CCC, CLANG, WORK, SEED, COUNT = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]), int(sys.argv[5])
OPT = os.environ.get('OPT', '-O2')
GCC = os.environ.get('GCC', '/opt/homebrew/bin/gcc-15')
os.makedirs(WORK, exist_ok=True)

HDR = r'''#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
struct SV { uint8_t c; uint16_t h; uint32_t w; uint64_t d; };
union U {
    uint8_t b[64]; uint16_t h[32]; uint32_t w[16]; uint64_t d[8];
    float f[16]; double g[8]; struct SV s[2];
};
static union U G0, G1;
static uint64_t H = 1469598103934665603ULL;
static void mix(uint64_t v) { H = (H ^ v) * 1099511628211ULL; }
static void mixbuf(const union U *u) { for (int i = 0; i < 64; i++) mix(u->b[i]); }
static volatile int VZ = 0;
__attribute__((noinline)) static void wr32(uint32_t *p, uint8_t *q, uint32_t v) { *p = v; q[1] ^= (uint8_t)(v >> 3); *p += q[0]; }
__attribute__((noinline)) static uint64_t rd_mixed(const uint16_t *h, const uint8_t *b, const uint64_t *d) { return (uint64_t)h[0] * 31 + b[3] + (d[0] >> 7); }
__attribute__((noinline)) static void cp_fwd(uint8_t *dst, const uint8_t *src, int n) { for (int i = 0; i < n; i++) dst[i] = src[i] + 1; }
__attribute__((noinline)) static void cp_bwd(uint8_t *dst, const uint8_t *src, int n) { for (int i = n - 1; i >= 0; i--) dst[i] = src[i] ^ 0x5a; }
__attribute__((noinline)) static void sw(uint32_t *a, uint32_t *b) { uint32_t t = *a; *a = *b + 1; *b = t; }
/* One typed pointer and char access only: a uint32_t * and a uint64_t * to
 * the same bytes would break strict aliasing (ccc's type-based alias
 * analysis rightly keeps a load of *d across a store to *w). */
static inline uint64_t in_rw(uint32_t *w, uint8_t *b, const uint8_t *d) {
    uint64_t r, s; memcpy(&r, d, 8); *w += 0x01010101u; r += *w; b[0] = (uint8_t)r;
    memcpy(&s, d, 8); r += s; *w ^= b[2]; return r + *w;
}
'''

BUFS = ['G0', 'G1', 'L0', 'L1', '(*P0)']
WIDTH = {'b': (1, 64, 'uint8_t'), 'h': (2, 32, 'uint16_t'), 'w': (4, 16, 'uint32_t'), 'd': (8, 8, 'uint64_t')}

def R(xs):
    return random.choice(xs)

def val(bits):
    k = random.randrange(6)
    if k == 0:
        return str(random.randint(0, 2 ** bits - 1)) + 'ULL'
    if k == 1:
        return R(['0', '1', '0xff', '0x8000', '0xffffffffULL', '0x8000000000000000ULL', '0x0102030405060708ULL'])
    if k == 2:
        return 'acc'
    if k == 3:
        return '(acc * %d + %d)' % (random.randint(1, 99), random.randint(0, 999))
    if k == 4:
        return '(uint64_t)VZ + %d' % random.randint(0, 255)
    return 'acc >> %d' % random.randint(1, 40)

def access(buf):
    m = R(list(WIDTH))
    size, n, ty = WIDTH[m]
    return '%s.%s[%d]' % (buf, m, random.randrange(n)), size, ty

def alias_stmt(b):
    """Aliasing the optimizer must see through, in the function itself."""
    k = random.randrange(6)
    i = random.randrange(16)
    if k == 0:
        # A typed pointer and a char pointer into the same bytes.
        j = 4 * i + random.randrange(4)
        return ('{ uint32_t *p = &%s.w[%d]; uint8_t *q = %s.b + %d; *p = (uint32_t)(%s); *q = (uint8_t)(acc + 7); '
                'acc += *p; *q ^= 0x81; acc = acc * 5 + *p; }' % (b, i, b, j, val(32)))
    if k == 1:
        # Offsets known only at run time: VZ is a volatile 0.
        return ('{ int o = VZ + %d; %s.b[o] = (uint8_t)(acc >> 3); acc += %s.w[(o >> 2) %% 16]; '
                '%s.h[(o >> 1) %% 32] += (uint16_t)acc; acc += %s.d[(o >> 3) %% 8]; }' % (random.randrange(64), b, b, b, b))
    if k == 2:
        # A load in a loop whose bytes the loop writes: it must not be hoisted.
        n = random.randint(2, 9)
        return ('for (int i = 0; i < %d; i++) { acc += %s.w[%d]; %s.b[%d + (i & 3)] = (uint8_t)(i * 37 + acc); }'
                % (n, b, i, b, 4 * i))
    if k == 3:
        # Inlinable helpers taking aliasing pointers.
        return 'acc += in_rw(&%s.w[%d], %s.b + %d, %s.b + %d);' % (b, i, b, random.randrange(60), b, 8 * random.randrange(8))
    if k == 4:
        # Dead and live stores to the same bytes in a row.
        return ('{ %s.d[%d] = acc; %s.w[%d] = 1; %s.b[%d] = 2; %s.h[%d] = (uint16_t)(acc >> 9); acc += %s.d[%d]; }'
                % (b, i // 2, b, i, b, 4 * i + 1, b, 2 * i, b, i // 2))
    # Two pointers to the same union compared, then used.
    return ('{ union U *x = &%s, *y = &%s; x->w[%d] = (uint32_t)acc; y->b[%d] += 3; '
            'acc += x->w[%d] + (x == y); }' % (b, R(BUFS), i, 4 * i + random.randrange(4), i))

def stmt(depth=0):
    k = random.randrange(27)
    b, b2 = R(BUFS), R(BUFS)
    if k >= 22:
        return alias_stmt(b)
    if k < 4:
        a, size, ty = access(b)
        return '%s = (%s)(%s);' % (a, ty, val(size * 8))
    if k < 8:
        a, size, ty = access(b)
        return 'acc = acc * 3 + %s;' % a
    if k == 8:
        a, _, _ = access(b)
        return '%s ^= (__typeof__(%s))(acc | 1);' % (a, a)
    if k == 9:
        off, n = random.randrange(56), R([1, 2, 3, 4, 7, 8])
        return '{ uint64_t t = 0; memcpy(&t, %s.b + %d, %d); acc += t; }' % (b, off, n)
    if k == 10:
        off, n = random.randrange(56), R([1, 2, 3, 4, 5, 8])
        return '{ uint64_t t = acc * 0x9e3779b97f4a7c15ULL; memcpy(%s.b + %d, &t, %d); }' % (b, off, n)
    if k == 11:
        off = random.randrange(64)
        n = random.randint(1, 64 - off)
        return 'memset(%s.b + %d, %s, %d);' % (b, off, R(['0', '0xab', '(int)(acc & 255)']), n)
    if k == 12:
        d, s = random.randrange(40), random.randrange(40)
        n = random.randint(1, 24)
        return 'memmove(%s.b + %d, %s.b + %d, %d);' % (b, d, b, s, n)
    if k == 13:
        s = random.randrange(32)
        d = s + random.randint(-8, 8)
        d = min(max(d, 0), 32)
        n = random.randint(1, 31)
        return 'cp_fwd(%s.b + %d, %s.b + %d, %d);' % (b, d, b, s, n)
    if k == 14:
        s = random.randrange(32)
        d = min(max(s + random.randint(-8, 8), 0), 32)
        return 'cp_bwd(%s.b + %d, %s.b + %d, %d);' % (b, d, b, s, random.randint(1, 31))
    if k == 15:
        i, j = random.randrange(16), random.randrange(16)
        return 'wr32(&%s.w[%d], %s.b + %d, (uint32_t)(%s));' % (b, i, R([b, b2]), random.randrange(60), val(32))
    if k == 16:
        return 'acc += rd_mixed(&%s.h[%d], %s.b + %d, &%s.d[%d]);' % (b, random.randrange(32), b, random.randrange(60), R([b, b2]), random.randrange(8))
    if k == 17:
        return '%s.s[%d] = %s.s[%d];' % (b, random.randrange(2), b2, random.randrange(2))
    if k == 18:
        return '{ struct SV t = %s.s[%d]; t.h += (uint16_t)acc; %s.s[%d] = t; acc += t.w; }' % (b, random.randrange(2), b2, random.randrange(2))
    if k == 19:
        return 'sw(&%s.w[%d], &%s.w[%d]);' % (b, random.randrange(16), R([b, b2]), random.randrange(16))
    if k == 20 and depth < 2:
        n = random.randint(2, 12)
        a, size, ty = access(b)
        m = a.split('.')[1].split('[')[0]
        cap = WIDTH[m][1]
        body = ['for (int i = 0; i < %d; i++) {' % n,
                '  %s.%s[(i * %d + %d) %% %d] += (%s)(%s.%s[(i + %d) %% %d] + i);' % (b, m, random.randint(1, 5), random.randrange(cap), cap, WIDTH[m][2], b2, m, random.randrange(cap), cap),
                '  ' + stmt(depth + 1), '}']
        return '\n    '.join(body)
    if k == 21:
        # Float views: store a computed value, read the bits back as integers.
        if random.random() < 0.5:
            return '{ %s.f[%d] = (float)(acc %% 1000) / 8.0f; acc += %s.w[%d]; }' % (b, random.randrange(16), b, random.randrange(16))
        return '{ %s.g[%d] = (double)(acc %% 100000) * 0.125; acc += %s.d[%d]; }' % (b, random.randrange(8), b, random.randrange(8))
    a, size, ty = access(b)
    return '%s = (%s)acc;' % (a, ty)

def program():
    out = [HDR, 'int main(void) {',
           '    static union U L0s; union U L1; union U *P0 = malloc(sizeof *P0);',
           '    memset(&L1, 0x11, sizeof L1); memset(P0, 0x22, sizeof *P0);',
           '    for (int i = 0; i < 64; i++) { G0.b[i] = (uint8_t)(i * 7 + 1); G1.b[i] = (uint8_t)(255 - i); L0s.b[i] = (uint8_t)(i ^ 0x3c); }',
           '#define L0 L0s',
           '    uint64_t acc = 0x1234;']
    for blk in range(random.randint(4, 8)):
        out.append('    {')
        for _ in range(random.randint(4, 12)):
            out.append('    ' + stmt())
        out.append('    mixbuf(&G0); mixbuf(&G1); mixbuf(&L0); mixbuf(&L1); mixbuf(P0); mix(acc);')
        out.append('    printf("%%d %%016llx %%016llx\\n", %d, (unsigned long long)H, (unsigned long long)acc);' % blk)
        out.append('    }')
    out += ['    free(P0);', '    return 0;', '}']
    return '\n'.join(out) + '\n'

def run(cmd, timeout=60):
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
        return p.returncode, p.stdout, p.stderr
    except subprocess.TimeoutExpired:
        return -1, '', 'timeout'

def main():
    bad = skipped = 0
    for it in range(COUNT):
        random.seed(SEED * 100003 + it)
        src = os.path.join(WORK, 'm%d_%d.c' % (SEED, it))
        open(src, 'w').write(program())
        rc, _, err = run([CLANG, '-O0', '-w', '-fsanitize=undefined,address', '-fno-sanitize-recover=all', src, '-o', src + '.ub'])
        if rc != 0:
            skipped += 1
            print('clang-fail', src, err[:200])
            continue
        rc, _, err = run([src + '.ub'])
        os.remove(src + '.ub')
        if rc != 0:
            skipped += 1
            print('ub-skip', src, err[:200])
            continue
        rc, _, _ = run([CLANG, '-w', OPT, src, '-o', src + '.c.bin'])
        _, oc, _ = run([src + '.c.bin'])
        rcg, _, _ = run([GCC, '-w', OPT, src, '-o', src + '.g.bin'])
        og = run([src + '.g.bin'])[1] if rcg == 0 else None
        rcx, _, errx = run([CCC, '-w', OPT, src, '-o', src + '.x.bin'])
        if rcx != 0:
            bad += 1
            print('FAIL-BUILD', src, errx[:300])
            continue
        rx, ox, _ = run([src + '.x.bin'])
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
