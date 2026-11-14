#!/usr/bin/env python3
"""Run every differential fuzzer in this directory for a few programs at a
fixed seed: a regression check over what they cover (see README.md).

usage: run.py --ccc PATH [--clang PATH] [--count N] [--seed S] [-j N] [--only a,b] [--keep]

Exits 1 if any fuzzer found a difference (its work directory is kept and
printed), 0 otherwise.
"""
import argparse
import os
import platform
import re
import shutil
import stat
import subprocess
import sys
import tempfile
import time
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))

# name -> whether it takes a CLANG argument (cfuzz compares ccc's constant
# folding with its own run-time results only).
FUZZERS = {
    'abifuzz': True, 'adfz': True, 'asmfz': True, 'atfz': True, 'bffuzz': True, 'bfrt': True,
    'cffuzz': True, 'cfuzz': False, 'clfz': True, 'cqfz': True, 'cvfz': True, 'cxfz': True,
    'ffuzz': True, 'fpfuzz': True, 'gnfz': True, 'gnufz': True, 'i128fz': True, 'ifuzz': True,
    'loopfuzz': True, 'memfz': True, 'pfuzz': True, 'pkfz': True, 'rdfuzz': True, 'rtfuzz': True, 'sfz': True,
    'sjfz': True, 'stfz': True, 'strfz': True, 'tlsfz': True, 'vafz': True, 'vafz2': True,
    'vlafz': True, 'vlfz': True, 'vlfz2': True, 'vmfz': True, 'wsfz': True,
}

SUMMARIES = [
    re.compile(r'^done (\d+) bad (\d+)'),
    re.compile(r'^seed \d+: (?P<bad>\d+) of (?P<n>\d+) files mismatched'),
    re.compile(r'^files (\d+) mismatching (\d+)'),
]


def write_script(path, body):
    with open(path, 'w') as fh:
        fh.write('#!/bin/sh\n' + body + '\n')
    os.chmod(path, os.stat(path).st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)


def reference_clang(work, clang):
    """A clang wrapper that builds for this machine's architecture: on Apple
    silicon, a Python running under Rosetta would otherwise get x86_64."""
    if clang:
        return clang
    path = os.path.join(work, 'clang-ref')
    if sys.platform == 'darwin' and platform.machine() in ('arm64', 'x86_64') and os.path.exists('/usr/bin/arch'):
        hw = subprocess.run(['sysctl', '-n', 'hw.optional.arm64'], capture_output=True, text=True).stdout.strip()
        if hw == '1':
            write_script(path, 'exec /usr/bin/arch -arm64 /usr/bin/clang -arch arm64 "$@"')
            return path
    write_script(path, 'exec clang "$@"')
    return path


def parse(out):
    for line in reversed(out.splitlines()):
        for rx in SUMMARIES:
            m = rx.match(line.strip())
            if m:
                if rx is SUMMARIES[1]:
                    return int(m.group('n')), int(m.group('bad'))
                return int(m.group(1)), int(m.group(2))
    return None


def run_one(name, args, ref, env, root):
    work = os.path.join(root, name)
    os.makedirs(work, exist_ok=True)
    cmd = [sys.executable, os.path.join(HERE, name + '.py'), args.ccc]
    if FUZZERS[name]:
        cmd.append(ref)
    cmd += [work, str(args.seed), str(args.count)]
    t0 = time.time()
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, env=env, timeout=args.timeout)
        out, rc = p.stdout + p.stderr, p.returncode
    except subprocess.TimeoutExpired as e:
        out, rc = (e.stdout or b'').decode(errors='replace') if isinstance(e.stdout, bytes) else (e.stdout or ''), -1
        out += '\nTIMEOUT'
    with open(os.path.join(work, 'log.txt'), 'w') as fh:
        fh.write(out)
    res = parse(out)
    return name, rc, res, time.time() - t0, work, out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--ccc', required=True)
    ap.add_argument('--clang', default=None, help='reference compiler (default: clang for this machine)')
    ap.add_argument('--count', type=int, default=3, help='programs per fuzzer')
    ap.add_argument('--seed', type=int, default=1)
    ap.add_argument('-j', type=int, default=4)
    ap.add_argument('--only', default='')
    ap.add_argument('--timeout', type=int, default=1800)
    ap.add_argument('--keep', action='store_true', help='keep the work directory even when clean')
    args = ap.parse_args()
    args.ccc = os.path.abspath(args.ccc)

    root = tempfile.mkdtemp(prefix='ccc-fuzz-')
    ref = reference_clang(root, args.clang)
    env = dict(os.environ)
    if not os.path.exists(env.get('GCC', '/opt/homebrew/bin/gcc-15')):
        # The second opinion is optional: without gcc-15 a fuzzer just
        # cannot drop the lines where gcc disagrees with clang.
        nogcc = os.path.join(root, 'no-gcc')
        write_script(nogcc, 'exit 1')
        env['GCC'] = nogcc

    names = [n for n in FUZZERS if not args.only or n in args.only.split(',')]
    failed = []
    with ThreadPoolExecutor(max_workers=args.j) as ex:
        for name, rc, res, secs, work, out in ex.map(lambda n: run_one(n, args, ref, env, root), names):
            if res is None or rc != 0:
                status = 'ERROR rc=%d' % rc
                failed.append((name, work, out))
            elif res[1]:
                status = '%d of %d differ' % (res[1], res[0])
                failed.append((name, work, out))
            else:
                status = 'ok (%d)' % res[0]
            print('%-9s %-18s %6.0fs' % (name, status, secs), flush=True)
    if failed:
        print('\n%d fuzzer(s) found differences or failed; work directory kept: %s' % (len(failed), root))
        for name, work, out in failed:
            tail = '\n'.join(out.strip().splitlines()[-4:])
            print('--- %s (%s)\n%s' % (name, work, tail))
        return 1
    if args.keep:
        print('work directory: %s' % root)
    else:
        shutil.rmtree(root, ignore_errors=True)
    print('all %d fuzzers clean' % len(names))
    return 0


if __name__ == '__main__':
    sys.exit(main())
