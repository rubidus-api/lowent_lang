#!/usr/bin/env python3
"""Generate the conformance vectors from the reference model.

    python3 -I docs/conformance/gen.py            # write vectors/*.json
    python3 -I docs/conformance/gen.py --check    # regenerate in memory and compare with the files

Output is deterministic: boundary values plus a fixed-seed sample. Integers are
written as decimal strings, f32 values as 8-digit hex bit patterns.
"""
import json
import os
import random
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "refmodel"))
import intmodel as im          # noqa: E402
import castmodel as cm         # noqa: E402
import layoutmodel as lm       # noqa: E402
import floatmodel as fm        # noqa: E402
import resultmodel as rsm      # noqa: E402
import allocmodel as am        # noqa: E402
import rc11model as rc         # noqa: E402
import litmus_tests as lt      # noqa: E402
import progmodel as pm         # noqa: E402
import program_examples as px  # noqa: E402
import simdmodel as sm         # noqa: E402
import fixmodel as fx          # noqa: E402
import bigmodel as bm          # noqa: E402
import decmodel as dm          # noqa: E402

SCHEMA = "bts-vectors/1"
SEED = 20261028
INT_TYPES = ["i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64"]
UNSIGNED = ["u8", "u16", "u32", "u64"]
SIGNED = ["i8", "i16", "i32", "i64"]


def s(v):
    if isinstance(v, bool):
        return v
    if isinstance(v, int):
        return str(v)
    if isinstance(v, list):
        return [s(x) for x in v]
    return v          # "trap" / "none"


def values(tn, rng, extra=6):
    t = im.TYPES[tn]
    vs = {t.min, t.min + 1, -1 if t.signed else 1, 0, 1, 2, t.max - 1, t.max, t.max // 2, (t.max // 2) + 1}
    if t.signed:
        vs |= {-2, t.min // 2, t.min // 2 - 1}
    vs |= {rng.randint(t.min, t.max) for _ in range(extra)}
    return sorted(v for v in vs if t.contains(v))


def case(op, tn, disposition, rows, **extra):
    c = {"op": op, "type": tn, "disposition": disposition, "timing": im.timing(op, disposition),
         "names": im.surface(op, disposition, tn if tn in im.TYPES else None), "rows": rows}
    c.update(extra)
    return c


def integer_ops():
    rng = random.Random(SEED)
    cases = []
    for tn in INT_TYPES:
        t = im.TYPES[tn]
        vs = values(tn, rng)
        divisors = sorted(set(vs) | {0, 1, -1 if t.signed else 2, 3, 7})
        divisors = [d for d in divisors if t.contains(d)]
        for op in ("add", "sub", "mul"):
            for d in im.ADMITS[op]:
                cases.append(case(op, tn, d, [s([a, b, im.binary(op, t, a, b, d)]) for a in vs for b in vs]))
        if t.signed:
            for op in ("neg", "abs"):
                for d in im.ADMITS[op]:
                    cases.append(case(op, tn, d, [s([a, im.unary(op, t, a, d)]) for a in vs]))
        for op in sorted(im.DIVISION):
            for d in im.ADMITS[op]:
                cases.append(case(op, tn, d, [s([a, b, im.binary(op, t, a, b, d)]) for a in vs for b in divisors]))
        counts = [0, 1, t.bits // 2, t.bits - 1, t.bits, t.bits + 1, 2 * t.bits + 3, (1 << 32) - 1, (1 << 64) - 1]
        for op in ("shl", "shr"):
            for d in ("trap", "wrap"):
                cases.append(case(op, tn, d, [s([a, k, im.shift(op, t, a, k, d)]) for a in vs for k in counts],
                                  note="disposition applies to the count; shl drops bits shifted out"))
        for op in ("rotl", "rotr"):
            cases.append(case(op, tn, None, [s([a, k, im.rotate(op, t, a, k)]) for a in vs for k in counts]))
        cases.append(case("eq_mask", tn, None, [s([a, b, im.eq_mask(t, a, b)]) for a in vs[:6] for b in vs[:6]]))
        cases.append(case("select", tn, None, [s([m, a, b, im.select(t, m, a, b)])
                                                for m in (0, -1 if t.signed else t.max, 0x0F)
                                                for a in vs[:4] for b in vs[-4:]]))
        cases.append(case("order_key", tn, None, [s([a, im.order_key(t, a)]) for a in vs]))
    for tn in UNSIGNED:
        t = im.TYPES[tn]
        vs = values(tn, rng, extra=4)
        cases.append(case("add_carry", tn, None, [s([a, b, c, im.add_carry(t, a, b, c)]) for a in vs for b in vs for c in (0, 1)],
                          result="[sum, carry]"))
        cases.append(case("sub_borrow", tn, None, [s([a, b, c, im.sub_borrow(t, a, b, c)]) for a in vs for b in vs for c in (0, 1)],
                          result="[diff, borrow]"))
        cases.append(case("mul_wide", tn, None, [s([a, b, im.mul_wide(t, a, b)]) for a in vs for b in vs],
                          result="[lo, hi]"))
    for tn in SIGNED:
        t = im.TYPES[tn]
        vs = values(tn, rng, extra=4)
        cases.append(case("mul_wide_s", tn, None, [s([a, b, im.mul_wide_s(t, a, b)]) for a in vs for b in vs],
                          result="[lo, hi]"))
    # usize / idiff (spec 04 §1.1): the same rules as the fixed-width type of the target's width.
    # A runner runs only the cases whose width equals its target's usize width.
    urng = random.Random(SEED + 9)      # own stream: the rows of the other cases stay as they were
    for w in (16, 32, 64):
        for alias, base in (("usize", f"u{w}"), ("idiff", f"i{w}")):
            t = im.TYPES[base]
            vs = values(base, urng, extra=3)
            divisors = [d for d in sorted(set(vs) | {0, 1, 3, 7} | ({-1} if t.signed else set())) if t.contains(d)]
            for op in ("add", "sub", "mul") + (("neg", "abs") if t.signed else ()):
                for d in im.ADMITS[op]:
                    rows = ([s([a, im.unary(op, t, a, d)]) for a in vs] if op in ("neg", "abs")
                            else [s([a, b, im.binary(op, t, a, b, d)]) for a in vs for b in vs])
                    cases.append(case(op, alias, d, rows, width=w))
            for op in ("div_trunc", "rem_trunc"):
                for d in im.ADMITS[op]:
                    cases.append(case(op, alias, d, [s([a, b, im.binary(op, t, a, b, d)]) for a in vs for b in divisors],
                                      width=w))
    # mixed-type comparison (BT-6): every pair of types, boundary values
    for ta in INT_TYPES:
        for tb in INT_TYPES:
            va, vb = values(ta, rng, extra=0)[:5] + values(ta, rng, extra=0)[-3:], values(tb, rng, extra=0)[:5] + values(tb, rng, extra=0)[-3:]
            for op in ("cmp_eq", "cmp_lt", "cmp_le"):
                cases.append(case(op, ta, None, [s([a, b, im.cmp(op, a, b)]) for a in va for b in vb], type_b=tb))
    # division by a public constant (02 §2.4, gate 13): every constant divisor (not 0, not -1 when signed),
    # every dividend, quotient toward zero; computed with one N-bit high multiply and fix-ups
    for tn in INT_TYPES:
        t = im.TYPES[tn]
        ds = [1, 2, 3, 7, 10, 641, t.max, t.max - 1, (t.max >> 1) + 1] + ([-2, -3, -7, -10, t.min, t.min + 1] if t.signed else [])
        for d in dict.fromkeys(x for x in ds if t.contains(x) and im.div_const_magic(t, x)):
            xs = sorted({t.min, t.min + 1, -1 if t.signed else 2, 0, 1, d - 1 if t.contains(d - 1) else 0, d,
                         t.max - 1, t.max} | {rng.randint(t.min, t.max) for _ in range(10)})
            magic = {k: (str(v) if not isinstance(v, bool) else v) for k, v in im.div_const_magic(t, d).items()}
            for op, f in (("div_const", im.div_const), ("rem_const", im.rem_const)):
                cases.append(case(op, tn, None, [s([x, f(t, x, d)]) for x in xs], divisor=str(d),
                                  reference_rewrite=magic))
    # ML-KEM arithmetic (BT-8): Montgomery reduction with R = 2^16, q = 3329, i16/i32 wrap
    q, qinv = 3329, 62209
    i16, i32 = im.TYPES["i16"], im.TYPES["i32"]
    rows = []
    for a in sorted({0, 1, -1, q, -q, q * 2 ** 15 - 1, -q * 2 ** 15} | {rng.randint(-q * 2 ** 15, q * 2 ** 15 - 1) for _ in range(20)}):
        t = i16.wrap(i32.wrap(a) * qinv)                       # i16 wrapping multiply
        r = (a - t * q) >> 16                                  # exact division by 2^16 (arithmetic shift)
        rows.append(s([a, r]))
    cases.append({"op": "mlkem_montgomery_reduce", "type": "i32", "disposition": None, "timing": "ct",
                  "names": {"lowent": None, "pinax": None}, "rows": rows,
                  "note": "composite case: t = wrap_i16(a * 62209); r = (a - t*3329) >> 16; r ≡ a·2^-16 (mod 3329), |r| < 3329",
                  "contract": "-3329*2^15 <= a < 3329*2^15"})
    return {"schema": SCHEMA, "suite": "integer_ops",
            "source": "docs/conformance/refmodel/intmodel.py (report C, R5.1, R5.4)",
            "row_layout": "operands..., expected; integers as decimal strings; 'trap' = execution stops; 'none' = chk failure value",
            "cases": cases}


# binary floating point (spec 02 §3, report H §4.0): bit patterns in hex, "nan" = any NaN (FC-01)
F_SPECIAL = {
    "f32": [0x00000000, 0x80000000, 0x00000001, 0x80000001, 0x007FFFFF, 0x00800000, 0x80800000, 0x00400000,
            0x3F800000, 0xBF800000, 0x3F800001, 0x3F000000, 0x3EFFFFFF, 0x3FC00000, 0x40000000, 0x3DCCCCCD,
            0x33800000, 0x4B800000, 0x7F7FFFFF, 0xFF7FFFFF, 0x7F800000, 0xFF800000, 0x7FC00000, 0x7F800001],
    "f64": [0x0000000000000000, 0x8000000000000000, 0x0000000000000001, 0x8000000000000001, 0x000FFFFFFFFFFFFF,
            0x0010000000000000, 0x8010000000000000, 0x0008000000000000, 0x3FF0000000000000, 0xBFF0000000000000,
            0x3FF0000000000001, 0x3FE0000000000000, 0x3FDFFFFFFFFFFFFF, 0x3FF8000000000000, 0x4000000000000000,
            0x3FB999999999999A, 0x3CA0000000000000, 0x4340000000000000, 0x7FEFFFFFFFFFFFFF, 0xFFEFFFFFFFFFFFFF,
            0x7FF0000000000000, 0xFFF0000000000000, 0x7FF8000000000000, 0x7FF0000000000001],
}


def hx(f, v):
    return v if isinstance(v, (str, bool)) else f"{v:0{f.bits // 4}x}"


def nan_or(f, b):
    return "nan" if fm.is_nan(f, b) else b


def f_random(f, rng):
    """random bit pattern biased toward small exponents, subnormals and short significands"""
    r = rng.random()
    b = rng.getrandbits(f.bits)
    if r < 0.25:
        b &= ~(((1 << f.ebits) - 1) << (f.p - 1))                       # subnormal
    elif r < 0.5:
        b = (b & ~f.fmask) | (rng.getrandbits(4) << rng.randrange(f.p - 4))   # few significand bits
    elif r < 0.75:
        e = f.bias + rng.randint(-4, 4)
        b = (b & (f.sign | f.fmask)) | (e << (f.p - 1))                 # near 1.0
    return b


def f_ties(f, op, rng, want):
    """pairs whose exact result lies halfway between two neighbours (round-to-even decides)"""
    out = []
    while len(out) < want:
        e = f.bias + rng.randint(-3, 3)
        if op == "add":       # b is exactly half an ulp of a
            a = (e << (f.p - 1)) | (rng.getrandbits(3) << rng.randrange(f.p - 3)) | rng.getrandbits(2)
            b = (rng.getrandbits(1) * f.sign) | ((e - f.p) << (f.p - 1))
        else:                 # (1 + 2^-k)(1 + 2^-j) with k + j = p: the last term is half an ulp
            k = rng.randint(1, f.p - 1)
            a = (e << (f.p - 1)) | (1 << (f.p - 1 - k))
            b = ((f.bias + rng.randint(-2, 2)) << (f.p - 1)) | (1 << (k - 1))
            a |= rng.getrandbits(1) * f.sign
        if fm.is_tie(f, op, a, b):
            out.append((a, b))
    return out


def float_ops():
    rng = random.Random(SEED + 7)
    cases = []
    names = {"lowent": None, "pinax": None}
    for tn, f in fm.FORMATS.items():
        vs = F_SPECIAL[tn] + [f_random(f, rng) for _ in range(8)]
        status = "required" if tn == "f32" else "optional"

        def fc(op, rows, **extra):
            c = {"op": op, "type": tn, "disposition": None, "timing": "vt", "names": names, "rows": rows,
                 "status": status}
            c.update(extra)
            cases.append(c)
        for op in ("add", "sub", "mul", "div"):
            pairs = [(a, b) for a in vs for b in vs] + [(f_random(f, rng), f_random(f, rng)) for _ in range(300)]
            if op in ("add", "mul"):
                pairs += f_ties(f, op, rng, 40)
            fc(op, [[hx(f, a), hx(f, b), hx(f, fm.binary(op, f, a, b))] for a, b in pairs])
        sq = vs + [f_random(f, rng) & ~f.sign for _ in range(200)]
        fc("sqrt", [[hx(f, a), hx(f, fm.sqrt(f, a))] for a in sq])
        for op in ("lt", "eq"):
            fc(op, [[hx(f, a), hx(f, b), fm.compare(op, f, a, b)] for a in vs for b in vs])
        fc("order_key", [[hx(f, a), hx(f, fm.order_key(f, a))] for a in vs], result="unsigned integer, same width, hex")
        nans = [f.inf | 1, f.inf | (f.qnan ^ f.inf) >> 1, f.sign | f.inf | 1, f.qnan | 5, f.sign | f.qnan]   # sNaNs, then qNaNs
        fc("copy", [[hx(f, a), hx(f, a)] for a in nans + vs],
           note="02 §3.2, 08 FC-01 (gate 13): copying, passing and returning inside the processor keep every bit, "
                "signaling NaNs included; only a C ABI boundary whose target has abi_float_return x87 may set the quiet bit")
        arrays = [[], [vs[8]], vs[:5], vs[8:20], [vs[8], vs[9]] * 9, [vs[14]] * 33, vs[:21], vs[-4:] + vs[:3]]
        arrays += [[f_random(f, rng) for _ in range(rng.randint(5, 40))] for _ in range(12)]
        arrays = [[x for x in a if not fm.is_nan(f, x)] if i < 6 else a for i, a in enumerate(arrays)]
        for w in (4, 8, 16):
            fc(f"sum_tree{w}", [[[hx(f, x) for x in a], hx(f, nan_or(f, fm.sum_tree(f, w, a)))] for a in arrays],
               note="NaN results are reported as nan")
    return {"schema": SCHEMA, "suite": "float_ops",
            "source": "docs/conformance/refmodel/floatmodel.py (spec 02 §3, report H §4.0)",
            "row_layout": "operands..., expected; values as hex bit patterns of the case type; 'nan' = any NaN (FC-01); "
                          "lt/eq expected as booleans; sum_tree rows: [array], expected; default mode only (IEEE, subnormals, no FTZ)",
            "cases": cases}


def result_sets():
    """spec 08 §4.4-4.6 (FC-03, FC-04, FC-05, FC-07): every result a conforming processor may show"""
    sc = []

    def add(sid, rule, kind, allowed, **what):
        sc.append({"id": sid, "rule": rule, "kind": kind, **what, "allowed": allowed})
    for raw in (0x00, 0x01, 0x02, 0x80, 0xFE, 0xFF):
        add(f"mat-bool-{raw:02x}", "08 §4.4, FC-07; 03 §1", "materialize", rsm.materialize("bool", raw),
            type="bool", bytes=f"{raw:02x}", observe="value")
        if raw > 1:
            add(f"mat-bool-{raw:02x}-twice", "08 §4.4 (fixed after materialization)", "materialize",
                rsm.materialize_twice("bool", raw), type="bool", bytes=f"{raw:02x}", observe="value used twice")
    for v in (0x41, 0xD7FF, 0xD800, 0xDFFF, 0xE000, 0x10FFFF, 0x110000, 0xFFFFFFFF):
        add(f"mat-char-{v:x}", "08 §4.4, FC-07; 03 §2.2", "materialize", rsm.materialize("char", v),
            type="char", u32=str(v), observe="value")
    for tag in (0, 1, 2, 255):
        add(f"mat-tag-{tag}", "08 §4.4, FC-07; 06 §3.1", "materialize", rsm.materialize("tag", tag, 2),
            type="Result(u32,u8)", tag=str(tag), observe="variant (0 = ok, 1 = err)")
    add("mat-tag-2-twice", "08 §4.4 (fixed after materialization)", "materialize",
        rsm.materialize_twice("tag", 2, 2), type="Result(u32,u8)", tag="2", observe="variant used twice")
    for ty, bits in (("u8", 8), ("u32", 32)):
        add(f"uninit-{ty}", "08 §4.5, FC-03", "uninit", {"any_bits": bits}, type=ty, observe="value")
        add(f"uninit-{ty}-same-read", "08 §4.5, FC-03 (fixed within one read)", "uninit", [True],
            type=ty, observe="x == x for one read x")
    add("uninit-bool", "08 §4.5 then §4.4", "uninit", ["trap", False, True], type="bool", observe="value")
    add("pad-eq", "06 §2.5", "padding", [True], type="struct s_u8_u32_u8 (ffi)",
        fields=["1", "2", "3"], padding_a="00", padding_b="ff", observe="a == b (fields equal, padding differs)")
    add("pad-copy-fields", "06 §2.5, FC-04 (field bytes are copied)", "padding", [["1", "2", "3"]],
        type="struct s_u8_u32_u8 (ffi)", fields=["1", "2", "3"], observe="fields of the copy")
    add("pad-copy-bytes", "06 §2.5, FC-04 then FC-03", "padding", {"any_bits": 8},
        type="struct s_u8_u32_u8 (ffi)", observe="a padding byte of the copy")
    for sid, width, writes, init in (("race-read-u16", 2, [0xABCD], 0), ("race-read-u32", 4, [0x11223344], 0),
                                     ("race-read-u32-two", 4, [0x11223344, 0x55667788], 0xFFFFFFFF)):
        add(sid, "08 §4.6, FC-05 (per byte any value, fixed within one read)", "race", rsm.racy(width),
            type=f"u{8 * width}", initial=str(init), writes=[str(w) for w in writes], observe="racing non-atomic read")
    add("race-read-same-read", "08 §4.6, FC-05 (fixed within one read)", "race", [True], type="u32", initial="0",
        writes=["287454020"], observe="x == x for one racing read x")
    add("race-final-u32", "08 §4.6, FC-05 (after racing writes)", "race", rsm.racy(4), type="u32", initial="0",
        writes=["16909060", "2695938256"], observe="content after both writes finished")
    add("race-checked-index", "08 §4.6 (check and access see the same read)", "race",
        rsm.checked_index(4, 4), type="u32", initial="0",
        writes=["1", "16777216"], length="4", observe="a[i] with i read racily, idx check kept")
    return {"schema": SCHEMA, "suite": "result_sets",
            "source": "docs/conformance/refmodel/resultmodel.py (spec 08 §4.4-4.6, 06 §2.5)",
            "row_layout": "scenarios[] = {id, rule, kind, what is in memory or races, observe, allowed}; allowed is the complete "
                          "list of permitted observations ({any_bits: n} = every n-bit value); 'trap' = execution stops; "
                          "a runner passes when every observation lies in allowed",
            "scenarios": sc}


def alloc_sequences():
    """spec 05 §1 and §3: arena and pool sequences; offsets from the region start (aligned to 16)"""
    rng = random.Random(SEED + 11)
    seqs = []

    def arena(name, cap, steps):
        seqs.append({"name": name, "allocator": {"kind": "arena", "capacity": cap, "max_align": am.REGION_ALIGN},
                     "steps": am.expect(steps, am.Arena(cap))})

    def pool(name, block, count, steps):
        seqs.append({"name": name, "allocator": {"kind": "pool", "block": block, "count": count, "max_align": am.REGION_ALIGN},
                     "steps": am.expect(steps, am.Pool(block, count))})
    A = lambda size, align, as_=None: {"op": "allocate", "size": size, "align": align, **({"as": as_} if as_ else {})}
    D = lambda b, size, align: {"op": "deallocate", "block": b, "size": size, "align": align}
    arena("arena-align", 64, [A(1, 1, "a"), A(4, 4, "b"), A(2, 2, "c"), A(8, 8, "d"), A(1, 16, "e")])
    arena("arena-zero-size", 32, [A(3, 1, "a"), A(0, 8, "z"), A(4, 4, "b"), D("z", 0, 8), A(0, 16)])
    arena("arena-last-block", 64, [A(8, 8, "a"), A(8, 8, "b"), D("a", 8, 8), A(8, 8, "c"), D("c", 8, 8), A(4, 4, "d")])
    arena("arena-exact-fit", 32, [A(16, 8, "a"), A(16, 16, "b"), A(1, 1), D("b", 16, 16), A(17, 1), A(16, 1)])
    arena("arena-bad-align", 32, [A(4, 3), A(4, 0), A(4, 32), A(4, 16, "a")])
    # 05 §3: reset only with no live block; it reclaims what deallocating a non-last block could not
    arena("arena-reset", 32, [A(12, 4, "a"), A(12, 4, "b"), D("a", 12, 4), D("b", 12, 4), A(24, 4),
                              {"op": "reset"}, A(24, 4, "c")])
    arena("arena-realloc-fail", 32, [A(8, 4, "a"), {"op": "fill", "block": "a", "bytes": "0102030405060708"},
                                     {"op": "reallocate", "block": "a", "old": 8, "new": 64, "align": 4},
                                     {"op": "check_contents", "block": "a", "bytes": "0102030405060708"}])
    for i in range(12):
        steps, live = [], []
        for j in range(rng.randint(6, 14)):
            if live and rng.random() < 0.3:
                b, size, align = live.pop(rng.randrange(len(live)))
                steps.append(D(b, size, align))
            else:
                size, align = rng.choice([0, 1, 2, 3, 4, 8, 12, 16, 24]), rng.choice([1, 2, 4, 8, 16])
                steps.append(A(size, align, f"b{j}"))
                live.append((f"b{j}", size, align))
        arena_obj = am.Arena(96)
        done = am.expect(steps, arena_obj)                    # drop deallocations of failed allocations
        ok = {st["as"] for st in done if st["op"] == "allocate" and "ok" in st["expect"] and "as" in st}
        steps = [st for st in steps if st["op"] != "deallocate" or st["block"] in ok]
        arena(f"arena-random-{i}", 96, steps)
    pool("pool-exhaust", 16, 3, [A(16, 8, "a"), A(8, 8, "b"), A(1, 1, "c"), A(1, 1), D("b", 8, 8), A(16, 16, "d"), A(1, 1)])
    pool("pool-bad-align", 16, 2, [A(16, 32), A(16, 3), A(16, 16, "a")])
    sizes = []
    for bits in (16, 32, 64):
        for count, elem in ((0, 8), (1 << (bits - 3), 8), ((1 << (bits - 3)) - 1, 8), (3, (1 << bits) // 3), ((1 << bits) - 1, 1), (1 << (bits // 2), 1 << (bits // 2)),
                            ((1 << (bits - 1)) - 1, 1), (1 << (bits - 1), 1), ((1 << (bits - 4)), 8)):
            r = am.checked_size(count, elem, bits)
            sizes.append({"usize_bits": bits, "count": str(count), "elem": str(elem), "expect": {r[0]: str(r[1]) if r[0] == "ok" else r[1]}})
    return {"schema": SCHEMA, "suite": "alloc_sequences",
            "source": "docs/conformance/refmodel/allocmodel.py (spec 05 §1, §3)",
            "row_layout": "sequences[] = {name, allocator, steps}; steps: allocate {size, align, as, expect {ok: offset | 'free_block'} "
                          "or {err: AllocError}}, deallocate {block, size, align}, reset, fill/check_contents {block, bytes}, "
                          "reallocate {block, old, new, align, expect}; offsets from the region start (aligned to 16). "
                          "For every successful allocate: non-null, aligned, inside the region, disjoint from live blocks. "
                          "array_sizes[]: count * elem in chk arithmetic before any call; above the largest idiff of that width is size_overflow",
            "sequences": seqs, "array_sizes": sizes}


def litmus():
    """spec 07 §3.2-3.3: allowed outcomes of classic litmus tests under RC11"""
    tests = []
    for name, (threads, probe) in lt.TESTS.items():
        outs = sorted(rc.outcomes(threads))
        allowed = [{"regs": {r: str(v) for r, v in regs}, "mem": {x: str(v) for x, v in mem}} for regs, mem in outs]
        hit = any(all({**dict(regs), **dict(mem)}.get(k) == v for k, v in probe.items()) for regs, mem in outs)
        tests.append({"name": name, "threads": threads, "probe": {k: str(v) for k, v in probe.items()},
                      "probe_verdict": "allowed" if hit else "forbidden", "allowed": allowed})
    return {"schema": SCHEMA, "suite": "litmus",
            "source": "docs/conformance/refmodel/rc11model.py (axiomatic RC11, spec 07 §3; tests in refmodel/litmus_tests.py)",
            "row_layout": "tests[] = {name, threads: [[instruction...]...], probe, probe_verdict, allowed: [{regs, mem}]}; "
                          "instructions: {st, v, mo} {ld, r, mo} {rmw: xchg|fadd, loc, v, r, mo} {fence}; every location starts at 0; "
                          "a run fails when its outcome (registers and final memory) is not in allowed",
            "tests": tests}


def programs():
    """accept/reject examples in the shared program notation (secret, provenance, ownership, borrowing,
    program space, capability targets); the judge must agree with every hand-written verdict"""
    out = []
    for ex in px.EXAMPLES:
        verdict, rule = pm.judge(ex["fn"], ex.get("target"))
        if verdict != ex["verdict"] or (verdict == "reject" and rule != ex["rule"]):
            raise SystemExit(f"progmodel disagrees with example {ex['name']}: {verdict} {rule}")
        out.append({k: ex[k] for k in ("name", "rule", "verdict", "twin", "target", "note", "fn") if k in ex})
    return {"schema": SCHEMA, "suite": "programs",
            "source": "docs/conformance/refmodel/program_examples.py, judged by refmodel/progmodel.py (gate 8 question 7)",
            "row_layout": "examples[] = {name, rule, verdict accept|reject, twin, target?, note?, fn}; fn = {params, body, ret?, "
                          "structs?} in the shared program notation (see progmodel.py); every reject example has an accept twin "
                          "that differs in one place; target = the only target the example is for",
            "examples": out}


SIMD_SHAPES = [("u8", 16), ("i8", 16), ("u16", 8), ("i16", 8), ("u32", 4), ("i32", 4), ("u64", 2), ("i64", 2)]


def simd_ops():
    """spec 03 §3.2-3.6 and the variable-SIMD annex §3; every case is run for each implementation"""
    rng = random.Random(SEED + 13)
    cases = []
    names = {"lowent": None, "pinax": None}

    def sc(op, ty, disp, rows, **extra):
        cases.append({"op": op, "type": ty, "disposition": disp, "timing": "ct", "names": names, "rows": rows,
                      "implementations": "each", **extra})
    for tn, n in SIMD_SHAPES:
        t = im.TYPES[tn]
        edge = [t.min, t.max, 0, 1, -1 if t.signed else t.max - 1, t.max // 2 + 1]
        vecs = [[rng.choice(edge) if rng.random() < 0.4 else rng.randint(t.min, t.max) for _ in range(n)] for _ in range(10)]
        vecs += [[edge[i % len(edge)] for i in range(n)], [edge[(i + 3) % len(edge)] for i in range(n)]]
        pairs = [(vecs[i], vecs[(i * 7 + 3) % len(vecs)]) for i in range(len(vecs))]
        ty = f"simd({tn},{n})"
        for op, disps in (("add", ("wrap", "sat")), ("sub", ("wrap", "sat")), ("mul", ("wrap",)),
                          ("and", (None,)), ("or", (None,)), ("xor", (None,)), ("eq", (None,)), ("lt", (None,))):
            for d in disps:
                sc(op, ty, d, [[s(a), s(b), s(sm.lanes_op(op, tn, d, a, b))] for a, b in pairs])
        counts = [[rng.choice([0, 1, t.bits - 1, t.bits, t.bits + 3, 2 * t.bits + 1, 255 % (4 * t.bits)]) for _ in range(n)]
                  for _ in range(6)]
        fixed = [0, n, t.bits - 1, t.bits, t.bits + 1, n + 1, (1 << 32) - 1, (1 << 64) - 1]   # N next to W; unsigned counts (gate 13)
        counts += [[fixed[(h + j) % len(fixed)] for j in range(n)] for h in range(0, len(fixed), n)]
        for op in ("shl", "shr"):
            sc(op, ty, "wrap", [[s(vecs[i % len(vecs)]), s(k), s(sm.lanes_op(op, tn, None, vecs[i % len(vecs)], k))]
                                for i, k in enumerate(counts)],
               note="lane shift counts are always k mod lane bits (03 §3.3)")
        for op in ("sum", "min", "max"):
            sc(f"reduce_{op}", ty, None, [[s(a), s(sm.reduce(op, tn, a))] for a in vecs])
        if t.bits * n <= 128:
            sc("to_int", ty, None, [[s(a), str(sm.to_int(tn, a))] for a in vecs],
               note="bit reinterpretation as an unsigned integer of the same size, lane 0 in the lowest bits (03 §3.2)")
    table = [rng.randrange(256) for _ in range(16)]
    idxs = [[rng.choice([0, 5, 15, 16, 0x80, 0xFF, rng.randrange(16)]) for _ in range(16)] for _ in range(6)]
    sc("lane_lookup16", "simd(u8,16)", None, [[s(table), s(i), s(sm.lane_lookup16(table, i))] for i in idxs])
    slices = [[rng.randrange(256) for _ in range(m)] for m in (0, 3, 8, 16, 20)]
    sc("load_masked", "simd(u8,8)", None, [[s(sl), str(k), s(sm.load_masked(sl, k, 8))] for sl in slices for k in (0, 2, 8, 30)],
       note="rows: [slice, n, lanes]")
    sc("store_masked", "simd(u8,8)", None, [[s(sl), s(list(range(1, 9))), str(k), s(sm.store_masked(sl, list(range(1, 9)), k))]
                                            for sl in slices for k in (0, 2, 8, 30)], note="rows: [destination slice, vector, n, slice after]")
    for d in ("trap", "chk"):
        sc("load_full", "simd(u8,8)", d, [[s(sl), s(sm.load_full(sl, 8, d))] for sl in slices])
    kern = []
    for name, f in sm.KERNELS.items():
        tn = sm.KERNEL_TYPES[name]
        t = im.TYPES[tn]
        for m in (1, 3, 4, 15, 16, 17, 40):
            data = [rng.choice([t.min, t.max, 0, rng.randint(t.min, t.max)]) for _ in range(m)]
            if name == "add_sat_u8" and m % 2:
                data = data + [rng.randint(t.min, t.max)]
            res = {lc: f(lc, data) for lc in (1, 4, 16)}
            if len({json.dumps(v) for v in res.values()}) != 1:
                raise SystemExit(f"svec kernel {name} depends on the lane count")
            kern.append({"kernel": name, "description": " ".join(f.__doc__.split()), "type": tn, "input": s(data), "expected": res[1], "lane_counts": [1, 4, 16],
                         "implementations": "each"})
    return {"schema": SCHEMA, "suite": "simd_ops",
            "source": "docs/conformance/refmodel/simdmodel.py (spec 03 §3, variable-SIMD annex §3)",
            "row_layout": "cases: rows [lanes a, lanes b or counts, expected lanes] as decimal strings (lane 0 first); reduce/to_int "
                          "rows [lanes, value]; 'trap'/'none' as in integer_ops. kernels: an algorithm over svec(T) run with "
                          "simd.vlen_bits giving 1, 4 and 16 lanes; every lane count must give expected. "
                          "implementations = each: run the rows once per implementation (harness-protocol.md §3)",
            "cases": cases, "kernels": kern}


def casting_rules():
    rng = random.Random(SEED + 1)
    types = INT_TYPES + ["i128", "u128"]
    pairs = []
    for a in types + ["f32", "f64"]:
        for b in types + ["f32", "f64"]:
            pairs.append({"from": a, "to": b, "lossless": cm.lossless(a, b),
                          "implicit": {"lowent": cm.lowent_implicit(a, b), "pinax": cm.pinax_implicit(a, b)}})
    common = []
    for a in types:
        for b in types:
            common.append({"a": a, "b": b,
                           "lub_fixed_width": cm.lub(a, b, INT_TYPES),
                           "common": {"lowent": cm.lowent_common(a, b), "pinax": cm.pinax_common(a, b)}})
    narrowing = []
    for s_ in INT_TYPES:
        for t_ in INT_TYPES:
            if cm.lossless(s_, t_):
                continue
            vs = values(s_, rng, extra=3)
            for d in ("trap", "chk", "sat", "wrap"):
                narrowing.append({"from": s_, "to": t_, "disposition": d,
                                  "rows": [s([v, cm.narrow(s_, t_, v, d)]) for v in vs]})
    f32_bits = [0x00000000, 0x80000000, 0x3F800000, 0xBF800000, 0x3FC00000, 0xBFC00000, 0x3F000000,
                0x4EFFFFFF, 0x4F000000, 0xCF000000, 0xCF000001, 0x4F800000, 0x437F0000, 0x43800000,
                0x7F800000, 0xFF800000, 0x7FC00000, 0x00000001, 0x7F7FFFFF] + [rng.getrandbits(32) for _ in range(8)]
    from_f32 = []
    for t_ in INT_TYPES:
        for d in ("trap", "chk", "sat"):
            from_f32.append({"to": t_, "disposition": d, "rounding": "toward zero",
                             "rows": [[f"{b:08x}", s(cm.f32_to_int(b, t_, d))] for b in f32_bits]})
    to_f32 = []
    for s_ in INT_TYPES:
        vs = values(s_, rng, extra=3) + ([(1 << 24) + 1] if im.TYPES[s_].contains((1 << 24) + 1) else [])
        to_f32.append({"from": s_, "rounding": "nearest, ties to even",
                       "rows": [[s(v), f"{cm.int_to_f32_bits(v):08x}"] for v in sorted(set(vs))]})
    return {"schema": SCHEMA, "suite": "casting_rules",
            "source": "docs/conformance/refmodel/castmodel.py (report A, R5.3)",
            "row_layout": "integers as decimal strings; f32 as 8-digit hex bit patterns",
            "relations": pairs, "common_type": common, "narrowing": narrowing,
            "f32_to_int": from_f32, "int_to_f32": to_f32}


# struct and built-in layouts (specs 04 and 06); FFI rows are checked against clang in check.py
STRUCTS = {
    "s_u8_u32_u8":   {"layout": "ffi", "fields": [["a", "u8"], ["b", "u32"], ["c", "u8"]]},
    "s_u8_u64_u16":  {"layout": "ffi", "fields": [["a", "u8"], ["b", "u64"], ["c", "u16"]]},
    "s_u16_f64":     {"layout": "ffi", "fields": [["a", "u16"], ["b", "f64"]]},
    "s_u8_ptr":      {"layout": "ffi", "fields": [["tag", "u8"], ["p", "ptr"]]},
    "s_array_u32":   {"layout": "ffi", "fields": [["a", "array(u8,3)"], ["b", "u32"]]},
    "s_nested":      {"layout": "ffi", "fields": [["a", "u8"], ["inner", "struct:s_u8_u32_u8"], ["z", "u16"]]},
    "s_sizes":       {"layout": "ffi", "fields": [["n", "usize"], ["a", "uaddr"], ["d", "idiff"], ["k", "u8"]]},
    "s_bool_char":   {"layout": "ffi", "fields": [["b", "bool"], ["c", "char"], ["d", "u8"]]},
    "s_f32_u8":      {"layout": "ffi", "fields": [["x", "f32"], ["y", "u8"]]},
    "s_u8_i128":     {"layout": "ffi", "fields": [["a", "u8"], ["b", "i128"]]},
    "s_align16":     {"layout": "ffi", "align": 16, "fields": [["a", "u32"]]},
    "s_packed":      {"layout": "packed", "fields": [["a", "u8"], ["b", "u32"], ["c", "u16"]]},
    "s_packed_al4":  {"layout": "packed", "align": 4, "fields": [["a", "u8"], ["b", "u32"]]},
    "s_packed_atomic": {"layout": "packed", "fields": [["a", "u8"], ["b", "Atomic(u16)"]]},
    "s_packed_nested": {"layout": "packed", "fields": [["a", "u8"], ["inner", "struct:s_atomic"]]},
    "s_simd":        {"layout": "ffi", "fields": [["a", "u8"], ["v", "simd(u32,4)"]]},
    "s_simd_wide":   {"layout": "ffi", "fields": [["a", "u8"], ["v", "simd(u8,64)"], ["z", "u16"]]},
    "s_atomic":      {"layout": "default", "fields": [["a", "u8"], ["b", "Atomic(u32)"], ["c", "Atomic(u64)"]]},
    "u_u8_u64_arr":  {"layout": "ffi", "union": True, "fields": [["a", "u8"], ["b", "u64"], ["c", "array(u8,12)"]]},
    "s_cacheline":   {"layout": "default", "align": "cache_line", "fields": [["count", "Atomic(u32)"], ["owner", "u16"]]},
    "s_reorder":     {"layout": "default", "reorder": True, "fields": [["a", "u8"], ["b", "u64"], ["c", "u16"], ["d", "u32"], ["e", "u8"]]},
    "s_reorder_tie": {"layout": "default", "reorder": True, "fields": [["a", "u16"], ["b", "u8"], ["c", "u16"], ["d", "u8"], ["p", "ptr"]]},
    "s_default_mix": {"layout": "default", "fields": [["tag", "u8"], ["v", "slice(u8)"], ["o", "Option(u32)"], ["r", "Option(ref(u32))"]]},
}
BUILTINS = ["ref(u32)", "mutref(u64)", "ptr(u32)", "usize", "idiff", "uaddr",
            "slice(u8)", "slice_mut(u8)", "slice(u64)", "array(u16,5)", "array(u8,0)",
            "Option(ref(u32))", "Option(mutref(u32))", "Option(owned_ptr(u32,static))", "Option(owned_ptr(u32,ctx))",
            "Option(ptr(u32))", "Option(u8)", "Option(u32)", "Option(u64)", "Option(struct:s_u16_f64)",
            "Option(Option(ref(u32)))", "Option(slice(u8))",
            "Result(u32,u8)", "Result(u64,u16)", "Result(slice(u8),u32)", "Result(u8,array(u8,3))",
            "Atomic(u8)", "Atomic(u16)", "Atomic(u32)", "Atomic(u64)", "Atomic(ptr(u8))",
            "owned_ptr(u32,static)", "owned_ptr(u32,ctx)",
            "simd(u32,4)", "simd(u8,64)", "simd(u16,2)", "Mutex(u32)", "Mutex(u64)", "Mutex(struct:s_u16_f64)", "RwLock(u64)", "RwLock(array(u8,3))"]
BITFIELDS = [
    {"name": "ctrl_lsb_le", "unit": 16, "order": "lsb", "endian": "little",
     "fields": [["en", 1], ["mode", 3], ["div", 4], ["rsvd", 8]], "values": [[1, 5, 9, 0], [0, 7, 15, 255], [1, 0, 0, 1]]},
    {"name": "ctrl_msb_be", "unit": 16, "order": "msb", "endian": "big",
     "fields": [["en", 1], ["mode", 3], ["div", 4], ["rsvd", 8]], "values": [[1, 5, 9, 0], [0, 7, 15, 255], [1, 0, 0, 1]]},
    {"name": "hdr_lsb_be32", "unit": 32, "order": "lsb", "endian": "big",
     "fields": [["ver", 4], ["ihl", 4], ["tos", 8], ["len", 16]], "values": [[4, 5, 0, 1500], [6, 15, 255, 65535]]},
    {"name": "delta_lsb_le_signed", "unit": 16, "order": "lsb", "endian": "little",
     "fields": [["dx", 5, "signed"], ["dy", 5, "signed"], ["flag", 1], ["rsvd", 5]],
     "values": [[-16, 15, 1, 0], [-1, -1, 0, 31], [3, -4, 1, 7], [0, 0, 0, 0]]},
    {"name": "flags_msb_le8", "unit": 8, "order": "msb", "endian": "little",
     "fields": [["a", 1], ["b", 1], ["c", 2]], "values": [[1, 0, 3], [0, 1, 1]]},
]


def struct_layout():
    targets = {n: {"ptr": list(t.ptr), "addr_bytes": t.addr_bytes, "usize_bytes": t.usize_bytes, "atomic_max": t.atomic_max,
                   "atomic_ldst": t.atomic_ldst, "atomic_rmw": t.atomic_rmw, "masking": t.masking, "cache_line": t.cache_line,
                   "oracle": " ".join(["clang"] + list(t.clang)) if t.clang else None, "note": t.note,
                   "scalars": {k: list(v) for k, v in sorted(t.scalars.items())}}
               for n, t in lm.TARGETS.items()}
    structs = []
    for name, decl in STRUCTS.items():
        res = {}
        for tn, tg in lm.TARGETS.items():
            try:
                res[tn] = lm.struct_layout(decl, tg, STRUCTS)
            except lm.Unavailable as e:
                res[tn] = {"unavailable": str(e)}
            except lm.Rejected as e:
                res[tn] = {"rejected": str(e)}
        structs.append({"name": name, **decl, "results": res})
    builtins = []
    for t in BUILTINS:
        res = {}
        for tn, tg in lm.TARGETS.items():
            try:
                res[tn] = lm.builtin_detail(t, tg, STRUCTS)
            except lm.Unavailable as e:
                res[tn] = {"unavailable": str(e)}
        builtins.append({"type": t, "results": res})
    bits = []
    for b in BITFIELDS:
        rows = []
        for vs in b["values"]:
            word, pos = lm.bitfield_pack(b["unit"], b["order"], b["fields"], vs)
            rows.append([s(vs), str(word), [f"{x:02x}" for x in lm.unit_bytes(word, b["unit"], b["endian"])]])
        bits.append({"name": b["name"], "unit_bits": b["unit"], "bit_order": b["order"], "endian": b["endian"],
                     "fields": b["fields"], "bit_positions": lm.bitfield_pack(b["unit"], b["order"], b["fields"],
                                                                             [0] * len(b["fields"]))[1],
                     "rows": rows})
    return {"schema": SCHEMA, "suite": "struct_layout",
            "source": "docs/conformance/refmodel/layoutmodel.py (specs 04, 06)",
            "row_layout": "sizes and offsets in bytes; padding as [start, length]; bitfield rows: [field values], unit value, unit bytes in memory order",
            "targets": targets, "structs": structs, "builtins": builtins, "bitfields": bits}


def dump(obj):
    return json.dumps(obj, ensure_ascii=False, indent=None, separators=(",", ":")) + "\n"


def range_erasure():
    """spec 08 §3: for each example, the checks the floor must remove and those it must keep,
    with an input (meeting the contracts) that fires each kept check."""
    import rangemodel as rm
    floor = rm.ANALYSES["IxT+L"]
    examples = []
    for name, fn in rm.EXAMPLES.items():
        removed = rm.run(fn, floor)
        wit = rm.witnesses(fn)
        if rm.has_contracts(fn):                   # 08 §2.4: the same example with its contracts erased
            bare = rm.erase_contracts(fn)
            e_removed, e_wit = rm.run(bare, floor), rm.witnesses(bare)
        checks = []
        for cid, kind in rm.check_kinds(fn).items():
            if not cid.startswith("c"):
                continue
            w = wit.get(cid)
            row = {"id": cid, "kind": kind, "floor": "remove" if removed.get(cid) else "keep",
                   "fires_on": {"args": w[0], "lens": w[1]} if w else None}
            if rm.has_contracts(fn):
                w = e_wit.get(cid)
                row["erased"] = {"floor": "remove" if e_removed.get(cid) else "keep",
                                 "fires_on": {"args": w[0], "lens": w[1]} if w else None}
            checks.append(row)
        examples.append({"name": name, "params": fn["params"], "requires": fn.get("requires", []),
                         "calls": fn.get("calls", []), "body": fn["body"], "checks": checks})
    return {"schema": "bts-vectors/1", "suite": "range_erasure",
            "source": "docs/conformance/refmodel/rangemodel.py (floor = IxT+L, spec 08 §3)",
            "row_layout": "examples[].checks[] = {id, kind, floor: remove|keep, fires_on, erased?: {floor, fires_on}}",
            "examples": examples}


def layout_for(path):
    """struct_layout rows for one processor-supplied target description (harness-protocol.md §2)"""
    tg = lm.load_target(None, path)
    rows = {"target": tg.name, "structs": {}, "builtins": {}}
    for name, decl in STRUCTS.items():
        try:
            rows["structs"][name] = lm.struct_layout(decl, tg, STRUCTS)
        except lm.Unavailable as e:
            rows["structs"][name] = {"unavailable": str(e)}
        except lm.Rejected as e:
            rows["structs"][name] = {"rejected": str(e)}
    for t in BUILTINS:
        try:
            rows["builtins"][t] = lm.builtin_detail(t, tg, STRUCTS)
        except lm.Unavailable as e:
            rows["builtins"][t] = {"unavailable": str(e)}
    return rows


def device_access():
    """04 §2.1 device_ptr and §4 volatile width: accept/reject verdicts derived from each target description"""
    rows = []
    for tn in lm.ORDER:
        with open(os.path.join(lm.TARGET_DIR, tn + ".json"), encoding="utf-8") as f:
            d = json.load(f)
        top = (1 << d["addr_bits"]) - 1
        probes = []
        for r in d["device_ranges"]:
            probes += [(r["space"], r["first"]), (r["space"], r["last"])]
            if r["first"] > 0:
                probes.append((r["space"], r["first"] - 1))
            if r["last"] < top:
                probes.append((r["space"], r["last"] + 1))
        if not d["device_ranges"]:
            probes.append(("data", 0x1000 % (top + 1)))
        for space, a in probes:
            inside = any(r["space"] == space and r["first"] <= a <= r["last"] for r in d["device_ranges"])
            for width in (1, 2, 4, 8):
                split = width == 2 and d["io16_store_order"] is not None   # the annex fixes the split order (AVR annex §3)
                ok_width = width <= d["volatile_max"] or split
                verdict = "accept" if inside and ok_width else "reject"
                reason = None if verdict == "accept" else ("outside device_ranges" if not inside else "wider than volatile_max")
                row = {"target": tn, "space": space, "addr": a, "width": width, "verdict": verdict, "reason": reason}
                if split and verdict == "accept":
                    row["store_order"] = d["io16_store_order"]
                rows.append(row)
    return {"schema": "bts-vectors/1", "suite": "device_access",
            "source": "docs/conformance/gen.py from docs/conformance/targets/*.json (specs 04 §2.1, §4; 08 §4.3 T1)",
            "row_layout": "rows[] = {target, space, addr, width (bytes), verdict accept|reject, reason}: "
                          "volatile access of that width through device_ptr(space, addr) with constant addr; "
                          "store_order (accepted split 16-bit accesses): high_first or low_first byte store order",
            "rows": rows}


Q_FORMATS = ["q_s(0,7)", "q_s(3,4)", "q_u(0,8)", "q_u(4,4)", "q_s(0,15)", "q_s(7,8)", "q_u(8,8)",
             "q_s(0,31)", "q_s(15,16)", "q_u(16,16)"]
DISPOSITIONS = ("sat", "wrap", "chk", "trap")


def fixed_ops():
    """02 §4: Q-format add, sub and mul (four roundings) under the four dispositions, on storage integers; conversions"""
    rng = random.Random(SEED + 4)
    cases = []
    for name in Q_FORMATS:
        t, n = fx.q_type(name)
        half = 1 << (n - 1)
        edge = sorted({t.min, t.min + 1, 0, 1, half, t.max - 1, t.max} | ({-1, -half} if t.signed else set()))
        pairs = [(a, b) for a in edge for b in edge]
        odd = [x for x in (1, 3, 5, -1, -3, -5) if t.contains(x)]
        ties = [(a, half) for a in odd] + [(half, a) for a in odd]          # products a * 2**(n-1): exact ties
        pairs += ties + [(rng.randint(t.min, t.max), rng.randint(t.min, t.max)) for _ in range(12)]
        pairs = list(dict.fromkeys(pairs))
        for op in ("add", "sub", "mul"):
            for d in DISPOSITIONS:
                for rd in (fx.ROUNDINGS if op == "mul" else (None,)):
                    rows = [[s(a), s(b), s(fx.q_op(op, name, a, b, d, rd))] for a, b in pairs]
                    c = {"op": op, "type": name, "disposition": d, "timing": "ct", "names": {"lowent": None, "pinax": None},
                         "rows": rows}
                    if rd:
                        c["rounding"] = rd
                    cases.append(c)
    cases += fixed_conversions()
    return {"schema": SCHEMA, "suite": "fixed_ops", "source": "docs/conformance/refmodel/fixmodel.py (spec 02 §4)",
            "row_layout": "cases: rows [a, b, expected] as storage integers in decimal; the value of storage k is k / 2**n. "
                          "mul: exact product of the storage integers, n bits dropped with `rounding` (nearest_even is the "
                          "default, toward_zero and toward_neg_inf are chosen by name), then the disposition (sat is the "
                          "default). Conversions (gate 13): from_int [integer of type_b, storage], to_int [storage, "
                          "integer of type `to`, value rounded toward zero], convert [storage, storage of the Q type `to` "
                          "under `rounding`], from_f32 [f32 bits in hex, storage; value times 2**n rounded toward zero, "
                          "NaN and infinities as f32 -> integer], to_f32 [storage, f32 bits in hex, nearest even]; "
                          "the disposition applies to the result. 'trap' / 'none' as in integer_ops",
            "cases": cases}


def fixed_conversions():
    """02 §4 (gate 13): Q <-> integer, Q -> Q, f32 <-> Q"""
    def q(op, name, rows, d, **kw):
        return dict({"op": op, "type": name, "disposition": d, "timing": "ct",
                     "names": {"lowent": None, "pinax": None}, "rows": rows}, **kw)
    out = []
    f32s = [0x00000000, 0x80000000, 0x3F800000, 0xBF800000, 0x3F000000, 0xBF000000, 0x3EAAAAAB, 0x3D800000,
            0x3B800000, 0x37800000, 0x42FE0000, 0x43000000, 0xC3000000, 0x447A0000, 0x7F800000, 0xFF800000,
            0x7FC00000, 0x00000001]
    for name in Q_FORMATS:
        t, n = fx.q_type(name)
        ks = sorted(x for x in {t.min, t.min + 1, -1, 2, 0, 1, (1 << n) - 1, 1 << n, (1 << n) + 1, t.max - 1, t.max}
                    if t.contains(x))
        ints = sorted({0, 1, -1, 2, -2, 3, t.max >> n, (t.max >> n) + 1, (t.min >> n), (t.min >> n) - 1, 1000, -1000})
        for d in DISPOSITIONS:
            out.append(q("from_int", name, [[s(v), s(fx.from_int(name, v, d))] for v in ints], d, type_b="i32"))
            for to in ("i32", "i8"):
                out.append(q("to_int", name, [[s(k), s(fx.to_int(name, k, to, d))] for k in ks], d, to=to))
            if d != "wrap":     # f32 -> Q has no wrap, as f32 -> integer (02 §1.5, §4; gate 16)
                out.append(q("from_f32", name, [[f"{b:08x}", s(fx.from_f32(name, b, d))] for b in f32s], d))
        out.append(q("to_f32", name, [[s(k), f"{fx.to_f32(name, k):08x}"] for k in ks], None))
    for src, dst in (("q_s(3,4)", "q_s(7,8)"), ("q_s(7,8)", "q_s(3,4)"), ("q_u(8,8)", "q_u(4,4)"), ("q_s(0,15)", "q_s(0,7)"),
                     ("q_u(4,4)", "q_s(3,4)")):
        t, _ = fx.q_type(src)
        ks = sorted(x for x in {t.min, t.min + 1, -1, 2, 0, 1, 7, 8, 9, 24, 25, 40, 41, -24, -40, t.max - 1, t.max}
                    if t.contains(x))
        for d in DISPOSITIONS:
            for rd in fx.ROUNDINGS:
                out.append(q("convert", src, [[s(k), s(fx.convert(src, k, dst, rd, d))] for k in ks], d, to=dst, rounding=rd))
    return out


def canonical_encoding():
    """02 §6: canonical little-endian bytes for integers, usize/idiff of each width, f32/f64 and Q formats"""
    rows = []
    for tn in INT_TYPES:
        t = im.TYPES[tn]
        for v in sorted({t.min, -1 if t.signed else 2, 0, 1, t.max, 0x1234 % (t.max + 1)}):
            rows.append({"type": tn, "value": s(v), "bytes": fx.encode(tn, v)})
    for w in (16, 32, 64):
        for tn, vs in (("usize", [0, 1, (1 << w) - 1]), ("idiff", [-(1 << (w - 1)), -1, 0, (1 << (w - 1)) - 1])):
            for v in vs:
                rows.append({"type": tn, "width": w, "value": s(v), "bytes": fx.encode(tn, v, w)})
    f32s = [0x00000000, 0x80000000, 0x3F800000, 0xBF800000, 0x7F800000, 0xFF800000, 0x00000001,
            0x7FC00000, 0x7FC00001, 0xFFC00000, 0x7F800001, 0xFFFFFFFF]
    f64s = [0, 0x8000000000000000, 0x3FF0000000000000, 0x7FF0000000000000, 0x7FF8000000000000,
            0x7FF8000000000001, 0xFFF8000000000000, 0x7FF0000000000001, 0xFFFFFFFFFFFFFFFF]
    rows += [{"type": "f32", "value": f"{v:08x}", "bytes": fx.encode("f32", v)} for v in f32s]
    rows += [{"type": "f64", "value": f"{v:016x}", "bytes": fx.encode("f64", v)} for v in f64s]
    for name in ("q_s(3,4)", "q_u(8,8)", "q_s(15,16)"):
        t, _ = fx.q_type(name)
        rows += [{"type": name, "value": s(v), "bytes": fx.encode(name, v)} for v in sorted({t.min, -1 if t.signed else 1, 0, t.max})]
    for tn, v in (("bool", True), ("bool", False), ("char", "U+0041"), ("char", "U+1F600"),
                  ("struct{u8,u32,u8}", ["1", "2", "3"]), ("struct{u16,i64,bool}", ["65535", "-2", True]),
                  ("Option(u32)", "5"), ("Option(u32)", None), ("Result(u8,u16)", {"ok": "7"}), ("Result(u8,u16)", {"err": "513"})):
        rows.append({"type": tn, "value": v, "bytes": fx.encode_composite(tn, v)})
    return {"schema": SCHEMA, "suite": "canonical_encoding", "source": "docs/conformance/refmodel/fixmodel.py (spec 02 §6)",
            "row_layout": "rows[] = {type, width? (usize/idiff only), value, bytes}: value is decimal for integers and Q "
                          "storage integers, a hex bit pattern for f32/f64; bytes is the canonical encoding in hex, "
                          "lowest address first. usize/idiff widen to max(8 bytes, width). Composites (gate 14): struct{T1,...} "
                          "values are lists, bool a JSON boolean, char U+XXXX, Option null or the payload, Result {ok} or {err}",
            "rows": rows}


def build_modes():
    """08 §1.2: each check with the result when it is kept and when a build mode removes it"""
    rng = random.Random(SEED + 5)
    cases = []

    def add(check, op, tn, rows, **extra):
        out = []
        for args in rows:
            kept, erased = fx.checked_and_erased(check, op, tn, args)
            out.append([s(a) if not isinstance(a, str) else a for a in args] + [s(kept), s(erased)])
        cases.append({"check": check, "op": op, "type": tn, "removable": check in fx.REMOVABLE, "rows": out, **extra})
    for tn in INT_TYPES:
        t = im.TYPES[tn]
        vs = [t.min, t.min + 1, 0, 1, t.max - 1, t.max] + ([-1] if t.signed else [])
        for op in ("add", "sub", "mul"):
            add("ovf", op, tn, [(a, b) for a in vs for b in vs])
        if t.signed:
            for op in ("neg", "abs"):
                add("ovf", op, tn, [(a,) for a in vs])
        for op in ("shl", "shr"):
            add("shc", op, tn, [(rng.choice(vs[1:]), k) for k in (0, 1, t.bits - 1, t.bits, t.bits + 1, 2 * t.bits + 3)])
        add("div", "div_trunc", tn, [(a, b) for a in (t.min, 7, 0) for b in ([0, 1] + ([-1] if t.signed else []))])
    for frm, to in (("i32", "i8"), ("i32", "u8"), ("u16", "u8"), ("i64", "i16"), ("u32", "i32")):
        f, t = im.TYPES[frm], im.TYPES[to]
        vals = sorted(v for v in {f.min, -1 if f.signed else 0, 0, t.max, t.max + 1, t.min, t.min - 1, f.max} if f.contains(v))
        add("nar", "narrow", to, [(v,) for v in vals], **{"from": frm})
    add("idx", "index", "usize", [(i, n) for i in (0, 3, 4, 5) for n in (0, 4)])
    add("tag", "match", "u8", [(g, 3) for g in (0, 2, 3, 255)])
    for to in ("i32", "u8"):
        add("fcv", "f32_to_int", to, [(b,) for b in (0x3F800000, 0xC0000000, 0x7FC00000, 0x7F800000, 0x4F000000, 0x43800000)])
    for c in cases:
        if c["check"] == "fcv":
            c["rows"] = [[f"{int(r[0]):08x}"] + r[1:] for r in c["rows"]]
    return {"schema": SCHEMA, "suite": "build_modes", "source": "docs/conformance/refmodel/fixmodel.py (spec 08 §1.2)",
            "row_layout": "checks: {check, op, type, from? (nar), removable, rows [operands..., kept, erased]}: kept is the "
                          "result with the check kept (the operator's default trap disposition; idx/tag give 'ok' when "
                          "in range); erased is the result in a build mode that removes the check without proof. Only "
                          "ovf, shc and nar are removable, and removing them gives the wrap value (shift counts mod the "
                          "width); for idx, div, tag and fcv erased equals kept. idx rows are [index, length], tag rows "
                          "[tag, variants], fcv rows [f32 bit pattern]",
            "checks": cases}


def bigint_ops():
    """02 §5.1: exact bigint operations, zero-divisor traps, shifts, comparisons, narrowing to fixed widths"""
    rng = random.Random(SEED + 6)
    edge = {0, 1, -1, 2, -2, 3, -3, 7, -7}
    for b in (31, 32, 33, 63, 64, 65, 127, 128, 129):
        edge |= {(1 << b) - 1, 1 << b, -(1 << b), -(1 << b) + 1}
    vals = sorted(edge) + [rng.randint(-(1 << 256), 1 << 256) for _ in range(10)]
    small = [0, 1, -1, 2, -7, 3, (1 << 32) + 5, -(1 << 64) - 3]
    divisors = [0, 1, -1, 2, -2, 3, -3, 7, -7, 1 << 32, -(1 << 32), (1 << 64) + 1, -(1 << 100) - 9,
                rng.randint(1, 1 << 128), -rng.randint(1, 1 << 128)]
    names = {"lowent": None, "pinax": None}

    def case(op, rows, **kw):
        return dict({"op": op, "type": "bigint", "disposition": None, "timing": "vt", "names": names, "rows": rows}, **kw)
    cases = []
    for op in ("add", "sub", "mul"):
        cases.append(case(op, [[s(a), s(b), s(bm.binary(op, a, b))] for a in vals for b in vals[::3] + small]))
    for op in bm.DIV:
        cases.append(case(op, [[s(a), s(b), s(bm.binary(op, a, b))] for a in vals[::2] + small for b in divisors]))
    for op in bm.UNARY:
        cases.append(case(op, [[s(a), s(bm.unary(op, a))] for a in vals]))
    for op in bm.SHIFTS:
        cases.append(case(op, [[s(a), s(k), s(bm.shift(op, a, k))] for a in vals[::2] + small
                               for k in (0, 1, 5, 31, 32, 33, 64, 100, 257)]))
    pairs = [(a, b) for a in vals[::2] for b in vals[::5]] + [(a, a) for a in vals[::4]]
    for op in bm.COMPARE:
        cases.append(case(op, [[s(a), s(b), s(bm.compare(op, a, b))] for a, b in pairs]))
    for tn in INT_TYPES:
        t = im.TYPES[tn]
        xs = sorted({0, 1, -1, t.min, t.max, t.min - 1, t.max + 1, (1 << 64) + 1, -(1 << 70)} | set(vals[-3:]))
        for d in DISPOSITIONS:
            cases.append(case("narrow", [[s(a), s(bm.narrow(a, tn, d))] for a in xs], to=tn, disposition=d))
    return {"schema": SCHEMA, "suite": "bigint_ops", "source": "docs/conformance/refmodel/bigmodel.py (spec 02 §5.1)",
            "row_layout": "cases: values in decimal. Binary ops rows [a, b, expected], unary [a, expected], shl/shr "
                          "[a, k, expected], comparisons [a, b, bool] (cmp gives -1, 0, 1), narrow [a, expected] to the "
                          "fixed-width type `to` under `disposition`. 'trap' (zero divisor, or narrowing with trap) and "
                          "'none' (chk) as in integer_ops",
            "cases": cases}


def decimal_ops():
    """02 §5.2: decimal arithmetic rounded to a context, quantize, comparisons, exact conversion from integers"""
    rng = random.Random(SEED + 7)
    vals = ["0E0", "1E0", "-1E0", "2E0", "3E0", "-7E0", "5E-1", "-5E-1", "15E-1", "-25E-1", "9995E-1", "-9995E-1",
            "125E-2", "10E0", "1E1", "1E34", "-1E-30", "123456789012345678901234567890123456789E-20"]
    vals += [f"{rng.choice(['', '-'])}{rng.randint(1, 10 ** rng.randint(1, 40))}E{rng.randint(-25, 25)}" for _ in range(6)]
    pairs = [(a, b) for a in vals[::3] for b in vals[1::4]] + [("1E0", "3E0"), ("2E0", "3E0"), ("-2E0", "3E0"),
                                                             ("1E0", "5E-1"), ("-1E0", "-5E-1"), ("9995E-1", "0E0")]
    names = {"lowent": None, "pinax": None}

    def case(op, rows, **kw):
        return dict({"op": op, "type": "decimal", "disposition": None, "timing": "vt", "names": names, "rows": rows}, **kw)
    cases = []
    for op in dm.ARITH:
        ps = pairs + [(a, "0E0") for a in vals[:3]] if op == "div" else pairs
        for p in (1, 3, 7, 16, 34):
            for m in dm.MODES:
                cases.append(case(op, [[a, b, dm.arith(op, a, b, p, m)] for a, b in ps], precision=p, rounding=m))
    for m in dm.MODES:
        cases.append(case("quantize", [[a, e, dm.quantize(a, e, m)] for a in vals for e in (-3, 0, 2)], rounding=m))
    for op in dm.UNARY:
        cases.append(case(op, [[a, dm.unary(op, a)] for a in vals]))
    cmp_pairs = pairs + [("10E0", "1E1"), ("5E-1", "50E-2"), ("0E0", "0E5")]
    for op in dm.COMPARE:
        cases.append(case(op, [[a, b, dm.compare(op, a, b)] for a, b in cmp_pairs]))
    for tn in ("i64", "u64", "bigint"):
        t = im.TYPES.get(tn)
        xs = [0, 1, 7, 100] + ([t.min, -1, t.max] if t and t.signed else [t.max] if t else [-(1 << 200) - 1, 1 << 128])
        cases.append(case("from_integer", [[s(v), dm.from_integer(v)] for v in xs], type_b=tn))
    return {"schema": SCHEMA, "suite": "decimal_ops", "source": "docs/conformance/refmodel/decmodel.py (spec 02 §5.2)",
            "row_layout": "cases: a decimal is written [-]cEe for (-1)^s * c * 10^e; results are compared by value "
                          "(02 §5.2: the result exponent is library-level). add/sub/mul/div rows [a, b, expected] "
                          "under the context `precision` and `rounding`; div by zero gives 'trap'. quantize rows "
                          "[a, e, expected] (a multiple of 10^e under `rounding`), neg/abs [a, expected], comparisons "
                          "[a, b, bool] (cmp gives -1, 0, 1), from_integer [integer in decimal, expected] from the "
                          "type `type_b`",
            "cases": cases}


# The row units of each suite: the source is the table of harness-protocol.md §5.2; this copy is compared
# with it by scripts/check_docs.py. A path walks lists ([]) and objects ({}); the rows are counted in the
# order the units are listed, each in file order. An element of `x[].rows[]` without `rows` is one row.
ROW_UNITS = {
    "programs": ["examples[]"],
    "range_erasure": ["examples[].checks[]"],
    "device_access": ["rows[]"],
    "canonical_encoding": ["rows[]"],
    "build_modes": ["checks[].rows[]"],
    "litmus": ["tests[].allowed[]"],
    "result_sets": ["scenarios[]"],
    "alloc_sequences": ["sequences[].steps[]", "array_sizes[]"],
    "casting_rules": ["relations[]", "common_type[]", "narrowing[].rows[]", "f32_to_int[].rows[]", "int_to_f32[].rows[]"],
    "struct_layout": ["structs[].results{}", "builtins[].results{}", "bitfields[].rows[]"],
    "simd_ops": ["cases[].rows[]", "kernels[]"],
}
DEFAULT_UNITS = ["cases[].rows[]"]


def walk(o, path):
    """the rows a unit path names, in file order"""
    if not path:
        return [o]
    head, rest = path[0], path[1:]
    if head.endswith("[]") or head.endswith("{}"):
        key = head[:-2]
        if key not in o and rest == [] and head == "rows[]":
            return [o]                                   # an element without rows is one row
        items = o.get(key, []) if isinstance(o, dict) else []
        items = list(items.values()) if head.endswith("{}") else items
        return [r for it in items for r in walk(it, rest)]
    return walk(o[head], rest)


def rows_of(o):
    return [r for u in ROW_UNITS.get(o["suite"], DEFAULT_UNITS) for r in walk(o, u.split("."))]


def count_rows(o):
    return len(rows_of(o))


def main():
    if "--target" in sys.argv:
        print(json.dumps(layout_for(sys.argv[sys.argv.index("--target") + 1]), indent=1, sort_keys=True))
        return 0
    out = os.path.join(HERE, "vectors")
    suites = {"integer_ops.json": integer_ops(), "casting_rules.json": casting_rules(),
              "struct_layout.json": struct_layout(), "range_erasure.json": range_erasure(),
              "float_ops.json": float_ops(), "result_sets.json": result_sets(),
              "alloc_sequences.json": alloc_sequences(), "litmus.json": litmus(),
              "programs.json": programs(), "simd_ops.json": simd_ops(),
              "device_access.json": device_access(), "fixed_ops.json": fixed_ops(),
              "canonical_encoding.json": canonical_encoding(), "build_modes.json": build_modes(),
              "bigint_ops.json": bigint_ops(), "decimal_ops.json": decimal_ops()}
    if "--check" in sys.argv:
        bad = [n for n, o in suites.items() if open(os.path.join(out, n), encoding="utf-8").read() != dump(o)]
        print("gen --check:", "stale " + ", ".join(bad) if bad else "vectors match the model")
        return 1 if bad else 0
    os.makedirs(out, exist_ok=True)
    for n, o in suites.items():
        with open(os.path.join(out, n), "w", encoding="utf-8") as f:
            f.write(dump(o))
        rows = count_rows(o)
        print(f"wrote vectors/{n}: {rows} rows")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
