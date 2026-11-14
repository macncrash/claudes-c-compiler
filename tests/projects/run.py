#!/usr/bin/env python3
"""Build real C libraries and a driver for each (drivers/) with ccc and with
a reference compiler, run both, and compare the output.

usage: run.py --ccc PATH [--clang PATH] [--src-root DIR] [--only a,b] [-j N] [--opt -O2]

The library sources are not in this repository: each project is found
under --src-root (default: the cargo registry, ~/.cargo/registry/src/*,
where crates such as zstd-sys and libsqlite3-sys keep the C they build).
A project whose sources are missing is skipped. Exits 1 if any project
builds or runs differently with ccc.
"""
import argparse
import glob
import os
import platform
import shutil
import stat
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))

# dir: glob under the source root (the last match, i.e. the newest version,
# is used); sources: globs under dir; inc: include dirs under dir.
PROJECTS = [
    {'name': 'zstd', 'dir': 'zstd-sys-*/zstd/lib',
     'sources': ['common/*.c', 'compress/*.c', 'decompress/*.c'], 'inc': ['.', 'common'],
     'defines': ['ZSTD_DISABLE_ASM']},
    {'name': 'lz4', 'dir': 'lz4-sys-*/liblz4/lib',
     'sources': ['lz4.c', 'lz4hc.c', 'lz4frame.c', 'xxhash.c'], 'inc': ['.'], 'defines': []},
    {'name': 'blake3', 'dir': 'blake3-*/c', 'driver': 'blake3',
     'sources': ['blake3.c', 'blake3_dispatch.c', 'blake3_portable.c'], 'inc': ['.'],
     'defines': ['BLAKE3_NO_SSE2', 'BLAKE3_NO_SSE41', 'BLAKE3_NO_AVX2', 'BLAKE3_NO_AVX512', 'BLAKE3_USE_NEON=0']},
    {'name': 'blake3-neon', 'dir': 'blake3-*/c', 'driver': 'blake3', 'arch': 'arm64',
     'sources': ['blake3.c', 'blake3_dispatch.c', 'blake3_portable.c', 'blake3_neon.c'], 'inc': ['.'],
     'defines': ['BLAKE3_NO_SSE2', 'BLAKE3_NO_SSE41', 'BLAKE3_NO_AVX2', 'BLAKE3_NO_AVX512', 'BLAKE3_USE_NEON=1']},
    {'name': 'sqlite', 'dir': 'libsqlite3-sys-*/sqlite3',
     'sources': ['sqlite3.c'], 'inc': ['.'],
     'defines': ['SQLITE_THREADSAFE=0', 'SQLITE_OMIT_LOAD_EXTENSION', 'SQLITE_ENABLE_FTS5',
                 'SQLITE_ENABLE_MATH_FUNCTIONS']},
]


def write_script(path, body):
    with open(path, 'w') as fh:
        fh.write('#!/bin/sh\n' + body + '\n')
    os.chmod(path, os.stat(path).st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)


def is_apple_silicon():
    if sys.platform != 'darwin':
        return False
    r = subprocess.run(['sysctl', '-n', 'hw.optional.arm64'], capture_output=True, text=True)
    return r.stdout.strip() == '1'


def reference_cc(work, clang):
    """clang for this machine: on Apple silicon a Python under Rosetta would
    otherwise make clang build x86_64 objects."""
    if clang:
        return clang
    path = os.path.join(work, 'cc-ref')
    if is_apple_silicon():
        write_script(path, 'exec /usr/bin/arch -arm64 /usr/bin/clang -arch arm64 "$@"')
    else:
        write_script(path, 'exec cc "$@"')
    return path


def find_dir(root, pattern):
    hits = sorted(glob.glob(os.path.join(root, pattern)))
    return hits[-1] if hits else None


def run(cmd, cwd=None, timeout=900):
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, cwd=cwd, timeout=timeout)
        return p.returncode, p.stdout, p.stderr
    except subprocess.TimeoutExpired:
        return -1, '', 'timeout'


def build_and_run(cc, label, proj, src_dir, work, opt, pool):
    out = os.path.join(work, label)
    os.makedirs(out, exist_ok=True)
    flags = [opt, '-w'] + ['-I' + os.path.join(src_dir, i) for i in proj['inc']] + ['-D' + d for d in proj['defines']]
    files = []
    for pat in proj['sources']:
        files += sorted(glob.glob(os.path.join(src_dir, pat)))
    driver = os.path.join(HERE, 'drivers', proj.get('driver', proj['name']) + '.c')
    files.append(driver)
    objs = [os.path.join(out, '%d_%s.o' % (k, os.path.basename(f)[:-2])) for k, f in enumerate(files)]
    results = list(pool.map(lambda fo: run([cc] + flags + ['-c', fo[0], '-o', fo[1]]), zip(files, objs)))
    for f, (rc, _, err) in zip(files, results):
        if rc != 0:
            return 'compile failed: %s: %s' % (os.path.basename(f), err.strip()[:300]), None
    exe = os.path.join(out, 'run')
    # Link with the reference compiler driver for both: the comparison is of
    # the code each compiler generated, linked the same way.
    rc, _, err = run([REF_LINK, '-o', exe] + objs + ['-lm'])
    if rc != 0:
        return 'link failed: %s' % err.strip()[:300], None
    rc, so, se = run([exe], cwd=out)
    return None, (rc, so, se)


REF_LINK = None


def main():
    global REF_LINK
    ap = argparse.ArgumentParser()
    ap.add_argument('--ccc', required=True)
    ap.add_argument('--clang', default=None)
    ap.add_argument('--src-root', default=os.path.expanduser('~/.cargo/registry/src/*'))
    ap.add_argument('--only', default='')
    ap.add_argument('--opt', default='-O2')
    ap.add_argument('-j', type=int, default=8)
    ap.add_argument('--keep', action='store_true')
    args = ap.parse_args()
    ccc = os.path.abspath(args.ccc)
    work = tempfile.mkdtemp(prefix='ccc-projects-')
    ref = reference_cc(work, args.clang)
    REF_LINK = ref
    roots = sorted(glob.glob(args.src_root)) or [args.src_root]
    failed = skipped = 0
    with ThreadPoolExecutor(max_workers=args.j) as pool:
        for proj in PROJECTS:
            if args.only and proj['name'] not in args.only.split(','):
                continue
            if proj.get('arch') == 'arm64' and not (is_apple_silicon() or platform.machine() in ('arm64', 'aarch64')):
                continue
            src_dir = None
            for r in roots:
                src_dir = find_dir(r, proj['dir'])
                if src_dir:
                    # The crate (and version) the sources come from.
                    origin = os.path.relpath(src_dir, r).split(os.sep)[0]
                    break
            if not src_dir:
                print('%-12s skipped (no %s under %s)' % (proj['name'], proj['dir'], args.src_root))
                skipped += 1
                continue
            pw = os.path.join(work, proj['name'])
            err_r, ref_res = build_and_run(ref, 'ref', proj, src_dir, pw, args.opt, pool)
            if err_r:
                print('%-12s skipped (reference: %s)' % (proj['name'], err_r))
                skipped += 1
                continue
            err_c, ccc_res = build_and_run(ccc, 'ccc', proj, src_dir, pw, args.opt, pool)
            rc_r, out_r, _ = ref_res
            if err_c:
                status = 'FAIL ' + err_c
            elif not out_r.strip() or rc_r != 0:
                status = 'skipped (reference run: exit %d, %d bytes)' % (rc_r, len(out_r))
                skipped += 1
                print('%-12s %s' % (proj['name'], status))
                continue
            elif ccc_res != ref_res:
                rc_c, out_c, err_c2 = ccc_res
                rl, cl = out_r.splitlines(), out_c.splitlines()
                first = next((k for k in range(max(len(rl), len(cl)))
                              if (rl[k] if k < len(rl) else None) != (cl[k] if k < len(cl) else None)), None)
                status = 'FAIL exit %d vs %d; first difference at line %s:\n    ref %s\n    ccc %s' % (
                    rc_r, rc_c, first, rl[first] if first is not None and first < len(rl) else '<none>',
                    cl[first] if first is not None and first < len(cl) else '<none>' + err_c2[:200])
            else:
                status = 'ok (%d lines)' % len(out_r.splitlines())
            if status.startswith('FAIL'):
                failed += 1
            print('%-12s %s  [%s]' % (proj['name'], status, origin), flush=True)
    if failed or args.keep:
        print('work directory: %s' % work)
    else:
        shutil.rmtree(work, ignore_errors=True)
    print('%d failed, %d skipped' % (failed, skipped))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
