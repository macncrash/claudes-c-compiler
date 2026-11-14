#!/usr/bin/env python3
"""setjmp/longjmp fuzzer.  Functions take many non-volatile locals (int,
long, double, float, struct, array, int128), computed before setjmp and
never changed after it, plus volatile locals and globals that are changed
after it; then setjmp in if/switch/!/== forms (setjmp, _setjmp, sigsetjmp),
heavy work with new temps in the first-return arm (to reuse registers and
stack slots of locals that are dead on that arm but live on the
longjmp-return arm), longjmp from the same function, from nested callees,
through intermediate frames that have their own setjmp, with value 0, and
repeatedly (loops through setjmp); VLAs and alloca in the frames.
Only defined behaviour: a non-volatile local is never changed between a
setjmp and its longjmp.  Programs on which clang -O0 and -O2 disagree are
skipped.  usage: sjfz.py CCC CLANG WORK SEED COUNT   (env OPT, default -O2)"""
import os
import random
import subprocess
import sys

CCC, CLANG, WORK, SEED, COUNT = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]), int(sys.argv[5])
OPT = os.environ.get('OPT', '-O2')
os.makedirs(WORK, exist_ok=True)

HDR = r'''#include <stdio.h>
#include <setjmp.h>
#include <alloca.h>
#include <string.h>
typedef unsigned long u64;
struct P { int a; long b; double c; };
static u64 mix(u64 h, u64 v) { return (h ^ v) * 1099511628211UL; }
static jmp_buf JB[8];
static sigjmp_buf SJ[8];
static int gi; static long gl; static double gd;
static volatile int sink;
__attribute__((noinline)) static double fpnoise(int n) {
  double a = n, b = 1, c = 2, d = 3, e = 5, f = 7, g = 9, h = 11;
  for (int i = 0; i < n; i++) { a = a + b * 2; b = c + d; c = e - f; d = g + h * 3; e = a - c; f = b + 1; g = d - e; h = f + g;
    if (a > 1e6 || a < -1e6) a = 1; if (b > 1e6 || b < -1e6) b = 2; if (c > 1e6 || c < -1e6) c = 4; if (d > 1e6 || d < -1e6) d = 7;
    if (e > 1e6 || e < -1e6) e = 5; if (f > 1e6 || f < -1e6) f = 6; if (g > 1e6 || g < -1e6) g = 1; if (h > 1e6 || h < -1e6) h = 3; }
  return a + b + c + d + e + f + g + h;
}
__attribute__((noinline)) static u64 inoise(u64 n) {
  u64 a = n, b = 3, c = 5, d = 7, e = 11, f = 13, g = 17, h = 19, i2 = 23, j = 29;
  for (int i = 0; i < (int)(n & 15); i++) { a += b * c; b ^= d + e; c -= f * g; d += h ^ i2; e = e * 3 + j; f ^= a; g += b; h -= c; i2 ^= d; j += e; }
  return (a ^ b ^ c ^ d ^ e ^ f ^ g ^ h ^ i2 ^ j) & 0xffffff;
}
static u64 fh(double x) { if (x != x) return 7; if (x > 1e15 || x < -1e15) return 9; return (u64)(long)(x * 8); }
'''

JMPS = [('setjmp(JB[%d])', 'longjmp(JB[%d], %s)', 4), ('_setjmp(JB[%d])', '_longjmp(JB[%d], %s)', 4),
        ('sigsetjmp(SJ[%d], 0)', 'siglongjmp(SJ[%d], %s)', 2), ('sigsetjmp(SJ[%d], 1)', 'siglongjmp(SJ[%d], %s)', 2)]

TYS = ['int', 'long', 'double', 'float', 'unsigned', 'short', 'char', '__int128', 'struct P', 'arr']


def hexpr(ty, e):
    if ty in ('float', 'double'):
        return 'fh(%s)' % e
    if ty == '__int128':
        return '((u64)(%s) ^ (u64)((%s) >> 64))' % (e, e)
    if ty == 'struct P':
        return '((u64)%s.a ^ (u64)%s.b << 5 ^ (u64)(long)(%s.c * 4))' % (e, e, e)
    return '(u64)(%s)' % e


def gen(r):
    out = [HDR]
    nfn = r.randint(2, 5)
    # throwers: f_thr_i(depth, which, val) recurses then longjmps
    out.append('__attribute__((noinline)) static void thr(int d, int w, int v) {')
    out.append('  volatile long pad[%d]; for (int i = 0; i < %d; i++) pad[i] = d * i;' % ((r.randint(1, 8),) * 2))
    out.append('  sink += (int)fpnoise(d + 3) + (int)inoise(d * 7);')
    out.append('  if (d > 0) { thr(d - 1, w, v); return; }')
    out.append('  if (w < 8) longjmp(JB[w], v); else siglongjmp(SJ[w - 8], v);')
    out.append('}')
    out.append('__attribute__((noinline)) static void thr_vla(int n, int w, int v) {')
    out.append('  int a[n + 1]; for (int i = 0; i <= n; i++) a[i] = i * v; sink += a[n];')
    out.append('  char *q = alloca(n * 8 + 16); memset(q, n, n * 8 + 16); sink += q[n];')
    out.append('  thr(n & 3, w, v);')
    out.append('}')
    fnames = []
    for fi in range(nfn):
        fn = 'f%d' % fi
        sjf, ljf, nb = r.choice(JMPS)
        buf = fi
        widx = buf if nb == 4 else buf + 8
        sj = sjf % buf

        def lj(v):
            return ljf % (buf, v)
        L = []
        L.append('%sstatic u64 %s(int a, int b, long *cp) {' % (r.choice(['__attribute__((noinline)) ', '', 'inline ', '__attribute__((always_inline)) inline ']), fn))
        L.append('  volatile u64 h = 0;')
        nv = []
        for i in range(r.randint(4, 24)):
            ty = r.choice(TYS)
            nm = 'n%d' % i
            src = r.choice(['a', 'b', '(a * %d + b)' % r.randint(1, 99), '(b ^ %d)' % r.randint(0, 999), '(a - b * 3)', str(r.randint(-999, 999))]
                           + ['n%d' % j for j in range(max(0, i - 3), i) if nv[j][1] in ('int', 'long', 'unsigned', 'short', 'char')])
            if ty == 'arr':
                ln = r.randint(1, 6)
                L.append('  int %s[%d]; for (int i = 0; i < %d; i++) %s[i] = %s + i * %d;' % (nm, ln, ln, nm, src, r.randint(1, 9)))
                nv.append((nm, 'arr', ln))
            elif ty == 'struct P':
                L.append('  struct P %s = { %s, (long)%s * 8, %s * 0.5 };' % (nm, src, src, src))
                nv.append((nm, ty, 0))
            elif ty == '__int128':
                L.append('  __int128 %s = (__int128)%s * 1000000007L * 1000000009L;' % (nm, src))
                nv.append((nm, ty, 0))
            else:
                L.append('  %s %s = (%s)(%s);' % (ty, nm, ty, src))
                nv.append((nm, ty, 0))
        vla = r.random() < 0.3
        if vla:
            L.append('  int vl[(a & 7) + 2]; for (int i = 0; i < (a & 7) + 2; i++) vl[i] = i * b;')
        alc = r.random() < 0.3
        if alc:
            L.append('  long *al = alloca(((b & 7) + 1) * sizeof(long)); for (int i = 0; i <= (b & 7); i++) al[i] = a + i;')
        vols = []
        for i in range(r.randint(1, 4)):
            ty = r.choice(['int', 'long', 'double'])
            L.append('  volatile %s v%d = %d;' % (ty, i, r.randint(-50, 50)))
            vols.append(('v%d' % i, ty))
        loop = r.random() < 0.25
        if loop:
            L.append('  for (int it = 0; it < %d; it++) {' % r.randint(1, 4))
        # first-return arm: heavy work, modify volatiles/globals, maybe jump
        arm0 = []
        for i in range(r.randint(3, 20)):
            c = r.random()
            if c < 0.3:
                vn, vt = r.choice(vols)
                arm0.append('%s = %s + %s;' % (vn, vn, r.choice(['a', 'b', '1', '(a ^ b)'])))
            elif c < 0.45:
                arm0.append(r.choice(['gi += a;', 'gl ^= (long)b * 128;', 'gd += a * 0.25;', '*cp += a;', '*cp = *cp * 3 + b;', 'cp[0] ^= (long)a * 64;']))
            elif c < 0.75:
                ty = r.choice(['u64', 'double', 'unsigned'])
                arm0.append('{ %s t%d = (%s)(a * %d + b) + (%s)inoise(a + %d); %s t%db = t%d * 3 - b; h = mix(h, %s); h = mix(h, %s); }'
                            % (ty, i, ty, r.randint(1, 50), ty, i, ty, i, i, hexpr(ty, 't%d' % i), hexpr(ty, 't%db' % i)))
            else:
                arm0.append('h = mix(h, fh(fpnoise(%d)) ^ (u64)inoise(b + %d));' % (r.randint(1, 20), i))
        v = r.choice(['0', '1', '2', str(r.randint(3, 9)), '(int)v0 & 0', 'a & 3'])
        how = r.random()
        if how < 0.3:
            jump = lj(v) + ';'
        elif how < 0.55:
            jump = 'thr(%d, %d, %s);' % (r.randint(0, 4), widx, v)
        elif how < 0.7:
            jump = 'thr_vla((a & 15) + 1, %d, %s);' % (widx, v)
        elif how < 0.85 and fnames:
            # call an earlier function that has its own setjmp, then jump
            jump = 'h = mix(h, %s(a + 1, b - 2, cp)); thr(1, %d, %s);' % (r.choice(fnames), widx, v)
        else:
            jump = 'thr(1, %d, %s);' % (widx, v)
        rep = r.random() < 0.4
        if rep:
            # loop via longjmp: a volatile counter decides when to stop
            jump = 'if (v0 < %d) { %s }' % (r.randint(1, 5) + 50, jump)
            arm0.append('v0 = v0 + 1;')
        else:
            jump = 'if ((a ^ b) & %d) { %s }' % (r.choice([0, 1, 3, 255]), jump)
        arm0.append(jump)
        arm0.append('h = mix(h, 77);')
        # longjmp-return arm: read non-volatile locals and volatiles
        arm1 = []
        for nm, ty, ln in r.sample(nv, r.randint(1, len(nv))):
            if ty == 'arr':
                arm1.append('for (int i = 0; i < %d; i++) h = mix(h, (u64)%s[i]);' % (ln, nm))
            else:
                arm1.append('h = mix(h, %s);' % hexpr(ty, nm))
        for vn, vt in vols:
            arm1.append('h = mix(h, %s);' % hexpr(vt, vn))
        if vla:
            arm1.append('for (int i = 0; i < (a & 7) + 2; i++) h = mix(h, (u64)vl[i]);')
        if alc:
            arm1.append('for (int i = 0; i <= (b & 7); i++) h = mix(h, (u64)al[i]);')
        if rep:
            # the longjmp-return arm loops back to the first-return arm by jumping again
            pass
        form = r.randrange(4)
        ind = '    ' if loop else '  '
        if form == 0:
            L.append(ind + 'if (%s == 0) { %s } else { h = mix(h, 1001); %s }' % (sj, ' '.join(arm0), ' '.join(arm1)))
        elif form == 1:
            L.append(ind + 'if (!%s) { %s } else { %s }' % (sj, ' '.join(arm0), ' '.join(arm1)))
        elif form == 2:
            L.append(ind + 'switch (%s) { case 0: %s break; case 1: h = mix(h, 1); %s break; case 2: h = mix(h, 2); %s break; default: h = mix(h, 3); %s }'
                     % (sj, ' '.join(arm0), ' '.join(arm1), ' '.join(arm1[:1]), ' '.join(arm1[-1:])))
        else:
            L.append(ind + 'if (%s != 0) { h = mix(h, 5); %s } else { %s }' % (sj, ' '.join(arm1), ' '.join(arm0)))
        if loop:
            L.append('    h = mix(h, (u64)it);')
            L.append('  }')
        # after both arms: non-volatile locals are still valid
        for nm, ty, ln in r.sample(nv, r.randint(0, min(4, len(nv)))):
            if ty != 'arr':
                L.append('  h = mix(h, %s);' % hexpr(ty, nm))
        L.append('  return h;')
        L.append('}')
        out += L
        fnames.append(fn)
    out.append('int main(void) {')
    out.append('  u64 h0 = 0;')
    out.append('  long c0 = 1, c1 = 2, c2 = 3;')
    out.append('  for (int it = 0; it < %d; it++) {' % r.randint(1, 3))
    for fn in fnames:
        if r.random() < 0.6:
            out.append('    h0 = mix(h0, %s(it * %d - %d, %d, &c%d));' % (fn, r.randint(1, 9), r.randint(0, 50), r.randint(-99, 99), r.randrange(3)))
    out.append('  }')
    out.append('  printf("loop %lx %ld %ld %ld\\n", h0, c0, c1, c2);')
    for fn in fnames:
        for _ in range(r.randint(1, 3)):
            out.append('  printf("%s %%lx\\n", %s(%d, %d, &c%d));' % (fn, fn, r.randint(-99, 99), r.randint(-99, 99), r.randrange(3)))
    out.append('  printf("g %d %ld %ld %ld %ld %ld\\n", gi, gl, (long)(gd * 4), c0, c1, c2);')
    out.append('  return 0;')
    out.append('}')
    return '\n'.join(out) + '\n'


def run(cmd, timeout=60):
    try:
        return subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return None


bad = skipped = 0
for t in range(COUNT):
    r = random.Random(SEED * 100003 + t)
    f = os.path.join(WORK, 'sj%d_%d.c' % (SEED, t))
    with open(f, 'w') as fh:
        fh.write(gen(r))
    outs = []
    ok = True
    for o in ('-O0', '-O2'):
        cmd = [CLANG, o, '-w', f, '-o', f + '.cl']
        if o == '-O0':
            cmd[3:3] = ['-fsanitize=undefined', '-fno-sanitize-recover=all']
        c = run(cmd)
        if c is None or c.returncode != 0:
            ok = False
            print('clang-fail', f, (c.stderr[:300] if c else 'timeout'))
            break
        u = run([f + '.cl'])
        os.remove(f + '.cl')
        if u is None or u.returncode != 0:
            ok = False
            print('skip', f, (u.stderr[:200] if u else 'timeout'))
            break
        outs.append(u.stdout)
    if not ok or outs[0] != outs[1]:
        if ok:
            print('clang-disagree', f)
        skipped += 1
        continue
    cc = run([CCC, OPT, '-w', f, '-o', f + '.ccc'])
    if cc is None or cc.returncode != 0:
        print('ccc-fail', f, (cc.stderr[:300] if cc else 'timeout'))
        bad += 1
        continue
    b = run([f + '.ccc'])
    if b is None or b.returncode != 0 or outs[0] != b.stdout:
        print('MISMATCH', f)
        bad += 1
    else:
        os.remove(f + '.ccc')
        os.remove(f)
print('done', COUNT, 'bad', bad, 'skipped', skipped)
