#!/usr/bin/env python3
"""arm64 inline asm operand fuzzer: asm statements with r/m/i constraints,
outputs (=r), read-write (+r), early clobbers (=&r), tied inputs ("0"),
%w/%x size modifiers, operands of every integer width (narrow values passed
to %w), memory operands on locals, globals and array elements, clobber lists
(registers, "cc", "memory"), asm in loops and with values live across it;
two outputs, clobbers of the registers ccc allocates, one variable as input
and output, outputs stored through pointers, and the arm_acle.h intrinsics.
Each asm does a simple add/eor/lsl/madd; results are hashed.
usage: asmfz.py CCC CLANG WORK SEED COUNT   (env OPT, default -O2)"""
import os
import random
import subprocess
import sys

CCC, CLANG, WORK, SEED, COUNT = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]), int(sys.argv[5])
OPT = os.environ.get('OPT', '-O2')
os.makedirs(WORK, exist_ok=True)

T64 = ['long', 'unsigned long', 'long long', 'unsigned long long']
T32 = ['int', 'unsigned']


def gen_stmt(r, k, vars64, vars32):
    c = r.randint(0, 14)
    a64, b64 = r.choice(vars64), r.choice(vars64)
    a32, b32 = r.choice(vars32), r.choice(vars32)
    if c == 0:
        return 'asm("add %%0, %%1, %%2" : "=r"(%s) : "r"(%s), "r"(%s));' % (a64, b64, r.choice(vars64))
    if c == 1:
        return 'asm("eor %%w0, %%w1, %%w2" : "=r"(%s) : "r"(%s), "r"(%s));' % (a32, b32, r.choice(vars32))
    if c == 2:
        return 'asm("add %%0, %%0, #%d" : "+r"(%s));' % (r.randint(0, 4095), a64)
    if c == 3:
        return 'asm("lsl %%w0, %%w1, %%2" : "=r"(%s) : "r"(%s), "i"(%d));' % (a32, b32, r.randint(0, 31))
    if c == 4:
        # early clobber: output written before the inputs are read
        return 'asm("mov %%0, #%d\\n\\tadd %%0, %%0, %%1\\n\\teor %%0, %%0, %%2" : "=&r"(%s) : "r"(%s), "r"(%s));' % (
            r.randint(0, 65535), a64, b64, r.choice(vars64))
    if c == 5:
        # tied input
        return 'asm("madd %%0, %%0, %%2, %%3" : "=r"(%s) : "0"(%s), "r"(%s), "r"(%s));' % (a64, b64, r.choice(vars64), r.choice(vars64))
    if c == 6:
        # memory operand: load and store through it
        m = r.choice(['g%d' % r.randint(0, 2), 'arr[%d]' % r.randint(0, 7), 'loc'])
        return 'asm("ldr x9, %%1\\n\\tadd x9, x9, %%2\\n\\tstr x9, %%0" : "=m"(%s) : "m"(%s), "r"(%s) : "x9");' % (m, m, a64)
    if c == 7:
        # clobbered register and flags; a value live across must survive
        return 'asm volatile("mov x10, #7\\n\\tadds x11, x10, x10\\n\\tadd %%0, %%1, x11" : "=r"(%s) : "r"(%s) : "x10", "x11", "cc");' % (a64, b64)
    if c == 8:
        # narrow operands: sub-word values through %w
        t = r.choice(['signed char', 'unsigned char', 'short', 'unsigned short'])
        return '{ %s n = (%s)%s; asm("add %%w0, %%w0, #%d" : "+r"(n)); %s = n; }' % (t, t, a64, r.randint(0, 300), a32)
    if c == 10:
        # two plain outputs, written before the inputs are all read
        return 'asm("add %%0, %%2, %%3\\n\\tsub %%1, %%2, %%3" : "=&r"(%s), "=&r"(%s) : "r"(%s), "r"(%s));' % (
            a64, r.choice([v for v in vars64 if v != a64]), b64, r.choice(vars64))
    if c == 11:
        # clobbers of registers the allocator hands out, values live across
        regs = r.sample(['x12', 'x13', 'x14', 'x15', 'x19', 'x20', 'x21', 'x22', 'x23', 'x24'], 2)
        return 'asm volatile("mov %s, #3\\n\\tmov %s, #5\\n\\tadd %%0, %%1, %%2" : "=r"(%s) : "r"(%s), "r"(%s) : "%s", "%s");' % (
            regs[0], regs[1], a64, b64, r.choice(vars64), regs[0], regs[1])
    if c == 12:
        # the same variable in and out (a CRC loop's shape), maybe in a loop
        s = 'asm("eor %%w0, %%w1, %%w2" : "=r"(%s) : "r"(%s), "r"(%s));' % (a32, a32, b32)
        if r.random() < 0.5:
            return s
        return 'for (int i = 0; i < %d; i++) { %s %s += i; }' % (r.randint(1, 6), s, b32)
    if c == 13:
        # the ACLE intrinsics (inline asm in ccc's arm_acle.h)
        f = r.choice(['__crc32b', '__crc32h', '__crc32w', '__crc32cb', '__crc32ch', '__crc32cw'])
        return '%s = %s((unsigned)%s, %s); %s = __crc32cd(%s, (unsigned long long)%s) + __rbit(%s) + __clz(%s);' % (
            a32, f, a32, b64, b32, b32, a64, a32, b32)
    if c == 14:
        # an output stored through a pointer
        return '{ long *pp = &arr[%d]; asm("add %%0, %%1, %%2" : "=r"(*pp) : "r"(%s), "r"(%s)); }' % (
            r.randint(0, 7), a64, b64)
    # asm inside a loop with an induction variable
    return 'for (int i = 0; i < %d; i++) asm("add %%0, %%0, %%1" : "+r"(%s) : "r"((long)i));' % (r.randint(1, 5), a64)


def gen_func(r, fi):
    vars64 = ['p%d' % i for i in range(4)]
    vars32 = ['q%d' % i for i in range(3)]
    body = ['long loc = c;']
    for i, v in enumerate(vars64):
        body.append('%s %s = (%s)(a * %d + b + %d);' % (r.choice(T64), v, r.choice(T64), r.randint(1, 9), i))
    for i, v in enumerate(vars32):
        body.append('%s %s = (%s)(b ^ %d);' % (r.choice(T32), v, r.choice(T32), r.randint(0, 999)))
    for k in range(r.randint(3, 12)):
        body.append(gen_stmt(r, k, vars64, vars32))
        if r.random() < 0.4:
            v = r.choice(vars64 + vars32)
            body.append('H(%s);' % v)
    for v in vars64 + vars32 + ['loc', 'g0', 'g1', 'g2']:
        body.append('H(%s);' % v)
    body.append('for (int i = 0; i < 8; i++) H(arr[i]);')
    return '__attribute__((noinline)) void f%d(long a, long b, long c)\n{\n    %s\n}' % (fi, '\n    '.join(body))


def gen_file(r):
    top = ['#include <stdio.h>', '#include <arm_acle.h>',
           '#define H(...) (h = h * 1000003u + (unsigned long long)(__VA_ARGS__))',
           'unsigned long long h;', 'long g0 = 5, g1 = -7, g2 = 99;', 'long arr[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };']
    nf = r.randint(1, 3)
    for fi in range(nf):
        top.append(gen_func(r, fi))
    calls = []
    for fi in range(nf):
        for _ in range(2):
            calls.append('f%d(%d, %d, %d);' % (fi, r.randint(-1000, 1000), r.randint(-99999, 99999), r.randint(-5, 5)))
    top.append('int main(void)\n{\n    %s\n    printf("%%llu\\n", h);\n    return 0;\n}' % '\n    '.join(calls))
    return '\n'.join(top) + '\n'


def run(cmd, timeout=60):
    try:
        return subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return None


bad = skipped = 0
for t in range(COUNT):
    r = random.Random(SEED * 100003 + t)
    f = os.path.join(WORK, 'a%d_%d.c' % (SEED, t))
    with open(f, 'w') as fh:
        fh.write(gen_file(r))
    ub = run([CLANG, '-O0', '-w', '-fsanitize=undefined', '-fno-sanitize-recover=all', f, '-o', f + '.ub'])
    if ub is None or ub.returncode != 0:
        skipped += 1
        print('clang-fail', f, (ub.stderr[:300] if ub else 'timeout'))
        continue
    u = run([f + '.ub'])
    os.remove(f + '.ub')
    if u is None or u.returncode != 0:
        skipped += 1
        print('skip', f, (u.stderr[:200] if u else 'timeout'))
        continue
    cc = run([CCC, OPT, '-w', f, '-o', f + '.ccc'])
    if cc is None or cc.returncode != 0:
        print('ccc-fail', f, (cc.stderr[:300] if cc else 'timeout'))
        bad += 1
        continue
    b = run([f + '.ccc'])
    if b is None or b.returncode != 0 or u.stdout != b.stdout:
        print('MISMATCH', f)
        bad += 1
    else:
        os.remove(f + '.ccc')
        os.remove(f)
print('done', COUNT, 'bad', bad, 'skipped', skipped)
