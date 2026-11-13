#!/usr/bin/env python3
"""Lowent runner for the basic_type_system (BTS) conformance vectors — RFC-0157 §8-1.

  python3 impl/tests/bts/run.py [--suite integer_ops] [--out impl/build/bts/results.jsonl] [--jobs 8]

Reads the vector copies in impl/tests/bts/vectors/ (their sha256 and the BTS commit are in SOURCES.tsv), turns every case
into a small Lowent function, and runs every row on both lowent implementations:

  vm      `lowentc --run-batch` — translated once per module, one forked run per row
  native  `lowentc --emit-c` + `cc`, then the binary once per row

A row that must trap is run on its own (harness-protocol §2: a trap does not hide the next row). The result file is
`bts-results/2` (harness-protocol §5) and is checked with the copied `check_results.py`. Mapping a BTS operation to Lowent:
the case's `names.lowent` when the vectors give it, else the table in `lowent_expr` below — a same-meaning spelling
(`wrap_sub 0 a` for a wrapping negation, `intops.*` for the named comparisons and the floor/truncating division); an
operation Lowent does not have is skipped as `language-policy` (the protocol's word for «the language does not have it»).
"""
import argparse, hashlib, json, os, pathlib, re, subprocess, sys, tempfile
from concurrent.futures import ThreadPoolExecutor

HERE = pathlib.Path(__file__).resolve().parent
PUB = HERE.parents[2]
LOWENTC = PUB / 'impl/build/lowentc'
VEC = HERE / 'conformance/vectors'
TARGET = 'x86_64'          # the machine this runs on; usize / idiff are 64 bits here
USIZE_BITS = 64
BUILD_MODE = 'debug'       # keeps every check (harness-protocol §1); written into each generated module
MODULE_CASES = 40          # cases per generated module (one translation, one C build)


def lowent_expr(case):
    """(expression over a. b. c., result type) or (None, skip reason)."""
    op, t, d = case['op'], case['type'], case.get('disposition')
    name = (case.get('names') or {}).get('lowent')
    if op in ('add_carry', 'sub_borrow'):
        return f'intops.{op}_{t} a. b. c. .', None
    if op == 'mul_wide':
        return f'intops.mul_wide_{t} a. b. .', None
    if op == 'mul_wide_s':
        return f'intops.mul_wide_s_{t} a. b. .', None
    if op in ('cmp_eq', 'cmp_lt', 'cmp_le'):
        return f'intops.{op} {t}. {case["type_b"]}. a. b. .', None
    if op == 'div_const':
        return f'div a. {case["divisor"]} .', None
    if op == 'rem_const':
        return f'intops.mod_trunc {t}. a. {case["divisor"]} .', None
    if name:
        n = len(case['rows'][0]) - 1
        return f'{name} ' + ' '.join(f'{v}.' for v in 'abc'[:n]) + ' .', None
    if op == 'neg':
        return {'trap': 'neg a. .', 'wrap': 'wrap_sub 0 a. .', 'sat': 'sat_sub 0 a. .', 'chk': 'chk_sub 0 a. .'}[d], None
    if op == 'abs' and d == 'trap':
        return 'abs a. .', None
    if op == 'div_floor' and d == 'trap':
        return f'intops.div_floor {t}. a. b. .', None
    if op == 'rem_trunc' and d == 'trap':
        return f'intops.mod_trunc {t}. a. b. .', None
    return None, 'language-policy'


def param_types(case):
    """the Lowent type of each operand."""
    op, t = case['op'], case['type']
    n = len(case['rows'][0]) - 1
    if op in ('div_const', 'rem_const'):
        return [t]
    if op in ('shl', 'shr', 'rotl', 'rotr'):
        return [t, 'u64']                       # shift counts are unsigned (canon §6.3.5 (3a)); the vectors go up to 2^64-1
    if op in ('cmp_eq', 'cmp_lt', 'cmp_le'):
        return [t, case['type_b']]
    return [t] * n


def result_type(case):
    op, t, d = case['op'], case['type'], case.get('disposition')
    if op in ('cmp_eq', 'cmp_lt', 'cmp_le'):
        return 'bool'
    if op in ('add_carry',):  return f'intops.carry_{t}'
    if op in ('sub_borrow',): return f'intops.borrow_{t}'
    if op == 'mul_wide':      return f'intops.wide_{t}'
    if op == 'mul_wide_s':    return f'intops.swide_{t}'
    return f'option {t}' if d == 'chk' else t


def expected_text(case, exp):
    """what lowentc prints for the expected result ('trap' stays 'trap')."""
    op, d = case['op'], case.get('disposition')
    if exp == 'trap':
        return 'trap'
    if isinstance(exp, bool):
        return '1' if exp else '0'
    if isinstance(exp, list):
        f = {'add_carry': ('sum', 'carry'), 'sub_borrow': ('diff', 'borrow'), 'mul_wide': ('lo', 'hi'), 'mul_wide_s': ('lo', 'hi')}[op]
        return '{' + ', '.join(f'{k} {v}' for k, v in zip(f, exp)) + '}'
    if d == 'chk':
        return 'none' if exp == 'none' else f'some {exp}'
    return str(exp)


def rows_of(suite_doc):
    """(global row number, case index, row) in harness-protocol §5.2 order — integer_ops: cases[].rows[]."""
    k = 0
    for ci, c in enumerate(suite_doc['cases']):
        for r in c['rows']:
            yield k, ci, r
            k += 1


def build_module(work, mi, cases):
    lines = [f'module bts{mi} .', f'build {BUILD_MODE} .', 'use intops .']
    for ci, c in cases:
        e, _ = lowent_expr(c)
        ps = ' '.join(f'input {v} {ty} .' for v, ty in zip('abc', param_types(c)))
        lines.append(f'fn c{ci} {ps} output {result_type(c)} . do return {e} . end .')
    src = work / f'bts{mi}.low'
    src.write_text('\n'.join(lines) + '\n')
    return src


def run_vm(src, calls):
    """calls: list of 'cN a b' → list of (rc, printed value or None)."""
    r = subprocess.run([str(LOWENTC), '--run-batch', str(src)], input='\n'.join(calls) + '\n', capture_output=True, text=True, cwd=PUB)
    out = r.stdout.split('\n\x1ebegin\n', 1)
    if len(out) != 2:
        return [(None, 'translate: ' + (r.stdout + r.stderr)[-300:])] * len(calls)
    res = []; body = out[1]
    for k in range(len(calls)):
        m = re.search(r'\n\x1e' + str(k) + r' (rc=(\d+)|sig=\d+|timeout)\n', body)
        if not m:
            res.append((None, 'no mark')); continue
        chunk, body = body[:m.start()], body[m.end():]
        rc = int(m.group(2)) if m.group(2) is not None else 128
        v = re.search(r' = (.*)$', chunk.strip().split('\n')[-1]) if chunk.strip() else None
        res.append((rc, v.group(1) if (v and rc == 0) else None))
    return res


def run_native(binp, call):
    r = subprocess.run([str(binp)] + call.split(' '), capture_output=True, text=True)
    v = re.search(r' = (.*)$', r.stdout.strip().split('\n')[-1]) if r.stdout.strip() else None
    return r.returncode, (v.group(1) if (v and r.returncode == 0) else None)


def judge(exp, rc, got):
    if rc is None:
        return 'error', got
    if exp == 'trap':
        return ('pass', None) if rc != 0 else ('fail', f'expected a trap, got {got}')
    if rc != 0:
        return 'fail', f'trapped (rc {rc}), expected {exp}'
    return ('pass', None) if got == exp else ('fail', f'got {got}, expected {exp}')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--suite', default='integer_ops')
    ap.add_argument('--out', default=str(PUB / 'impl/build/bts/results.jsonl'))
    ap.add_argument('--jobs', type=int, default=8)
    a = ap.parse_args()
    vf = VEC / f'{a.suite}.json'
    doc = json.loads(vf.read_text())
    if doc.get('schema') != 'bts-vectors/1':
        print(f'run: {vf.name}: unknown vector format {doc.get("schema")!r} — not starting'); return 2
    ver = subprocess.run([str(LOWENTC), '--version'], capture_output=True, text=True).stdout.split('\n')[0]
    head = {'schema': 'bts-results/2', 'runner': 'lowent impl/tests/bts/run.py', 'language': 'lowent', 'processor': 'lowentc',
            'processor_version': ver, 'build_mode': BUILD_MODE, 'target': TARGET,
            'vectors': {vf.name: hashlib.sha256(vf.read_bytes()).hexdigest()},
            'declaration': {'suites': {a.suite: 'supported'}, 'implementations': ['vm', 'native'], 'removes': [], 'erases_contracts': False}}
    cases = doc['cases']
    runnable = [(ci, c) for ci, c in enumerate(cases) if lowent_expr(c)[0]]
    work = pathlib.Path(tempfile.mkdtemp(prefix='bts-', dir=os.environ.get('TMPDIR')))
    mods = [runnable[i:i + MODULE_CASES] for i in range(0, len(runnable), MODULE_CASES)]
    results = {}   # (row, impl) -> (status, reason)

    def do_module(mi):
        mc = mods[mi]
        src = build_module(work, mi, mc)
        calls = []; rowids = []; mine = {ci for ci, _ in mc}
        for k, ci, r in rows_of(doc):
            if ci not in mine:
                continue
            c = cases[ci]
            if c.get('width') and int(c['width']) != USIZE_BITS:
                continue
            ops = r[:-1] if c['op'] not in ('div_const', 'rem_const') else r[:1]
            calls.append(f'c{ci} ' + ' '.join(str(x) for x in ops)); rowids.append((k, ci, r))
        out = {}
        vm = run_vm(src, calls)
        csrc = work / f'bts{mi}.c'; binp = work / f'bts{mi}'
        e = subprocess.run([str(LOWENTC), '--emit-c', str(src)], capture_output=True, text=True, cwd=PUB)
        nat_ok = e.returncode == 0
        if nat_ok:
            csrc.write_text(e.stdout)
            nat_ok = subprocess.run(['cc', '-O1', '-w', '-o', str(binp), str(csrc), '-lm'], capture_output=True).returncode == 0
        for (k, ci, r), (rc, got), call in zip(rowids, vm, calls):
            exp = expected_text(cases[ci], r[-1])
            out[(k, 'vm')] = judge(exp, rc, got)
            out[(k, 'native')] = judge(exp, *run_native(binp, call)) if nat_ok else ('error', 'native build failed')
        return out

    with ThreadPoolExecutor(a.jobs) as ex:
        for part in ex.map(do_module, range(len(mods))):
            results.update(part)

    outp = pathlib.Path(a.out); outp.parent.mkdir(parents=True, exist_ok=True)
    cnt = {'pass': 0, 'fail': 0, 'skip': 0, 'error': 0}
    with outp.open('w') as f:
        f.write(json.dumps(head, ensure_ascii=False) + '\n')
        for k, ci, r in rows_of(doc):
            c = cases[ci]
            label = f'{c["op"]}/{c["type"]}/{c.get("disposition")}'
            if c.get('width') and int(c['width']) != USIZE_BITS:
                rows = [{'status': 'skip', 'reason': 'target-unavailable'}]
            elif not lowent_expr(c)[0]:
                rows = [{'status': 'skip', 'reason': lowent_expr(c)[1]}]
            else:
                rows = []
                for impl in ('vm', 'native'):
                    st, why = results.get((k, impl), ('error', 'not run'))
                    rows.append({'status': st, 'impl': impl, **({'reason': why} if why else {})})
            for x in rows:
                f.write(json.dumps({'suite': a.suite, 'case': label, 'row': k, **x}, ensure_ascii=False) + '\n')
                cnt[x['status']] += 1
        conforms = cnt['fail'] == 0 and cnt['error'] == 0
        claim = (f'{a.suite} vectors:{head["vectors"][vf.name][:16]} target:{TARGET} mode:{BUILD_MODE} impl:vm+native') if conforms else None
        f.write(json.dumps({'summary': {a.suite: {**cnt, 'conforms': conforms, 'claim': claim}}}) + '\n')
    print(f'bts run: {a.suite} · pass {cnt["pass"]} · fail {cnt["fail"]} · skip {cnt["skip"]} · error {cnt["error"]} → {outp}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
