"""VLA fuzzer, second round: on top of vlfz.py's expressions (imported), VLA
typedefs (and arrays of them), typeof of VLA type names, sizeof operands with
side effects (i++), VLA parameters (T p[D1][D2], T (*q)[D1][D2]) in a callee,
and VLAs redeclared in a loop.

usage: vlfz2.py CCC CLANG WORK SEED COUNT   (env OPT, default -O1)
"""
import os, random, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import importlib.util
spec = importlib.util.spec_from_file_location('vlfz', os.path.join(HERE, 'vlfz.py'))
V = importlib.util.module_from_spec(spec)
src = open(os.path.join(HERE, 'vlfz.py')).read().replace('\nmain()\n', '\n')
exec(compile(src, 'vlfz.py', 'exec'), V.__dict__)
R, Obj, derive, dimstr = V.R, V.Obj, V.derive, V.dimstr

def lines_for(roots, nlines, fresh, body):
    for _ in range(nlines):
        o = derive(R(roots))
        x = o.text
        r = random.randrange(10)
        if r < 3:
            body.append('  printf("%%d %%zu %%ld\\n", __LINE__, sizeof(%s), OFF(%s, %s));' % (x, x, o.root))
            if o.kind == 'ptr':
                body.append('  printf("%%d %%ld\\n", __LINE__, OFF(%s + 1, %s));' % (x, x))
        elif r < 5:
            v = fresh('v')
            if o.kind == 'arr':
                body.append('  __typeof__(%s) %s; memset(%s, 7, sizeof %s); printf("%%d %%zu %%d\\n", __LINE__, sizeof %s, *(unsigned char *)%s);' % (x, v, v, v, v, v))
            else:
                body.append('  __typeof__(%s) %s = %s; printf("%%d %%ld %%ld\\n", __LINE__, OFF(%s + 1, %s), OFF(%s, %s));' % (x, v, x, v, v, v, o.root))
        elif r < 7 and o.kind == 'arr' and len(o.dims) > 1:
            # sizeof with a side effect in the operand: evaluated iff VLA type.
            body.append('  { int i_ = 0; size_t z_ = sizeof %s[i_++]; printf("%%d %%zu %%d\\n", __LINE__, z_, i_); }' % x)
        elif r < 8 and o.kind == 'ptr' and o.dims:
            body.append('  { int i_ = 0; size_t z_ = sizeof %s[i_++]; printf("%%d %%zu %%d\\n", __LINE__, z_, i_); }' % x)
        elif r < 9:
            t = R(V.TYPES)
            dims = [R(V.DIMS) for _ in range(random.randint(1, 3))]
            v = fresh('y')
            body.append('  __typeof__(%s%s) %s; memset(%s, 2, sizeof %s); printf("%%d %%zu %%zu\\n", __LINE__, sizeof %s, sizeof(__typeof__(%s%s)));' % (t, dimstr(dims), v, v, v, v, t, dimstr(dims)))
        else:
            body.append('  printf("%%d %%d\\n", __LINE__, calls);')

def gen(nlines):
    body, gbody = [], []
    roots = []
    nv = [0]
    def fresh(p):
        nv[0] += 1
        return '%s%d' % (p, nv[0])
    for _ in range(random.randint(1, 2)):
        t = R(V.TYPES)
        dims = [R(V.DIMS) for _ in range(random.randint(1, 2))]
        td = fresh('T')
        body.append('  typedef %s %s%s;' % (t, td, dimstr(dims)))
        a = fresh('a')
        outer = [R(V.DIMS) for _ in range(random.randint(0, 1))]
        body.append('  %s %s%s; memset(%s, 0, sizeof %s); printf("%%d %%zu %%zu\\n", __LINE__, sizeof %s, sizeof(%s));' % (td, a, dimstr(outer), a, a, a, td))
        roots.append(Obj(a, 'arr', outer + dims, t, a))
        if random.random() < 0.5:
            v = fresh('y')
            body.append('  __typeof__(%s[2]) %s; memset(%s, 1, sizeof %s); printf("%%d %%zu %%zu\\n", __LINE__, sizeof %s, sizeof %s[1]);' % (td, v, v, v, v, v))
        if random.random() < 0.5:
            p = fresh('p')
            body.append('  %s *%s = &%s; printf("%%d %%ld\\n", __LINE__, OFF(%s + 1, %s));' % (td, p, a if not outer else a + '[0]', p, p))
    pt = R(V.TYPES)
    pdims = [R(V.DIMS) for _ in range(random.randint(2, 3))]
    pa = fresh('a')
    body.append('  %s %s%s; memset(%s, 0, sizeof %s);' % (pt, pa, dimstr(pdims), pa, pa))
    roots.append(Obj(pa, 'arr', pdims, pt, pa))
    for _ in range(random.randint(1, 2)):
        t = R(V.TYPES)
        dims = [R(V.DIMS) for _ in range(random.randint(1, 3))]
        a = fresh('a')
        body.append('  %s %s%s; memset(%s, 0, sizeof %s);' % (t, a, dimstr(dims), a, a))
        roots.append(Obj(a, 'arr', dims, t, a))
    lines_for(roots, nlines, fresh, body)
    body.append('  for (int j_ = 1; j_ <= 2; j_++) {')
    lt = R(V.TYPES)
    ldims = ['(j_ + %s)' % R(['n', 'm', '1'])] + [R(V.DIMS) for _ in range(random.randint(0, 1))]
    la = fresh('a')
    body.append('  %s %s%s; memset(%s, 0, sizeof %s);' % (lt, la, dimstr(ldims), la, la))
    lines_for([Obj(la, 'arr', ldims, lt, la)], 4, fresh, body)
    body.append('  }')
    body.append('  g(n, m, k, %s, &%s);' % (pa, pa))
    groots = [Obj('p', 'ptr', pdims[1:], pt, 'p'), Obj('q', 'ptr', pdims, pt, 'q')]
    lines_for(groots, nlines // 2, fresh, gbody)
    gsig = 'static void g(int n, int m, int k, %s p%s, %s (*q)%s)' % (pt, dimstr(pdims), pt, dimstr(pdims))
    return gsig, gbody, body

def build(path):
    gsig, gbody, body = gen(30)
    out = [V.HDR]
    out.append('__attribute__((noinline)) %s {\n' % gsig + '\n'.join(gbody) + '\n}\n')
    out.append('__attribute__((noinline)) static void f(int n, int m, int k) {\n')
    out.append('\n'.join(body) + '\n')
    out.append('  printf("calls %d\\n", calls);\n}\n')
    out.append('int main(void) { f(2, 3, 4); f(3, 2, 5); f(4, 4, 2); return 0; }\n')
    open(path, 'w').write(''.join(out))

V.build = build
V.main()
