#!/usr/bin/env python3
"""Differential test harness: compile C programs with ccc and a reference
compiler (clang by default), run both, and compare exit status and stdout.

Usage:
    tests/difftest/fetch_suites.sh                 # once: download external suites
    tests/difftest/difftest.py                     # run everything, print summary
    tests/difftest/difftest.py --suite cases -k va # filter by suite / substring
    tests/difftest/difftest.py --save-baseline     # record baseline/<platform>.json
    tests/difftest/difftest.py --compare           # diff against the saved baseline

Suites:
    cases        tests/difftest/cases/*.c (hand-written). A companion file
                 NAME.helper.c is always compiled with the reference compiler
                 and linked into both binaries, to check ABI interoperability.
    c-testsuite  suites/c-testsuite/tests/single-exec/*.c
    gcc-torture  suites/gcc-torture/*.c (gcc.c-torture/execute)

A test whose reference build or run fails (compile error, crash, timeout) is
reported as `ref_fail` and excluded from the pass rate: there is nothing to
compare against.
"""

import argparse
import concurrent.futures
import datetime
import hashlib
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
SUITES_DIR = os.path.join(HERE, "suites")
BASELINE_DIR = os.path.join(HERE, "baseline")
CACHE_PATH = os.path.join(SUITES_DIR, ".ref-cache.json")

RUN_TIMEOUT = 10
COMPILE_TIMEOUT = 60
MAX_OUTPUT = 1 << 20

# Statuses, roughly from best to worst. Everything but pass/ref_fail is a failure.
PASS = "pass"
REF_FAIL = "ref_fail"
COMPILE_ERROR = "compile_error"      # ccc rejected the program or crashed
ASM_ERROR = "asm_error"              # ccc's assembly rejected by the assembler
LINK_ERROR = "link_error"
CRASH = "crash"                      # binary died from a signal
TIMEOUT = "timeout"
WRONG_EXIT = "wrong_exit"            # exit code differs, stdout matches
WRONG_OUTPUT = "wrong_output"
FAILURES = [COMPILE_ERROR, ASM_ERROR, LINK_ERROR, CRASH, TIMEOUT, WRONG_EXIT, WRONG_OUTPUT]


def host_arch():
    """The hardware architecture. platform.machine() reports the interpreter's
    architecture, which is x86_64 when Python runs under Rosetta on Apple Silicon."""
    if platform.system() == "Darwin":
        r = subprocess.run(["sysctl", "-n", "hw.optional.arm64"], capture_output=True, text=True)
        if r.stdout.strip() == "1":
            return "arm64"
    return platform.machine().lower()


def default_platform():
    return f"{platform.system().lower()}-{host_arch()}"


def arch_flags():
    """Flags pinning the reference compiler to the host architecture (Rosetta-proof)."""
    return ["-arch", host_arch()] if platform.system() == "Darwin" else []


# ---------------------------------------------------------------------------
# Test discovery
# ---------------------------------------------------------------------------

class Test:
    def __init__(self, suite, name, path, helper=None, extra_flags=()):
        self.suite = suite
        self.name = name
        self.path = path
        self.helper = helper
        self.extra_flags = list(extra_flags)

    @property
    def id(self):
        return f"{self.suite}/{self.name}"


# GCC torture tests that rely on target-specific options or GCC-only behavior
# carry dg- directives; tests that require options we don't model are skipped.
DG_SKIP = re.compile(r"dg-(require-effective-target|skip-if|additional-sources)|dg-options\s+\"[^\"]*-f(no-)?(trapv|wrapv|strict)")


def discover(suite):
    tests = []
    if suite == "cases":
        d = os.path.join(HERE, "cases")
        for f in sorted(os.listdir(d)) if os.path.isdir(d) else []:
            if f.endswith(".c") and not f.endswith(".helper.c"):
                stem = f[:-2]
                helper = os.path.join(d, stem + ".helper.c")
                tests.append(Test(suite, stem, os.path.join(d, f),
                                  helper if os.path.exists(helper) else None))
    elif suite == "c-testsuite":
        d = os.path.join(SUITES_DIR, "c-testsuite", "tests", "single-exec")
        for f in sorted(os.listdir(d)) if os.path.isdir(d) else []:
            if f.endswith(".c"):
                tests.append(Test(suite, f[:-2], os.path.join(d, f)))
    elif suite == "gcc-torture":
        d = os.path.join(SUITES_DIR, "gcc-torture")
        for f in sorted(os.listdir(d)) if os.path.isdir(d) else []:
            if not f.endswith(".c"):
                continue
            path = os.path.join(d, f)
            with open(path, errors="replace") as fh:
                if DG_SKIP.search(fh.read()):
                    continue
            tests.append(Test(suite, f[:-2], path))
    else:
        raise SystemExit(f"unknown suite: {suite}")
    return tests


SUITES = ["cases", "c-testsuite", "gcc-torture"]


# ---------------------------------------------------------------------------
# Building and running
# ---------------------------------------------------------------------------

def run(cmd, timeout, cwd=None):
    """Run a command; return (returncode, stdout bytes, stderr text, timed_out)."""
    try:
        p = subprocess.run(cmd, stdin=subprocess.DEVNULL, capture_output=True,
                           timeout=timeout, cwd=cwd)
        return p.returncode, p.stdout[:MAX_OUTPUT], p.stderr.decode(errors="replace"), False
    except subprocess.TimeoutExpired as e:
        return None, (e.stdout or b"")[:MAX_OUTPUT], "", True


def build(compiler, flags, test, out, workdir, ref_cc):
    """Compile (and link) a test. Returns (ok, stage, stderr)."""
    objs = []
    if test.helper:
        helper_obj = os.path.join(workdir, "helper.o")
        rc, _, err, to = run([ref_cc] + arch_flags() + ["-c", "-w", "-o", helper_obj, test.helper],
                             COMPILE_TIMEOUT)
        if rc != 0 or to:
            return False, "helper", err
        objs.append(helper_obj)
    obj = os.path.join(workdir, os.path.basename(out) + ".o")
    rc, _, err, to = run([compiler] + flags + test.extra_flags + ["-c", "-o", obj, test.path],
                         COMPILE_TIMEOUT, cwd=os.path.dirname(test.path))
    if to:
        return False, "compile", "compile timed out"
    if rc != 0:
        stage = "asm" if re.search(r"assembler failed|Assembly failed", err) else "compile"
        return False, stage, err
    rc, _, err, to = run([compiler] + flags + ["-o", out, obj] + objs + ["-lm"], COMPILE_TIMEOUT)
    if rc != 0 or to:
        return False, "link", err
    return True, None, ""


def execute(binary, cwd):
    rc, out, _, timed_out = run([binary], RUN_TIMEOUT, cwd=cwd)
    if timed_out:
        return {"status": "timeout", "stdout": out}
    return {"status": "exit", "code": rc, "stdout": out}


def describe(res):
    if res["status"] == "timeout":
        return "timeout"
    code = res["code"]
    return f"signal {-code}" if code < 0 else f"exit {code}"


def source_digest(test, ref_cc_version, ref_flags):
    h = hashlib.sha256()
    for p in [test.path, test.helper]:
        if p:
            with open(p, "rb") as f:
                h.update(f.read())
    h.update(ref_cc_version.encode())
    h.update(" ".join(ref_flags + test.extra_flags).encode())
    return h.hexdigest()


def run_test(test, args, ref_cache, ref_cc_version):
    workdir = tempfile.mkdtemp(prefix="difftest-")
    try:
        # Reference side (cached by source + compiler version + flags).
        key = source_digest(test, ref_cc_version, args.ref_flags)
        ref = ref_cache.get(key)
        if ref is None:
            ref_bin = os.path.join(workdir, "ref")
            ok, stage, err = build(args.ref_cc, args.ref_flags, test, ref_bin, workdir, args.ref_cc)
            if not ok:
                ref = {"status": "build_fail", "detail": f"{stage}: {first_line(err)}"}
            else:
                r = execute(ref_bin, os.path.dirname(test.path))
                ref = {"status": r["status"], "code": r.get("code"),
                       "stdout": r["stdout"].decode("latin-1")}
            ref_cache[key] = ref
        if ref["status"] != "exit" or (ref["code"] is not None and ref["code"] < 0):
            detail = ref.get("detail") or describe(ref)
            return {"status": REF_FAIL, "detail": detail}

        # ccc side.
        t0 = time.time()
        ccc_bin = os.path.join(workdir, "ccc")
        ok, stage, err = build(args.ccc, args.ccc_flags, test, ccc_bin, workdir, args.ref_cc)
        compile_s = time.time() - t0
        if not ok:
            status = {"asm": ASM_ERROR, "link": LINK_ERROR, "helper": REF_FAIL}.get(stage, COMPILE_ERROR)
            return {"status": status, "detail": first_line(err), "compile_s": compile_s}
        r = execute(ccc_bin, os.path.dirname(test.path))
        if r["status"] == "timeout":
            return {"status": TIMEOUT, "detail": "ccc binary timed out", "compile_s": compile_s}
        if r["code"] < 0:
            return {"status": CRASH, "detail": describe(r), "compile_s": compile_s}
        out = r["stdout"].decode("latin-1")
        if out != ref["stdout"]:
            return {"status": WRONG_OUTPUT, "compile_s": compile_s,
                    "detail": first_diff(ref["stdout"], out)}
        if r["code"] != ref["code"]:
            return {"status": WRONG_EXIT, "compile_s": compile_s,
                    "detail": f"expected exit {ref['code']}, got {r['code']}"}
        return {"status": PASS, "compile_s": compile_s}
    finally:
        shutil.rmtree(workdir, ignore_errors=True)


def first_line(text):
    # Report paths relative to the repo, so baselines don't record the
    # checkout location (and with it the user's home directory).
    text = text.replace(REPO + os.sep, "")
    for line in text.splitlines():
        line = line.strip()
        if line and not line.startswith("WARNING"):
            return line[:300]
    return ""


def first_diff(expected, actual):
    el, al = expected.splitlines(), actual.splitlines()
    for i in range(max(len(el), len(al))):
        e = el[i] if i < len(el) else "<eof>"
        a = al[i] if i < len(al) else "<eof>"
        if e != a:
            return f"line {i + 1}: expected {e[:100]!r}, got {a[:100]!r}"
    return "trailing whitespace/newline differs"


# ---------------------------------------------------------------------------
# Reporting
# ---------------------------------------------------------------------------

def summarize(results):
    by_suite = {}
    for tid, r in results.items():
        suite = tid.split("/", 1)[0]
        counts = by_suite.setdefault(suite, {})
        counts[r["status"]] = counts.get(r["status"], 0) + 1
    return by_suite


def pass_rate(counts):
    considered = sum(n for s, n in counts.items() if s != REF_FAIL)
    return (counts.get(PASS, 0) / considered * 100) if considered else 0.0


def print_summary(results, verbose):
    by_suite = summarize(results)
    cols = [PASS] + FAILURES + [REF_FAIL]
    header = f"{'suite':<14}" + "".join(f"{c:>14}" for c in cols) + f"{'pass%':>8}"
    print(header)
    print("-" * len(header))
    total = {}
    for suite in sorted(by_suite):
        counts = by_suite[suite]
        for k, v in counts.items():
            total[k] = total.get(k, 0) + v
        print(f"{suite:<14}" + "".join(f"{counts.get(c, 0):>14}" for c in cols)
              + f"{pass_rate(counts):>7.1f}%")
    if len(by_suite) > 1:
        print(f"{'TOTAL':<14}" + "".join(f"{total.get(c, 0):>14}" for c in cols)
              + f"{pass_rate(total):>7.1f}%")
    if verbose:
        print()
        for tid in sorted(results):
            r = results[tid]
            if r["status"] in FAILURES:
                print(f"  {r['status']:<14} {tid}: {r.get('detail', '')}")


def write_markdown(path, meta, results):
    by_suite = summarize(results)
    cols = [PASS] + FAILURES + [REF_FAIL]
    lines = [f"# difftest baseline: {meta['platform']}", "",
             f"- date: {meta['date']}",
             f"- ccc: `{meta['ccc_rev']}` flags `{' '.join(meta['ccc_flags'])}`",
             f"- reference: `{meta['ref_cc_version']}` flags `{' '.join(meta['ref_flags'])}`",
             "", "| suite | " + " | ".join(cols) + " | pass % |",
             "|---" * (len(cols) + 2) + "|"]
    for suite in sorted(by_suite):
        c = by_suite[suite]
        lines.append(f"| {suite} | " + " | ".join(str(c.get(k, 0)) for k in cols)
                     + f" | {pass_rate(c):.1f} |")
    lines += ["", "## Failures", ""]
    for status in FAILURES:
        failing = sorted(t for t, r in results.items() if r["status"] == status)
        if not failing:
            continue
        lines.append(f"### {status} ({len(failing)})")
        lines.append("")
        for t in failing:
            detail = results[t].get("detail", "").replace("|", "\\|")
            lines.append(f"- `{t}`: {detail}")
        lines.append("")
    with open(path, "w") as f:
        f.write("\n".join(lines))


def compare(baseline, results):
    """Print regressions and fixes relative to a baseline. Returns #regressions."""
    base = baseline["results"]
    regressions, fixes, changed = [], [], []
    for tid, r in sorted(results.items()):
        b = base.get(tid)
        if b is None:
            continue
        if b["status"] == PASS and r["status"] in FAILURES:
            regressions.append((tid, b["status"], r))
        elif b["status"] in FAILURES and r["status"] == PASS:
            fixes.append((tid, b["status"], r))
        elif b["status"] != r["status"]:
            changed.append((tid, b["status"], r))
    for title, rows in [("REGRESSIONS", regressions), ("FIXED", fixes), ("CHANGED", changed)]:
        if rows:
            print(f"\n{title} ({len(rows)}):")
            for tid, old, r in rows:
                print(f"  {tid}: {old} -> {r['status']}  {r.get('detail', '')}")
    if not (regressions or fixes or changed):
        print("\nno changes relative to baseline")
    return len(regressions)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ccc", default=os.path.join(REPO, "target", "release", "ccc"))
    ap.add_argument("--ref-cc", default="clang")
    ap.add_argument("--ccc-flags", default="-O2 -w", help="flags for ccc (default: %(default)s)")
    ap.add_argument("--ref-flags", default="-O0 -w", help="flags for the reference (default: %(default)s)")
    ap.add_argument("--suite", action="append", choices=SUITES, help="suite(s) to run (default: all)")
    ap.add_argument("-k", dest="filter", help="only tests whose id contains this substring")
    ap.add_argument("-j", "--jobs", type=int, default=os.cpu_count())
    ap.add_argument("-v", "--verbose", action="store_true", help="list every failure")
    ap.add_argument("--platform", default=default_platform())
    ap.add_argument("--save-baseline", action="store_true", help="write baseline/<platform>.{json,md}")
    ap.add_argument("--compare", action="store_true", help="diff against baseline/<platform>.json")
    ap.add_argument("--json", help="also write full results to this path")
    args = ap.parse_args()
    args.ccc_flags = args.ccc_flags.split()
    args.ref_flags = arch_flags() + args.ref_flags.split()

    if not os.path.exists(args.ccc):
        raise SystemExit(f"ccc not found at {args.ccc}; run `cargo build --release` first")

    tests = []
    for suite in args.suite or SUITES:
        found = discover(suite)
        if not found and suite != "cases":
            print(f"note: suite {suite} not found; run tests/difftest/fetch_suites.sh", file=sys.stderr)
        tests += found
    if args.filter:
        tests = [t for t in tests if args.filter in t.id]
    if not tests:
        raise SystemExit("no tests selected")

    ref_cc_version = subprocess.run([args.ref_cc, "--version"], capture_output=True,
                                    text=True).stdout.splitlines()[0]
    ref_cache = {}
    if os.path.exists(CACHE_PATH):
        with open(CACHE_PATH) as f:
            ref_cache = json.load(f)

    results = {}
    t0 = time.time()
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = {pool.submit(run_test, t, args, ref_cache, ref_cc_version): t for t in tests}
        for i, fut in enumerate(concurrent.futures.as_completed(futures), 1):
            t = futures[fut]
            try:
                results[t.id] = fut.result()
            except Exception as e:  # harness bug: record it rather than abort the run
                results[t.id] = {"status": COMPILE_ERROR, "detail": f"harness error: {e}"}
            if sys.stderr.isatty():
                print(f"\r[{i}/{len(tests)}]", end="", file=sys.stderr, flush=True)
    if sys.stderr.isatty():
        print(file=sys.stderr)
    elapsed = time.time() - t0

    os.makedirs(SUITES_DIR, exist_ok=True)
    with open(CACHE_PATH, "w") as f:
        json.dump(ref_cache, f)

    print_summary(results, args.verbose)
    print(f"\n{len(tests)} tests in {elapsed:.1f}s")

    ccc_rev = subprocess.run(["git", "-C", REPO, "describe", "--always", "--dirty"],
                             capture_output=True, text=True).stdout.strip()
    meta = {"platform": args.platform, "date": datetime.date.today().isoformat(),
            "ccc_rev": ccc_rev, "ccc_flags": args.ccc_flags,
            "ref_cc_version": ref_cc_version, "ref_flags": args.ref_flags}
    doc = {"meta": meta, "results": {k: results[k] for k in sorted(results)}}

    if args.json:
        with open(args.json, "w") as f:
            json.dump(doc, f, indent=1)

    baseline_json = os.path.join(BASELINE_DIR, f"{args.platform}.json")
    regressions = 0
    if args.compare:
        if not os.path.exists(baseline_json):
            raise SystemExit(f"no baseline at {baseline_json}")
        with open(baseline_json) as f:
            regressions = compare(json.load(f), results)
    if args.save_baseline:
        os.makedirs(BASELINE_DIR, exist_ok=True)
        # Timing varies run to run; keep baselines diffable.
        stable = {"meta": meta, "results": {k: {kk: vv for kk, vv in v.items() if kk != "compile_s"}
                                            for k, v in doc["results"].items()}}
        with open(baseline_json, "w") as f:
            json.dump(stable, f, indent=1)
            f.write("\n")
        write_markdown(os.path.join(BASELINE_DIR, f"{args.platform}.md"), meta, results)
        print(f"baseline written to {os.path.relpath(baseline_json, REPO)}")
    return 1 if regressions else 0


if __name__ == "__main__":
    sys.exit(main())
