#!/usr/bin/env python3
"""Lowent runner for the basic_type_system (BTS) conformance vectors — RFC-0157 §8-1.

  python3 impl/tests/bts/run.py [--suite integer_ops] [--out impl/build/bts/results.jsonl] [--jobs 8]
  suites: see SUITES at the end of the plans

Reads the vector copies in conformance/vectors/ (their sha256 and the BTS commit are in conformance/SOURCES.tsv), turns
every case into a small Lowent function, and runs every row on both lowent implementations:

  vm      `lowentc --run-batch` — translated once per module, one forked run per row
  native  `lowentc --emit-c` + `cc`, then the binary once per row

A row that must trap is run on its own (harness-protocol §2: a trap does not hide the next row). A row judged by
translation (casting_rules `relations` · `common_type`) is its own module: `--check` stands for the VM, `--emit-c` for
native. The result file is `bts-results/2` (harness-protocol §5) and is checked with the copied `check_results.py`;
the row numbers follow §5.2 and are counted against the copied `gen.py`.

Mapping a BTS operation to Lowent: the case's `names.lowent` when the vectors give it, else a same-meaning spelling
written below (`wrap_sub 0 a` for a wrapping negation, `intops.*` for the named comparisons and the floor/truncating
division, `bit_cast` for float bit patterns). Skips: `language-policy` = Lowent has decided not to have it;
`not-implemented` = Lowent has no spelling for it yet (the suite then makes no claim); `target-unavailable` = a row
for another target.
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
MODULE_CASES = 40          # functions per generated module (one translation, one C build)


# ── the row items a suite plan yields ─────────────────────────────────────────────────────────────────────────────
class Skip:
    def __init__(self, reason): self.reason = reason

class Exec:
    """run function `fsrc` (keyed by `fkey`) with `args`; `expect` is 'trap', a printed text, or a predicate.
    `kw` is the declaring word (`fn` · `proc`); `pre` are helper declarations written before it, `{F}` standing for its name."""
    def __init__(self, fkey, fsrc, args, expect, uses=(), kw='fn', pre=()):
        self.fkey, self.fsrc, self.args, self.expect, self.uses = fkey, fsrc, [str(x) for x in args], expect, tuple(uses)
        self.kw, self.pre = kw, tuple(pre)

class Erase:
    """range_erasure: translate `src` (op `bts_e`), read the compiler's remaining-check list, and for a `keep` check run `args` and
    expect a stop. `kind` is the check's kind (ovf · nar · div · shc · idx); `ckey` groups the rows of one example."""
    def __init__(self, ckey, src, kind, floor, args):
        self.ckey, self.src, self.kind, self.floor, self.args = ckey, src, kind, floor, args

class Xlate:
    """translate `body` (op declarations) as its own module; `accept` says whether Lowent must take it."""
    def __init__(self, body, accept, uses=()):
        self.body, self.accept, self.uses = body, accept, tuple(uses)


# ── integer_ops ───────────────────────────────────────────────────────────────────────────────────────────────────
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


def value_text(exp, chk=False):
    """what lowentc prints for an expected value ('trap' stays 'trap')."""
    if exp == 'trap':
        return 'trap'
    if isinstance(exp, bool):
        return '1' if exp else '0'
    if chk:
        return 'none' if exp == 'none' else f'some {exp}'
    return str(exp)


def plan_integer_ops(doc):
    for ci, c in enumerate(doc['cases']):
        label = f'{c["op"]}/{c["type"]}/{c.get("disposition")}'
        e, why = lowent_expr(c)
        fsrc = None
        if e:
            ps = ' '.join(f'input {v} {ty} .' for v, ty in zip('abc', param_types(c)))
            fsrc = f'{ps} output {result_type(c)} . do return {e} . end .'
        for r in c['rows']:
            if c.get('width') and int(c['width']) != USIZE_BITS:
                yield label, Skip('target-unavailable'); continue
            if not e:
                yield label, Skip(why); continue
            exp = r[-1]
            if isinstance(exp, list):
                f = {'add_carry': ('sum', 'carry'), 'sub_borrow': ('diff', 'borrow'), 'mul_wide': ('lo', 'hi'),
                     'mul_wide_s': ('lo', 'hi')}[c['op']]
                txt = '{' + ', '.join(f'{k} {v}' for k, v in zip(f, exp)) + '}'
            else:
                txt = value_text(exp, c.get('disposition') == 'chk')
            ops = r[:-1] if c['op'] not in ('div_const', 'rem_const') else r[:1]
            yield label, Exec(('int', ci), fsrc, ops, txt, uses=['intops'])


# ── float_ops ─────────────────────────────────────────────────────────────────────────────────────────────────────
FBITS = {'f32': ('u32', 32), 'f64': ('u64', 64)}


def is_nan_bits(bits, width):
    e, m = (8, 23) if width == 32 else (11, 52)
    v = int(bits)
    return (v >> m) & ((1 << e) - 1) == (1 << e) - 1 and v & ((1 << m) - 1) != 0


def fbits_expect(hexv, width):
    if hexv == 'nan':
        return lambda got: got is not None and got.isdigit() and is_nan_bits(got, width)
    return str(int(hexv, 16))


def plan_float_ops(doc):
    for ci, c in enumerate(doc['cases']):
        op, t = c['op'], c['type']
        label = f'{op}/{t}'
        u, w = FBITS[t]
        fc = lambda v: f'bit_cast {t} {v}. .'
        uses = []
        if op in ('add', 'sub', 'mul', 'div'):
            fsrc = f'input a {u} . input b {u} . output {u} . do return bit_cast {u} {op} {fc("a")} {fc("b")} . . . end .'
        elif op == 'sqrt':
            uses = ['math']
            # f32: math.sqrt is f64 only; one more rounding through f64 is exact for sqrt (53 ≥ 2·24 + 2)
            inner = (f'cast f32 math.sqrt widen f64 {fc("a")} . . .' if t == 'f32' else f'math.sqrt {fc("a")} .')
            fsrc = f'input a {u} . output {u} . do return bit_cast {u} {inner} . . end .'
        elif op in ('lt', 'eq'):
            fsrc = f'input a {u} . input b {u} . output bool . do return {op} {fc("a")} {fc("b")} . . end .'
        elif op == 'copy':
            fsrc = f'input a {u} . output {u} . do let x {t} {fc("a")} . return bit_cast {u} x. . . end .'
        elif op == 'order_key':
            uses = ['fbits']     # BTS 02 §3.3 — library `fbits` (2026-11-15 · W-20261115-0229 D3)
            fsrc = f'input a {u} . output {u} . do return fbits.order_key_{t} {fc("a")} . . end .'
        elif op in ('sum_tree4', 'sum_tree8', 'sum_tree16'):
            uses = ['fbits']     # BTS 02 §3.4 — the array is written into the function (one function per row)
            fsrc = 'TREE'
        else:
            fsrc = None
        for ri, r in enumerate(c['rows']):
            if not fsrc:
                yield label, Skip('not-implemented'); continue
            exp = r[-1]
            want = value_text(exp) if isinstance(exp, bool) else fbits_expect(exp, w)
            if fsrc == 'TREE':
                xs = [int(x, 16) for x in r[0]]
                n = max(len(xs), 1)
                body = (f'output {u} . do var xs lit array {n} {t} _ . . '
                        + (f'let b lit array {len(xs)} {u} ' + ' '.join(map(str, xs)) + ' . . '
                           + f'repeat i usize {len(xs)} do set idx xs. i. . bit_cast {t} idx b. i. . . . end . ' if xs else '')
                        + f'return bit_cast {u} fbits.{op}_{t} ' + ('xs. ' if xs else 'subslice xs. 0 0 . ') + '. . . end .')
                yield label, Exec(('float', ci, ri), body, [], want, uses)
                continue
            args = [str(int(x, 16)) for x in r[:-1]]
            yield label, Exec(('float', ci), fsrc, args, want, uses)


# ── casting_rules ─────────────────────────────────────────────────────────────────────────────────────────────────
def width_of(t):
    return int(re.sub(r'\D', '', t))


def plan_casting_rules(doc):
    for x in doc['relations']:
        body = f'fn f input a {x["from"]} . output {x["to"]} . do return a. . end .'
        yield f'relations/{x["from"]}->{x["to"]}', Xlate(body, bool(x['implicit']['lowent']))
    for x in doc['common_type']:
        a, b, cmn = x['a'], x['b'], x['common']['lowent']
        out = cmn or x.get('lub_fixed_width') or a
        body = f'fn f input a {a} . input b {b} . output {out} . do return add a. b. . . end .'
        yield f'common_type/{a}+{b}', Xlate(body, cmn is not None)
    for ci, x in enumerate(doc['narrowing']):
        f, t, d = x['from'], x['to'], x['disposition']
        word = {'trap': 'narrow' if width_of(t) < width_of(f) else 'cast', 'wrap': 'narrow_wrap', 'sat': 'narrow_sat',
                'chk': 'narrow_try'}[d]
        out = f'option {t}' if d == 'chk' else t
        fsrc = f'input a {f} . output {out} . do return {word} {t} a. . . end .'
        for r in x['rows']:
            yield f'narrowing/{f}->{t}/{d}', Exec(('narrow', ci), fsrc, r[:1], value_text(r[1], d == 'chk'))
    for ci, x in enumerate(doc['f32_to_int']):
        t, d = x['to'], x['disposition']
        word = {'trap': 'cast', 'sat': 'narrow_sat', 'chk': 'narrow_try'}.get(d)
        out = f'option {t}' if d == 'chk' else t
        fsrc = f'input a u32 . output {out} . do return {word} {t} bit_cast f32 a. . . . end .' if word else None
        for r in x['rows']:
            if not fsrc:
                yield f'f32_to_int/{t}/{d}', Skip('not-implemented'); continue
            yield f'f32_to_int/{t}/{d}', Exec(('f2i', ci), fsrc, [str(int(r[0], 16))], value_text(r[1], d == 'chk'))
    for ci, x in enumerate(doc['int_to_f32']):
        f = x['from']
        fsrc = f'input a {f} . output u32 . do return bit_cast u32 cast f32 a. . . . end .'
        for r in x['rows']:
            yield f'int_to_f32/{f}', Exec(('i2f', ci), fsrc, r[:1], str(int(r[1], 16)))


# ── simd_ops ──────────────────────────────────────────────────────────────────────────────────────────────────────
def vec_text(lanes):
    return 'vec[' + ' '.join(str(x) for x in lanes) + ']'


def plan_simd_ops(doc):
    """lane cases: `lit vec N T` from the lane arguments, the Lowent lane op, the printed `vec[…]`. `to_int` is `bit_cast u<N·w>` of the
    vector (canon §8.7.1 (3b), lane 0 lowest), `lane_lookup16` is `lane_lookup` (§6.3.7 (1e)) — both 2026-11-25, RFC-0157 §8-18 · 19.
    `load_masked` · `store_masked` · `load_full`: simd_memory_rows. `kernels`: simd_kernel_rows (§8-20)."""
    for ci, c in enumerate(doc['cases']):
        op, d = c['op'], c.get('disposition')
        t, n = re.match(r'simd\((\w+),(\d+)\)', c['type']).groups(); n = int(n)
        label = f'{op}/{c["type"]}/{d}'
        if op in ('load_masked', 'store_masked', 'load_full'):
            yield from simd_memory_rows(ci, c, op, d, t, n, label); continue
        va = ' '.join(f'a{i}.' for i in range(n)); vb = ' '.join(f'b{i}.' for i in range(n))
        ins_a = ' '.join(f'input a{i} {t} .' for i in range(n)); ins_b = ' '.join(f'input b{i} {t} .' for i in range(n))
        lx = f'let x vec {n} {t} lit vec {n} {t} {va} . .'; ly = f'let y vec {n} {t} lit vec {n} {t} {vb} . .'
        fsrc = None
        if op in ('add', 'sub', 'mul'):
            word = {'wrap': op, 'sat': f'sat_{op}'}.get(d)
            if word:
                fsrc = f'{ins_a} {ins_b} output vec {n} {t} . do {lx} {ly} return {word} x. y. . . end .'
        elif op in ('and', 'or', 'xor') or (op in ('shl', 'shr') and d == 'wrap'):
            # 2026-11-25 (RFC-0157 §8-17 · W-20261125-1240): the scalar words, lane by lane (canon §6.2.11 (3a)); a shift's count lanes are
            # masked to the lane width — exactly the scalar `wrap_shl` · `wrap_shr`, which is BTS 03 §3.3 «k mod lane bits».
            word = {'and': 'bit_and', 'or': 'bit_or', 'xor': 'bit_xor', 'shl': 'wrap_shl', 'shr': 'wrap_shr'}[op]
            fsrc = f'{ins_a} {ins_b} output vec {n} {t} . do {lx} {ly} let r vec {n} {t} {word} x. y. . . return r. . end .'
        elif op in ('eq', 'lt'):
            ones = '-1' if t.startswith('i') else str((1 << int(t[1:])) - 1)
            fsrc = (f'{ins_a} {ins_b} output vec {n} {t} . do {lx} {ly} let t vec {n} {t} splat {ones} . . let f vec {n} {t} splat 0 . . '
                    f'return lane_select {op} x. y. . t. f. . . end .')
        elif op == 'to_int':
            # 2026-11-25 (RFC-0157 §8-18 · 소유자 «18~20번 모두 추천대로») — the unsigned integer of the vector's size, lane 0 lowest
            fsrc = f'{ins_a} output u{n * int(t[1:])} . do {lx} return bit_cast u{n * int(t[1:])} x. . . end .'
        elif op == 'lane_lookup16':
            # 2026-11-25 (RFC-0157 §8-19) — `lane_lookup <table> <index>`; an index of 16 or more gives 0
            fsrc = f'{ins_a} {ins_b} output vec {n} {t} . do {lx} {ly} let r vec {n} {t} lane_lookup x. y. . . return r. . end .'
        elif op in ('reduce_sum', 'reduce_min', 'reduce_max'):
            word = {'reduce_sum': 'reduce_add'}.get(op, op)   # reduce_add stops when the sum does not fit (canon §6.3.7 (2))
            fsrc = f'{ins_a} output {t} . do {lx} return {word} x. . . end .'
        for r in c['rows']:
            if not fsrc:
                yield label, Skip('not-implemented'); continue
            if op.startswith('reduce_') or op == 'to_int':
                yield label, Exec(('simd', ci), fsrc, r[0], value_text(r[1]))
            elif op == 'lane_lookup16':
                yield label, Exec(('simd', ci), fsrc, list(r[0]) + list(r[1]), vec_text(r[2]))
            else:
                b = list(r[1])
                if op in ('shl', 'shr'):
                    # a count lane is `t` in Lowent, and the vectors' counts go up to 2^64-1: write k mod 2^w (as `t`) — it keeps
                    # k mod w, the only part a wrapping lane shift reads (w divides 2^w)
                    w = int(t[1:]); b = [str(((int(k) % (1 << w)) ^ (1 << (w - 1))) - (1 << (w - 1)) if t[0] == 'i' else int(k) % (1 << w)) for k in b]
                yield label, Exec(('simd', ci), fsrc, list(r[0]) + b, 'trap' if r[2] == 'trap' else vec_text(r[2]))
    for ki, k in enumerate(doc.get('kernels', [])):
        yield from simd_kernel_rows(ki, k)


# 2026-11-25 (RFC-0157 §8-20 · 소유자 «18~20번 모두 추천대로» — 권고 ⓐ): BTS writes each kernel once over a vector of unknown length and runs it
# at 1 · 4 · 16 lanes. Lowent spells that as ONE op whose lane count is a `comptime` input (canon §6.2.11 (1), §6.2.6 (1b-1)) — no new word —
# called once per lane count; the row passes when every call gives the expected answer (judged in Lowent, as store_masked). The body:
# full vectors with `load`, the tail one element at a time, and a lane fold through `store` into a 16-slot buffer (`lane_rotate` takes a
# literal count only, and `reduce_add` stops on overflow where the kernel wraps).
KERNELS = {
    'sum_u32_wrap': ('u32', """fn {F}_k input comptime n usize . input s slice u32 . output u32 . do
  var acc vec n u32 splat 0 . .
  var i usize 0 .
  while le add i. n. . len s. . . do let v vec n u32 load s. i. . . set acc. add acc. v. . . set i. add i. n. . . end .
  var ba lit array 16 u32 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 . .
  let b mut slice u32 subslice ba. 0 n. . .
  store b. 0 acc. .
  var r u32 0 .
  var j usize 0 .
  while lt j. n. . do set r. wrap_add r. idx b. j. . . . set j. add j. 1 . . end .
  while lt i. len s. . . do set r. wrap_add r. idx s. i. . . . set i. add i. 1 . . end .
  return r. .
end ."""),
    'add_sat_u8': ('u8', """proc {F}_k input comptime n usize . input s slice u8 . input o mut slice u8 . output bool . effects none . do
  let m usize div len s. . 2 . .
  var i usize 0 .
  while le add i. n. . m. . do
    let x vec n u8 load s. i. . .
    let y vec n u8 load s. add m. i. . . .
    let z vec n u8 sat_add x. y. . .
    store o. i. z. .
    set i. add i. n. . .
  end .
  while lt i. m. . do set idx o. i. . sat_add idx s. i. . idx s. add m. i. . . . . set i. add i. 1 . . end .
  return true .
end ."""),
    'count_eq_u8': ('u8', """fn {F}_k input comptime n usize . input s slice u8 . output u64 . do
  let f u8 idx s. 0 . .
  let fv vec n u8 splat f. . .
  let one vec n u8 splat 1 . .
  let zero vec n u8 splat 0 . .
  var ba lit array 16 u8 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 . .
  let b mut slice u8 subslice ba. 0 n. . .
  var r u64 0 .
  var i usize 1 .
  while le add i. n. . len s. . . do
    let x vec n u8 load s. i. . .
    let c vec n u8 lane_select eq x. fv. . one. zero. . .
    store b. 0 c. .
    var j usize 0 .
    while lt j. n. . do set r. add r. widen u64 idx b. j. . . . . set j. add j. 1 . . end .
    set i. add i. n. . .
  end .
  while lt i. len s. . . do
    if eq idx s. i. . f. . do set r. add r. 1 . . end .
    set i. add i. 1 . .
  end .
  return r. .
end ."""),
    'max_i16': ('i16', """fn {F}_k input comptime n usize . input s slice i16 . output i16 . do
  var acc vec n i16 splat idx s. 0 . . .
  var i usize 0 .
  while le add i. n. . len s. . . do let x vec n i16 load s. i. . . set acc. max acc. x. . . set i. add i. n. . . end .
  var r i16 reduce_max acc. . .
  while lt i. len s. . . do set r. max r. idx s. i. . . . set i. add i. 1 . . end .
  return r. .
end ."""),
}


def simd_kernel_rows(ki, k):
    name, label = k['kernel'], f'kernel/{k["kernel"]}'
    if name not in KERNELS or k.get('type') != KERNELS[name][0]:
        yield label, Skip('not-implemented'); return
    t, pre = KERNELS[name]
    xs, want, lanes = k['input'], k['expected'], k['lane_counts']
    m = len(xs)
    body = f'output bool . effects none . do let a lit array {m} {t} {" ".join(xs)} . . let s slice {t} subslice a. 0 {m} . . '
    if name == 'add_sat_u8':
        h = len(want)
        body += (f'let e lit array {h} {t} {" ".join(want)} . . var oa lit array {h} {t} {" ".join("0" for _ in want)} . . '
                 f'let o mut slice {t} subslice oa. 0 {h} . . ')
        for n in lanes:
            body += (f'let d{n} bool {{F}}_k {n} s. o. . . '
                     f'repeat i usize {h} do guard eq idx o. i. . idx e. i. . . else return false . . end . ')
    else:
        for n in lanes:
            body += f'guard eq {{F}}_k {n} s. . {want[0]} . else return false . . '
    body += 'return true . end .'
    yield label, Exec(('kernel', ki), body, [], '1', kw='proc', pre=(pre,))


def simd_memory_rows(ci, c, op, d, t, n, label):
    """2026-11-25 (W-20261125-0930 · RFC-0157 «남은 작업 진행»): the three memory ops map to words Lowent already has (canon §6.3.7).
    The slice is written into the function (one function per row, as `sum_tree`). `load_masked` · `store_masked` take a `mask n`;
    the row's `n` becomes the mask «lane < min(n, len s)» — computed IN Lowent (`min` · `lt` over a lane-index vector), so the
    tail-safety the vectors test (lanes past the slice are not touched) is the processor's, not the runner's. `load_full` trap = plain
    `load` (stops when the slice is short); chk = `guard ge len s. n.` then `load` — Lowent has no checked-load word, so that row
    measures a two-word spelling, not one op. `store_masked` is judged in Lowent: the function compares the whole slice to the
    expected bytes and gives back `true`."""
    def arr(name, xs, mut):
        k = max(len(xs), 1)
        body = ' '.join(xs) if xs else '_'
        return (f'{"var" if mut else "let"} {name}a lit array {k} {t} {body} . . '
                f'let {name} {"mut " if mut else ""}slice {t} subslice {name}a. 0 {len(xs)} . . ')
    ix = f'let ix vec {n} {t} lit vec {n} {t} ' + ' '.join(str(i) for i in range(n)) + ' . . '
    def mask(k):
        return (ix + f'let k {t} cast {t} min len s. . {k} . . . let kv vec {n} {t} splat k. . . '
                f'let m mask {n} lt ix. kv. . . ')
    for ri, r in enumerate(c['rows']):
        if op == 'load_masked':
            xs, k, want = r
            body = (f'output vec {n} {t} . do ' + arr('s', xs, False) + mask(k)
                    + f'let z vec {n} {t} splat 0 . . let r vec {n} {t} load_masked s. 0 m. z. . . return r. . end .')
            yield label, Exec(('simd', ci, ri), body, [], vec_text(want))
        elif op == 'store_masked':
            xs, v, k, after = r
            body = (f'output bool . effects none . do ' + arr('s', xs, True) + mask(k)
                    + f'let v vec {n} {t} lit vec {n} {t} ' + ' '.join(v) + ' . . store_masked s. 0 v. m. . '
                    + (f'let e lit array {len(after)} {t} ' + ' '.join(after) + ' . . '
                       + f'repeat i usize {len(after)} do guard eq idx s. i. . idx e. i. . . else return false . . end . ' if after else '')
                    + 'return true . end .')
            yield label, Exec(('simd', ci, ri), body, [], '1', kw='proc')
        else:
            xs, want = r
            if d == 'trap':
                body = f'output vec {n} {t} . do ' + arr('s', xs, False) + f'let r vec {n} {t} load s. 0 . . return r. . end .'
                yield label, Exec(('simd', ci, ri), body, [], 'trap' if want == 'trap' else vec_text(want))
            else:
                body = (f'output option vec {n} {t} . do ' + arr('s', xs, False)
                        + f'guard ge len s. . {n} . else return none . . let r vec {n} {t} load s. 0 . . return some r. . . end .')
                yield label, Exec(('simd', ci, ri), body, [], 'none' if want == 'none' else 'some ' + vec_text(want))


# ── litmus ────────────────────────────────────────────────────────────────────────────────────────────────────────
MO = {'rlx': 'relaxed', 'acq': 'acquire', 'rel': 'release', 'acq_rel': 'acq_rel', 'sc': 'seq_cst'}


def fold_add(terms):
    e = terms[0]
    for t in terms[1:]:
        e = f'add {e} {t} .'
    return e


def plan_litmus(doc):
    """each test is one `proc`: its threads are flows in a `task_group` sharing a `mut slice u64` through `atomic_*` with the test's
    orders; every register and the final memory come back packed four bits each into one u64. Judged per test (harness-protocol §2):
    every row of a test passes when the observed outcome is in `allowed`. ★ Lowent's flows are green threads on ONE OS thread, so an
    outcome is always some sequential interleaving: a pass shows no forbidden outcome appeared, not that a weak one cannot."""
    for ti, t in enumerate(doc['tests']):
        locs = sorted({i.get('st') or i.get('ld') or i.get('loc') for th in t['threads'] for i in th if 'fence' not in i})
        regs = sorted({i['r'] for th in t['threads'] for i in th if 'r' in i})
        li = {x: k for k, x in enumerate(locs)}; ri = {x: k for k, x in enumerate(regs)}
        pos = lambda k: 1 << (4 * k)
        pre = []
        for j, th in enumerate(t['threads']):
            body = []
            mine = [i['r'] for i in th if 'r' in i]
            body += [f'var {r} u64 0 .' for r in mine]
            for i in th:
                if 'fence' in i:
                    body.append(f'atomic_fence order {MO[i["fence"]]} .')
                elif 'st' in i:
                    body.append(f'atomic_store m. {li[i["st"]]} {i["v"]} order {MO[i["mo"]]} .')
                elif 'ld' in i:
                    body.append(f'set {i["r"]}. atomic_load m. {li[i["ld"]]} order {MO[i["mo"]]} . .')
                else:
                    word = {'fadd': 'atomic_add', 'xchg': 'atomic_swap'}[i['rmw']]
                    body.append(f'set {i["r"]}. {word} m. {li[i["loc"]]} {i["v"]} order {MO[i["mo"]]} . .')
            packed = fold_add([f'mul {pos(ri[r])} {r}. .' for r in mine]) if mine else '0'
            pre.append(f'proc {{F}}_t{j} input k cap atomic . input m mut slice u64 . output u64 . effects atomic . do '
                       + ' '.join(body) + f' return {packed} . end .')
        n = len(t['threads'])
        main = (f'input k cap atomic . output u64 . effects state atomic . do var m lit array {len(locs)} u64 '
                + ' '.join('0' for _ in locs) + ' . . '
                + ' '.join(f'var h{j} task u64 0 .' for j in range(n))
                + ' task_group do ' + ' '.join(f'set h{j}. spawn {{F}}_t{j} k. m. . .' for j in range(n)) + ' end . '
                + 'return ' + fold_add([f'await h{j}. .' for j in range(n)]
                                       + [f'mul {pos(len(regs) + li[x])} atomic_load m. {li[x]} . .' for x in locs]) + ' . end .')
        allowed = {(tuple(sorted(a['regs'].items())), tuple(sorted(a['mem'].items()))) for a in t['allowed']}

        def ok(got, regs=regs, locs=locs, allowed=allowed):
            if got is None or not got.isdigit():
                return False
            v = int(got)
            rr = tuple(sorted((r, str((v >> (4 * k)) & 15)) for k, r in enumerate(regs)))
            mm = tuple(sorted((x, str((v >> (4 * (len(regs) + k))) & 15)) for k, x in enumerate(locs)))
            return (rr, mm) in allowed
        for _ in t['allowed']:
            yield f'litmus/{t["name"]}', Exec(('litmus', ti), main, [], ok, kw='proc', pre=pre)


# ── range_erasure (RFC-0157 §8-2 · §8-6 · W-20261116-0607) ───────────────────────────────────────────────────────
def plan_range_erasure(doc):
    import erasure as E   # the example → Lowent adapter and the remaining-check reader (impl/tests/bts/erasure.py)
    for ei, ex in enumerate(doc['examples']):
        src = E.translate(ex)
        names = [n for n, _ in ex['params']] + [n + '_in' for n, _, _ in ex.get('calls', [])]
        types = {n: t for n, t in ex['params']}
        for c in ex['checks']:
            label = f'range_erasure/{ex["name"].split(" ")[0]}/{c["id"]}'
            args = None
            if c['floor'] == 'keep':
                fo = c['fires_on'] or {'args': {}, 'lens': {}}
                av = dict(fo.get('args', {}))
                for n, _, _ in ex.get('calls', []):
                    if n in av: av[n + '_in'] = av.pop(n)
                args = []
                for n in names:
                    if types.get(n) == 'slice':
                        args.append('[' + ','.join('0' for _ in range(fo.get('lens', {}).get(n, 0))) + ']')
                    else:
                        args.append(str(av.get(n, 0)))
            yield label, Erase(('erase', ei), src, c['kind'], c['floor'], args)


# ── programs (RFC-0157 §8-6 · W-20261115-1055) ──────────────────────────────────────────────────────────────────────
def plan_programs(doc):
    import programs as P   # the notation → Lowent adapter (impl/tests/bts/programs.py)
    for ex in doc['examples']:
        src, why = P.translate(ex, TARGET)
        label = f'programs/{ex["name"]}'
        if not src:
            yield label, Skip(why[0]); continue
        yield label, Xlate(src, ex['verdict'] == 'accept')


# ── build_modes (RFC-0157 §8-6 · W-20261116-1135) ───────────────────────────────────────────────────────────────────
# Lowent removes a check only when it is proved: no build mode removes the arithmetic checks (canon §6.4.7 (4b)), so the
# `erased` column has no mode to run in (harness protocol: `language-policy`) and every row is judged by its `kept` column,
# in `debug`. `declaration.removes` stays empty.
BM_OP = {'add': 'add a. b. .', 'sub': 'sub a. b. .', 'mul': 'mul a. b. .', 'neg': 'neg a. .', 'abs': 'abs a. .',
         'shl': 'shl a. b. .', 'shr': 'shr a. b. .', 'div_trunc': 'div a. b. .'}


def plan_build_modes(doc):
    for ci, c in enumerate(doc['checks']):
        op, t, chk = c['op'], c['type'], c['check']
        label = f'build_modes/{chk}/{op}/{t}' + (f'/{c["from"]}' if c.get('from') else '')
        if op in BM_OP:
            ps = [t, 'u64'] if op in ('shl', 'shr') else [t] * (1 if op in ('neg', 'abs') else 2)
            fsrc = ' '.join(f'input {v} {ty} .' for v, ty in zip('ab', ps)) + f' output {t} . do return {BM_OP[op]} . end .'
            for r in c['rows']:
                yield label, Exec(('bm', ci), fsrc, r[:-2], value_text(r[-2]))
        elif op == 'narrow':
            fsrc = f'input a {c["from"]} . output {t} . do return narrow {t} a. . . end .'
            for r in c['rows']:
                yield label, Exec(('bm', ci), fsrc, r[:1], value_text(r[-2]))
        elif op == 'f32_to_int':
            fsrc = f'input a u32 . output {t} . do return cast {t} bit_cast f32 a. . . . end .'   # float → integer is `cast` (canon §6.2.16 (1a))
            for r in c['rows']:
                yield label, Exec(('bm', ci), fsrc, [str(int(r[0], 16))], value_text(r[-2]))
        elif op == 'index':
            # [index, length]: the slice is written as a literal of that length (`[0,0,0,0]`)
            fsrc = 'input s slice u8 . input i usize . output u8 . do return idx s. i. . . end .'
            for r in c['rows']:
                arr = '[' + ','.join('0' for _ in range(int(r[1]))) + ']'
                yield label, Exec(('bm', ci), fsrc, [arr, r[0]], 'trap' if r[-2] == 'trap' else (lambda g: True))
        elif op == 'match':
            # [tag, variants]: BTS counts tags from 0; a Lowent variant number is written, so the enum numbers its variants 0 … n−1
            # (canon §6.2.7 (2c) — a written number) and `cast` to it stops on a number no variant has (canon §6.2.16 (5a))
            n = int(c['rows'][0][1])
            vs = ' '.join(f'v{i} {i} .' if i == 0 else f'v{i} .' for i in range(n))
            pre = [f'enum {{F}}_e do {vs} end .']
            fsrc = f'input g u16 . output u16 . do let e {{F}}_e cast {{F}}_e g. . . return cast u16 e. . . end .'
            for r in c['rows']:
                yield label, Exec(('bm', ci), fsrc, r[:1], 'trap' if r[-2] == 'trap' else r[0], pre=pre)
        else:
            for r in c['rows']:
                yield label, Skip('not-implemented')


# ── canonical_encoding (RFC-0157 §8-6 · W-20261116-1135) ────────────────────────────────────────────────────────────
# `encode_canonical <struct> <value>` writes the canonical bytes (canon §6.2.13 (4a) · W-20261116-1800 D1): fields with no gap, little-endian,
# −0 as +0 and every NaN as one pattern; a one-field struct gives the bytes of that field. The op returns the bytes folded into one u64 — acc = len, then acc = acc·256 + byte, wrapping — and the runner folds the
# expected bytes the same way.
CE_INT = {'i8', 'i16', 'i32', 'i64', 'u8', 'u16', 'u32', 'u64'}
CE_FOLD = ('var acc u64 cast u64 len b. . . . for y b. do set acc. wrap_add wrap_mul acc. 256 . widen u64 y. . . . end . return acc. .')


def ce_fold(hexs):
    bs = bytes.fromhex(hexs)
    acc = len(bs)
    for y in bs:
        acc = (acc * 256 + y) % (1 << 64)
    return str(acc)


def plan_canonical_encoding(doc):
    for ri, r in enumerate(doc['rows']):
        t, v = r['type'], r['value']
        label = f'canonical_encoding/{t}'
        want = ce_fold(r['bytes'])
        if t in CE_INT or t in ('usize', 'idiff'):
            if r.get('width') and int(r['width']) != USIZE_BITS:
                yield label, Skip('target-unavailable'); continue
            pre = [f'struct {{F}}_w do v {t} . end .']
            fsrc = f'input x {t} . output u64 . do let b slice u8 encode_canonical {{F}}_w lit {{F}}_w do v x. . end . . . {CE_FOLD} end .'
            yield label, Exec(('ce', t), fsrc, [v], want, pre=pre)
        elif t in ('f32', 'f64'):
            bt = 'u32' if t == 'f32' else 'u64'
            pre = [f'struct {{F}}_w do v {t} . end .']
            fsrc = (f'input x {bt} . output u64 . do let b slice u8 encode_canonical {{F}}_w lit {{F}}_w do v bit_cast {t} x. . . end . . . '
                    f'{CE_FOLD} end .')
            yield label, Exec(('ce', t), fsrc, [str(int(v, 16))], want, pre=pre)
        elif t == 'bool':
            pre = ['struct {F}_w do v bool . end .']
            fsrc = f'input x u8 . output u64 . do let b slice u8 encode_canonical {{F}}_w lit {{F}}_w do v ne x. 0 . . end . . . {CE_FOLD} end .'
            yield label, Exec(('ce', t), fsrc, ['1' if v else '0'], want, pre=pre)
        elif t.startswith('struct{'):
            fts = t[len('struct{'):-1].split(',')
            pre = ['struct {F}_w do ' + ' '.join(f'f{i} {ft} .' for i, ft in enumerate(fts)) + ' end .']
            ins = ' '.join(f'input x{i} {"u8" if ft == "bool" else ft} .' for i, ft in enumerate(fts))
            flds = ' '.join(f'f{i} {"ne x%d. 0 ." % i if ft == "bool" else "x%d." % i} .' for i, ft in enumerate(fts))
            fsrc = f'{ins} output u64 . do let b slice u8 encode_canonical {{F}}_w lit {{F}}_w do {flds} end . . . {CE_FOLD} end .'
            args = [('1' if a else '0') if isinstance(a, bool) else a for a in v]
            yield label, Exec(('ce', t), fsrc, args, want, pre=pre)
        elif t.startswith('Option(') or t.startswith('Result('):
            yield label, Skip('language-policy')      # an option / result has no fixed bytes (canon §6.2.13 (8)) — `encode` refuses it
        elif t == 'char':
            yield label, Skip('not-implemented')      # a `char` is not a layout field (canon §6.2.13 (8)) and has no `cast` from u32 yet
        else:
            yield label, Skip('not-implemented')      # Q formats: Lowent has no fixed-point types


# ── struct_layout (RFC-0157 §8-6 · W-20261116-1135) ─────────────────────────────────────────────────────────────────
# Read through the language's own queries (W-20261116-1800 D3): `size_of`, `align_of` and `offset_of <struct> <field>`. The padding is
# what the offsets and sizes leave, so it is not read separately. `packed` is `layout packed .` and a written alignment is `align n .`.
# Lowent has no unions yet (`not-implemented`); it keeps fields in the declared order, so the BTS `default` layout that reorders is not
# Lowent's (`language-policy`).
SL_SCALAR = {'u8', 'u16', 'u32', 'u64', 'i8', 'i16', 'i32', 'i64', 'f32', 'f64', 'bool', 'usize', 'idiff'}


def sl_type(ft, structs):
    """the Lowent type of a BTS field type, or None"""
    if ft in SL_SCALAR:
        return ft
    m = re.fullmatch(r'array\((\w+),(\d+)\)', ft)
    if m and m.group(1) in SL_SCALAR and int(m.group(2)) > 0:
        return f'array {m.group(2)} {m.group(1)}'
    if ft.startswith('struct:'):
        d = structs.get(ft[7:])
        if d is not None and sl_ok(d, structs):
            return '{F}_' + ft[7:]
    return None


def sl_ok(d, structs):
    return (d.get('layout') in ('ffi', 'packed') and not d.get('union') and isinstance(d.get('align', 1), int) and
            all(sl_type(ft, structs) for _, ft in d['fields']))


def sl_decls(name, structs, out, seen):
    if name in seen:
        return
    d = structs[name]
    for _, ft in d['fields']:
        if ft.startswith('struct:'):
            sl_decls(ft[7:], structs, out, seen)
    seen.add(name)
    out.append('struct {F}_' + name + ' do ' + ('layout packed . ' if d.get('layout') == 'packed' else '') +
               (f'align {d["align"]} . ' if 'align' in d else '') + ' '.join(f'{n} {sl_type(ft, structs)} .' for n, ft in d['fields']) + ' end .')


def sl_fold(vals):
    acc = 0
    for v in vals:
        acc = (acc * 1000003 + int(v)) % (1 << 64)
    return str(acc)


def plan_struct_layout(doc):
    structs = {s['name']: s for s in doc['structs']}
    for s in doc['structs']:
        for tn, res in s['results'].items():
            label = f'struct_layout/{s["name"]}/{tn}'
            if tn != TARGET:
                yield label, Skip('target-unavailable'); continue
            if s.get('layout') == 'default' and s.get('reorder'):
                yield label, Skip('language-policy'); continue
            if 'rejected' in res or not sl_ok(s, structs):
                yield label, Skip('not-implemented'); continue
            pre = []
            sl_decls(s['name'], structs, pre, set())
            S = '{F}_' + s['name']
            body = [f'var acc u64 cast u64 size_of {S} . . .',
                    f'set acc. wrap_add wrap_mul acc. 1000003 . cast u64 align_of {S} . . . .']
            for fn_, ft in s['fields']:
                body.append(f'set acc. wrap_add wrap_mul acc. 1000003 . cast u64 offset_of {S} {fn_} . . . .')
            fsrc = 'input x u8 . output u64 . do ' + ' '.join(body) + ' return acc. . end .'
            want = sl_fold([res['size'], res['align']] + res['offsets'])
            yield label, Exec(('sl', s['name']), fsrc, ['0'], want, pre=pre)
    for b in doc['builtins']:
        for tn, res in b['results'].items():
            label = f'struct_layout/builtin/{b["type"]}/{tn}'
            if tn != TARGET:
                yield label, Skip('target-unavailable'); continue
            lt = sl_type(b['type'], structs) if b['type'] in SL_SCALAR or b['type'].startswith('array(') else None
            if b['type'] == 'array(u8,0)':
                yield label, Skip('language-policy'); continue      # an array holds at least one element (canon §6.2.6)
            if b['type'].startswith(('Option(', 'Result(', 'slice', 'ref(', 'mutref(')):
                yield label, Skip('language-policy'); continue      # no fixed bytes (canon §6.2.13 (8))
            if not lt or 'unavailable' in res:
                yield label, Skip('not-implemented'); continue
            pre = [f'struct {{F}}_one do v {lt} . end .']
            fsrc = (f'input x u8 . output u64 . do var acc u64 cast u64 size_of {{F}}_one . . . '
                    f'set acc. wrap_add wrap_mul acc. 1000003 . cast u64 align_of {{F}}_one . . . . return acc. . end .')
            yield label, Exec(('slb', b['type']), fsrc, ['0'], sl_fold([res['size'], res['align']]), pre=pre)
    for b in doc['bitfields']:
        for _ in b['rows']:
            yield f'struct_layout/bitfield/{b["name"]}', Skip('not-implemented')   # no bit-field struct in Lowent


# ── alloc_sequences (RFC-0157 §8-6 · W-20261116-1135) ───────────────────────────────────────────────────────────────
# The steps observe a block's offset from the region start; Lowent's allocators hand out slices, and a slice's address is not a
# value (the word for it, `addr`, is decided and not built — canon §6.9.4): `not-implemented`. `array_sizes` is the library's size
# arithmetic `allocs.array_bytes count elem` (none above the largest idiff — canon §8.13 (2c)). Rows for 16- and 32-bit usize are
# `target-unavailable`.
def plan_alloc_sequences(doc):
    for sq in doc['sequences']:
        for _ in sq['steps']:
            yield f'alloc_sequences/{sq["name"]}', Skip('not-implemented')
    fsrc = 'input n usize . input e usize . output option usize . do return allocs.array_bytes n. e. . . end .'
    for r in doc['array_sizes']:
        label = f'alloc_sequences/array_sizes/{r["usize_bits"]}'
        if int(r['usize_bits']) != USIZE_BITS:
            yield label, Skip('target-unavailable'); continue
        (k, v), = r['expect'].items()
        yield label, Exec(('as',), fsrc, [r['count'], r['elem']], f'some {v}' if k == 'ok' else 'none', uses=['allocs'])


# ── result_sets (RFC-0157 §8-6 · W-20261116-1135) ───────────────────────────────────────────────────────────────────
# Only the bool scenarios have a Lowent spelling: bytes become a bool through `try_view` (canon §6.2.13 (9a)), which gives `none` for a
# byte that is not 0 or 1 — the value never exists, which this runner reports as the scenario's `trap`. The rest: a `result` has no
# bytes (`language-policy`); `char` from u32 is not built (`not-implemented`); every binding is written before it is read and padding
# bytes are not a struct's value, structs are not compared with `eq` (canon §6.3), and non-atomic data races are refused by the
# ownership rules (canon §8.12) — `language-policy`.
def plan_result_sets(doc):
    pre = ['struct {F}_b do v bool . end .']
    fsrc = ('input r u8 . output u8 . do var buf lit array 1 u8 0 . . set idx buf. 0 . r. . '
            'let o option view {F}_b try_view {F}_b buf. . . guard is_some o. . else return 9 . . '
            'let h view {F}_b some_value o. . . return cast u8 field h. v . . . end .')
    for sc in doc['scenarios']:
        label = f'result_sets/{sc["id"]}'
        if sc['kind'] == 'materialize' and sc['type'] == 'bool':
            raw = int(sc['bytes'], 16)
            twice = 'twice' in sc['id']
            def ok(g, allowed=sc['allowed'], twice=twice):
                v = 'trap' if g == '9' else (g == '1')
                return (v if v == 'trap' else ([v, v] if twice else v)) in allowed
            yield label, Exec(('rs',), fsrc, [str(raw)], ok, pre=pre)
        elif sc['kind'] == 'materialize' and sc['type'] == 'char':
            yield label, Skip('not-implemented')
        else:
            yield label, Skip('language-policy')


SUITES = {'range_erasure': plan_range_erasure, 'programs': plan_programs, 'integer_ops': plan_integer_ops, 'float_ops': plan_float_ops, 'casting_rules': plan_casting_rules,
          'simd_ops': plan_simd_ops, 'litmus': plan_litmus, 'build_modes': plan_build_modes,
          'canonical_encoding': plan_canonical_encoding, 'struct_layout': plan_struct_layout, 'alloc_sequences': plan_alloc_sequences,
          'result_sets': plan_result_sets}


# ── running ───────────────────────────────────────────────────────────────────────────────────────────────────────
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
        return 'fail', f'trapped (rc {rc}), expected {exp if isinstance(exp, str) else "a NaN"}'
    if callable(exp):
        return ('pass', None) if exp(got) else ('fail', f'got {got}, expected a NaN')
    return ('pass', None) if got == exp else ('fail', f'got {got}, expected {exp}')


def header_lines(name, uses):
    return [f'module {name} .', f'build {BUILD_MODE} .'] + [f'use {u} .' for u in sorted(uses)]


def plan(suite, doc):
    """the plan's rows, checked against the copied gen.py (harness-protocol §5.2 order and count)."""
    sys.path.insert(0, str(HERE / 'conformance'))
    import gen
    rows = list(SUITES[suite](doc))
    if len(rows) != len(gen.rows_of(doc)):
        raise SystemExit(f'run: {suite}: the plan has {len(rows)} rows, the vectors {len(gen.rows_of(doc))} — not starting')
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--suite', default='integer_ops', choices=sorted(SUITES))
    ap.add_argument('--out', default=str(PUB / 'impl/build/bts/results.jsonl'))
    ap.add_argument('--jobs', type=int, default=8)
    a = ap.parse_args()
    vf = VEC / f'{a.suite}.json'
    doc = json.loads(vf.read_text())
    if doc.get('schema') != 'bts-vectors/1':
        print(f'run: {vf.name}: unknown vector format {doc.get("schema")!r} — not starting'); return 2
    rows = plan(a.suite, doc)
    ver = subprocess.run([str(LOWENTC), '--version'], capture_output=True, text=True).stdout.split('\n')[0]
    head = {'schema': 'bts-results/2', 'runner': 'lowent impl/tests/bts/run.py', 'language': 'lowent', 'processor': 'lowentc',
            'processor_version': ver, 'build_mode': BUILD_MODE, 'target': TARGET,
            'vectors': {vf.name: hashlib.sha256(vf.read_bytes()).hexdigest()},
            'declaration': {'suites': {a.suite: 'supported'}, 'implementations': ['vm', 'native'], 'removes': [], 'erases_contracts': False}}
    work = pathlib.Path(tempfile.mkdtemp(prefix='bts-', dir=os.environ.get('TMPDIR')))

    # functions in first-use order, then modules of MODULE_CASES functions
    fname, fns = {}, []
    for _, it in rows:
        if isinstance(it, Exec) and it.fkey not in fname:
            fname[it.fkey] = f'c{len(fns)}'; fns.append(it)
    mods = [fns[i:i + MODULE_CASES] for i in range(0, len(fns), MODULE_CASES)]
    mod_of = {fname[f.fkey]: mi for mi, m in enumerate(mods) for f in m}
    calls_of = [[] for _ in mods]                 # (row number, call)
    for k, (_, it) in enumerate(rows):
        if isinstance(it, Exec):
            n = fname[it.fkey]
            calls_of[mod_of[n]].append((k, ' '.join([n] + it.args)))
    results = {}   # (row, impl) -> (status, reason)

    def do_module(mi):
        uses = set(u for f in mods[mi] for u in f.uses)
        lines = header_lines(f'bts{mi}', uses) + [l for f in mods[mi] for l in [p.replace('{F}', fname[f.fkey]) for p in f.pre]
                                                  + [f'{f.kw} {fname[f.fkey]} {f.fsrc}'.replace('{F}', fname[f.fkey])]]
        src = work / f'bts{mi}.low'; src.write_text('\n'.join(lines) + '\n')
        calls = calls_of[mi]
        vm = run_vm(src, [c for _, c in calls])
        csrc = work / f'bts{mi}.c'; binp = work / f'bts{mi}'
        e = subprocess.run([str(LOWENTC), '--emit-c', str(src)], capture_output=True, text=True, cwd=PUB)
        nat_ok = e.returncode == 0
        if nat_ok:
            csrc.write_text(e.stdout)
            nat_ok = subprocess.run(['cc', '-O1', '-w', '-o', str(binp), str(csrc), '-lm'], capture_output=True).returncode == 0
        out = {}
        for (k, call), (rc, got) in zip(calls, vm):
            exp = rows[k][1].expect
            out[(k, 'vm')] = judge(exp, rc, got)
            out[(k, 'native')] = judge(exp, *run_native(binp, call)) if nat_ok else ('error', 'native build failed')
        return out

    def do_xlate(k):
        it = rows[k][1]
        src = work / f'x{k}.low'
        src.write_text('\n'.join(header_lines(f'x{k}', it.uses) + [it.body]) + '\n')
        out = {}
        for impl, flag in (('vm', '--check'), ('native', '--emit-c')):
            r = subprocess.run([str(LOWENTC), flag, str(src)], capture_output=True, text=True, cwd=PUB)
            took = r.returncode == 0
            if took == it.accept:
                out[(k, impl)] = ('pass', None)
            else:
                lines = (r.stdout + r.stderr).strip().split('\n')
                diag = next((l.strip() for l in lines if ' E-' in l), lines[-1] if lines else '')
                out[(k, impl)] = ('fail', 'accepted, expected a rejection' if took else f'rejected, expected acceptance: {diag[:160]}')
        return out

    def do_erase(k):
        import erasure as E
        it = rows[k][1]
        src = work / f'e{k}.low'
        src.write_text('\n'.join(header_lines(f'e{k}', ()) + [it.src]) + '\n')
        ir = subprocess.run([str(LOWENTC), '--ir', str(src)], capture_output=True, text=True, cwd=PUB)
        if ir.returncode != 0:
            return {(k, impl): ('error', 'translate: ' + (ir.stdout + ir.stderr)[-200:]) for impl in ('vm', 'native')}
        left = [E.kind_of_irop(o) for o in E.kinds_left(ir.stdout)]
        listed = it.kind in left
        out = {}
        if it.floor == 'remove':
            st = ('pass', None) if not listed else ('fail', f'a `remove` check ({it.kind}) is still in the remaining-check list')
            return {(k, 'vm'): st, (k, 'native'): st}
        if not listed:
            st = ('fail', f'a `keep` check ({it.kind}) is not in the remaining-check list')
            return {(k, 'vm'): st, (k, 'native'): st}
        r = subprocess.run([str(LOWENTC), '--run', 'bts_e', str(src)] + it.args, capture_output=True, text=True, cwd=PUB)
        out[(k, 'vm')] = ('pass', None) if r.returncode != 0 and 'E-VM-' in r.stdout + r.stderr else ('fail', 'the kept check did not stop on its fires_on input')
        binp = work / f'e{k}'
        e = subprocess.run([str(LOWENTC), '--emit-c', str(src)], capture_output=True, text=True, cwd=PUB)
        if e.returncode != 0:
            out[(k, 'native')] = ('error', 'emit-c failed'); return out
        (work / f'e{k}.c').write_text(e.stdout)
        if subprocess.run(['cc', '-O1', '-w', '-o', str(binp), str(work / f'e{k}.c'), '-lm'], capture_output=True).returncode != 0:
            out[(k, 'native')] = ('error', 'native build failed'); return out
        rn = subprocess.run([str(binp), 'bts_e'] + it.args, capture_output=True, text=True)
        out[(k, 'native')] = ('pass', None) if rn.returncode != 0 else ('fail', 'the kept check did not stop on its fires_on input (native)')
        return out

    er = [k for k, (_, it) in enumerate(rows) if isinstance(it, Erase)]
    xl = [k for k, (_, it) in enumerate(rows) if isinstance(it, Xlate)]
    with ThreadPoolExecutor(a.jobs) as ex:
        for part in ex.map(do_module, range(len(mods))):
            results.update(part)
        for part in ex.map(do_xlate, xl):
            results.update(part)
        for part in ex.map(do_erase, er):
            results.update(part)

    outp = pathlib.Path(a.out); outp.parent.mkdir(parents=True, exist_ok=True)
    cnt = {'pass': 0, 'fail': 0, 'skip': 0, 'error': 0}
    with outp.open('w') as f:
        f.write(json.dumps(head, ensure_ascii=False) + '\n')
        for k, (label, it) in enumerate(rows):
            if isinstance(it, Skip):
                outs = [{'status': 'skip', 'reason': it.reason}]
            else:
                outs = []
                for impl in ('vm', 'native'):
                    st, why = results.get((k, impl), ('error', 'not run'))
                    outs.append({'status': st, 'impl': impl, **({'reason': why} if why else {})})
            for x in outs:
                f.write(json.dumps({'suite': a.suite, 'case': label, 'row': k, **x}, ensure_ascii=False) + '\n')
                cnt[x['status']] += 1
        ni = sum(1 for _, it in rows if isinstance(it, Skip) and it.reason == 'not-implemented')
        conforms = cnt['fail'] == 0 and cnt['error'] == 0 and ni == 0
        claim = (f'{a.suite} vectors:{head["vectors"][vf.name][:16]} target:{TARGET} mode:{BUILD_MODE} impl:vm+native') if conforms else None
        f.write(json.dumps({'summary': {a.suite: {**cnt, 'conforms': conforms, 'claim': claim}}}) + '\n')
    print(f'bts run: {a.suite} · pass {cnt["pass"]} · fail {cnt["fail"]} · skip {cnt["skip"]}'
          + (f' (not-implemented {ni})' if ni else '') + f' · error {cnt["error"]} → {outp}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
