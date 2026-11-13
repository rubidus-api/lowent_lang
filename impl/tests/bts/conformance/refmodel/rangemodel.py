"""Reference model of the floor analysis (spec 08 §3; report E, E5-E7).

Which run-time checks must every conforming processor remove? The floor is the reduced
product of intervals (on the mathematical value) and tnums (known bits), plus one relation
"x < len(s)". Five analyses are kept so the report's comparison can be reproduced:

  const   constants only
  I       intervals
  T       tnums (Vishwanathan et al., REF-MATH-03)
  IxT     reduced product of I and T
  IxT+L   IxT plus "x < len(s)"  -- the floor

Checks: ovf (add/sub/mul exact result fits), nar (narrowing), shc (shift count < width),
div (divisor != 0), idx (0 <= i < len(s)). Facts come from five sources (spec 08 §3.2):
requires at entry, a callee's ensures, guards, the counter of a 0..n loop, and checks
already passed. A contract fact is either a comparison or a bit-mask fact x & m == c.

    python3 -I rangemodel.py          table of the five analyses and a soundness run
"""
import json
import sys

TYPES = {"u8": (8, False), "u16": (16, False), "u32": (32, False),
         "i8": (8, True), "i16": (16, True), "i32": (32, True), "usize": (32, False)}


def trange(t):
    w, s = TYPES[t]
    return (-(1 << (w - 1)), (1 << (w - 1)) - 1) if s else (0, (1 << w) - 1)


# ---------------------------------------------------------------- tnum (value, mask) over w bits
def tn_top(w):
    return (0, (1 << w) - 1)


def tn_const(c, w):
    return (c & ((1 << w) - 1), 0)


def tn_and(a, b):
    av, am = a; bv, bm = b
    alpha, beta = av | am, bv | bm
    v = av & bv
    return (v, alpha & beta & ~v)


def tn_or(a, b):
    av, am = a; bv, bm = b
    v = av | bv
    mu = am | bm
    return (v, mu & ~v)


def tn_xor(a, b):
    av, am = a; bv, bm = b
    v = av ^ bv
    mu = am | bm
    return (v & ~mu, mu)


def tn_add(a, b, w):
    av, am = a; bv, bm = b
    M = (1 << w) - 1
    sm = (am + bm) & M
    sv = (av + bv) & M
    sigma = (sm + sv) & M
    chi = sigma ^ sv
    mu = chi | am | bm
    return (sv & ~mu & M, mu & M)


def tn_shl(a, k, w):
    M = (1 << w) - 1
    return ((a[0] << k) & M, (a[1] << k) & M)


def tn_shr(a, k):
    return (a[0] >> k, a[1] >> k)


def tn_range(t):
    """unsigned range of the pattern"""
    return (t[0], t[0] | t[1])


def tn_from_range(lo, hi, w):
    """known high bits shared by every value in [lo, hi] (0 <= lo <= hi)"""
    if lo < 0 or hi >= (1 << w):
        return tn_top(w)
    diff = lo ^ hi
    n = diff.bit_length()
    mask = (1 << n) - 1
    return (lo & ~mask, mask)


# ---------------------------------------------------------------- abstract value
class V:
    """interval (lo, hi) on the mathematical value, tnum on the pattern, optional bound < len(s)"""

    def __init__(self, t, lo, hi, tn, lt_len=None):
        self.t, self.lo, self.hi, self.tn, self.lt_len = t, lo, hi, tn, lt_len

    def __repr__(self):
        return f"{self.t}[{self.lo},{self.hi}]"


class Analysis:
    def __init__(self, use_i, use_t, use_l, const_only=False):
        self.use_i, self.use_t, self.use_l, self.const_only = use_i, use_t, use_l, const_only

    def top(self, t):
        lo, hi = trange(t)
        return V(t, lo, hi, tn_top(TYPES[t][0]))

    def const(self, t, c):
        return V(t, c, c, tn_const(c, TYPES[t][0]))

    def norm(self, v):
        """forget what this analysis does not track, then reduce the product"""
        t = v.t
        w, s = TYPES[t]
        lo, hi = trange(t)
        is_const = v.lo == v.hi
        if self.const_only and not is_const:
            return self.top(t)
        if not self.use_i and not is_const:
            v.lo, v.hi = lo, hi
        if not self.use_t and not is_const:
            v.tn = tn_top(w)
        if not self.use_l:
            v.lt_len = None
        v.lo, v.hi = max(v.lo, lo), min(v.hi, hi)
        if self.use_t and self.use_i and v.lo >= 0:                     # reduction both ways
            tlo, thi = tn_range(v.tn)
            if not s or thi <= hi:
                v.lo, v.hi = max(v.lo, tlo), min(v.hi, thi)
            fv, fm = tn_from_range(v.lo, v.hi, w)
            v.tn = (v.tn[0] | fv, v.tn[1] & fm)
        elif self.use_t and not self.use_i and not s:                   # tnum alone still bounds the value
            v.lo, v.hi = tn_range(v.tn)
        return v

    def fits(self, lo, hi, t):
        a, b = trange(t)
        return a <= lo and hi <= b


def bitlen_cap(x):
    return (1 << max(x, 0).bit_length()) - 1


def run(fn, an):
    """fn: dict(params, requires, body). returns {check_id: removed?}"""
    env, lens, out = {}, {}, {}
    for name, t in fn["params"]:
        if t == "slice":
            lens[name] = an.norm(V("usize", 0, trange("usize")[1], tn_top(32)))
        else:
            env[name] = an.norm(an.top(t))
    for name, t, conds in fn.get("calls", []):        # a callee's result: its ensures are facts
        env[name] = an.norm(an.top(t))
        for op, _, rhs in conds:
            narrow((op, name, rhs), env, lens, an)
    for req in fn.get("requires", []):
        narrow(req, env, lens, an)

    def ev(e):
        if isinstance(e, tuple) and e[0] == "lit":
            return an.const(e[2], e[1])
        if isinstance(e, str):
            return env[e]
        op = e[0]
        if op == "len":
            return lens[e[1]]
        if op in ("add", "sub", "mul"):
            cid, a, b = e[1], ev(e[2]), ev(e[3])
            t = a.t
            if op == "add":
                lo, hi = a.lo + b.lo, a.hi + b.hi
            elif op == "sub":
                lo, hi = a.lo - b.hi, a.hi - b.lo
            else:
                ps = [a.lo * b.lo, a.lo * b.hi, a.hi * b.lo, a.hi * b.hi]
                lo, hi = min(ps), max(ps)
            w = TYPES[t][0]
            tn = tn_add(a.tn, b.tn, w) if op == "add" else tn_top(w)
            if op == "mul" and a.lo == a.hi and a.lo >= 0 and a.lo & (a.lo - 1) == 0:
                tn = tn_shl(b.tn, a.lo.bit_length() - 1, w)
            if op == "mul" and b.lo == b.hi and b.lo >= 0 and b.lo & (b.lo - 1) == 0:
                tn = tn_shl(a.tn, b.lo.bit_length() - 1, w)
            if an.use_t and not an.use_i and not TYPES[t][1]:           # tnum-only: decide from the patterns
                tlo_a, thi_a = tn_range(a.tn)
                tlo_b, thi_b = tn_range(b.tn)
                if op == "add":
                    lo, hi = tlo_a + tlo_b, thi_a + thi_b
                elif op == "sub":
                    lo, hi = tlo_a - thi_b, thi_a - tlo_b
                else:
                    lo, hi = tlo_a * tlo_b, thi_a * thi_b
            out[cid] = an.fits(lo, hi, t)
            r = V(t, lo, hi, tn)
            if an.use_t and an.use_i and lo >= 0 and hi < (1 << w):     # carries stay inside the width
                pass
            elif not (lo >= 0 and hi < (1 << w)):
                r.tn = tn_top(w)
            return an.norm(r)
        if op in ("and", "or", "xor"):
            a, b = ev(e[1]), ev(e[2])
            t = a.t
            w = TYPES[t][0]
            tn = {"and": tn_and, "or": tn_or, "xor": tn_xor}[op](a.tn, b.tn)
            if a.lo >= 0 and b.lo >= 0:
                if op == "and":
                    lo, hi = 0, min(a.hi, b.hi)
                elif op == "or":
                    lo, hi = max(a.lo, b.lo), bitlen_cap(max(a.hi, b.hi))
                else:
                    lo, hi = 0, bitlen_cap(max(a.hi, b.hi))
            else:
                lo, hi = trange(t)
            return an.norm(V(t, lo, hi, tn))
        if op in ("shl", "shr"):
            cid, a, k = e[1], ev(e[2]), ev(e[3])
            t = a.t
            w = TYPES[t][0]
            out[cid] = k.lo >= 0 and k.hi < w
            if k.lo != k.hi:
                return an.norm(an.top(t))
            kk = k.lo
            if op == "shr":
                return an.norm(V(t, a.lo >> kk, a.hi >> kk, tn_shr(a.tn, kk)))
            tn = tn_shl(a.tn, kk, w)
            lo, hi = a.lo << kk, a.hi << kk
            if not an.fits(lo, hi, t):                                  # shl drops bits: fall back to the pattern
                lo, hi = trange(t)
            return an.norm(V(t, lo, hi, tn))
        if op == "narrow":
            cid, t, a = e[1], e[2], ev(e[3])
            out[cid] = an.fits(a.lo, a.hi, t)
            lo, hi = max(a.lo, trange(t)[0]), min(a.hi, trange(t)[1])
            w = TYPES[t][0]
            return an.norm(V(t, lo, hi, (a.tn[0] & ((1 << w) - 1), a.tn[1] & ((1 << w) - 1))))
        if op == "div":
            cid, a, b = e[1], ev(e[2]), ev(e[3])
            t = a.t
            out[cid] = b.lo > 0 or b.hi < 0
            if b.lo <= 0 <= b.hi:                                       # after the check: b != 0
                pass
            if b.lo > 0 and a.lo >= 0:
                return an.norm(V(t, a.lo // b.hi, a.hi // b.lo, tn_top(TYPES[t][0])))
            return an.norm(an.top(t))
        if op in ("neg", "abs"):
            cid, a = e[1], ev(e[2])
            t = a.t
            if op == "neg":
                lo, hi = -a.hi, -a.lo
            elif a.lo >= 0:
                lo, hi = a.lo, a.hi
            elif a.hi <= 0:
                lo, hi = -a.hi, -a.lo
            else:
                lo, hi = 0, max(-a.lo, a.hi)
            out[cid] = an.fits(lo, hi, t)
            return an.norm(V(t, lo, hi, tn_top(TYPES[t][0])))
        if op == "not":
            a = ev(e[1])
            t = a.t
            w, sg = TYPES[t]
            M = (1 << w) - 1
            if sg:
                return an.norm(V(t, -a.hi - 1, -a.lo - 1, tn_top(w)))
            return an.norm(V(t, M - a.hi, M - a.lo, (~a.tn[0] & ~a.tn[1] & M, a.tn[1])))
        if op == "rem":
            cid, a, b = e[1], ev(e[2]), ev(e[3])
            t = a.t
            w = TYPES[t][0]
            out[cid] = b.lo > 0 or b.hi < 0
            if b.lo > 0 and a.lo >= 0:
                tn = tn_top(w)
                if b.lo == b.hi and b.lo & (b.lo - 1) == 0:
                    k = b.lo - 1
                    tn = (a.tn[0] & k, a.tn[1] & k)
                return an.norm(V(t, 0, min(a.hi, b.hi - 1), tn))
            return an.norm(an.top(t))
        if op == "widen":
            t, a = e[1], ev(e[2])
            w = TYPES[t][0]
            tn = a.tn if a.lo >= 0 else tn_top(w)
            return an.norm(V(t, a.lo, a.hi, tn))
        if op == "idx":
            cid, s, i = e[1], e[2], ev(e[3])
            n = lens[s]
            out[cid] = i.lo >= 0 and (i.hi < n.lo or (i.lt_len == s))
            return an.norm(an.top("u8"))
        raise ValueError(op)

    def block(stmts):
        for st in stmts:
            kind = st[0]
            if kind == "let":
                v = ev(st[2])
                env[st[1]] = V(v.t, v.lo, v.hi, v.tn, v.lt_len)
            elif kind == "guard":
                narrow(st[1], env, lens, an)
            elif kind == "eval":
                ev(st[1])
            elif kind == "for":                                         # for i in 0 .. bound: body
                _, i, bound, body = st
                b = ev(bound)
                v = V("usize", 0, max(b.hi - 1, 0), tn_from_range(0, max(b.hi - 1, 0), 32))
                if isinstance(bound, tuple) and bound[0] == "len":
                    v.lt_len = bound[1]
                env[i] = an.norm(v)
                block(body)
    block(fn["body"])
    return out


def narrow(cond, env, lens, an):
    """facts from a guard or contract: (op, var, rhs)"""
    op, x, rhs = cond
    v = env.get(x)
    if isinstance(rhs, tuple) and rhs[0] == "len":
        n = lens[rhs[1]]
        if op == "lt":
            v.hi = min(v.hi, n.hi - 1)
            v.lt_len = rhs[1]
        env[x] = an.norm(v)
        return
    if x in lens and op == "ge":                                        # requires len(s) >= c
        lens[x].lo = max(lens[x].lo, rhs)
        lens[x] = an.norm(lens[x])
        return
    if op == "lt":
        v.hi = min(v.hi, rhs - 1)
    elif op == "le":
        v.hi = min(v.hi, rhs)
    elif op == "ge":
        v.lo = max(v.lo, rhs)
    elif op == "gt":
        v.lo = max(v.lo, rhs + 1)
    elif op == "ne0":
        if v.lo == 0:
            v.lo = 1
    elif op == "mask":                                                  # x & m == c
        m, c = rhs
        v.tn = ((v.tn[0] & ~m) | c, v.tn[1] & ~m)
        if an.use_t and not an.use_i and not TYPES[v.t][1]:
            v.lo, v.hi = tn_range(v.tn)
        elif an.use_t and v.lo >= 0:
            tlo, thi = tn_range(v.tn)
            v.lo, v.hi = max(v.lo, tlo), min(v.hi, thi)
    env[x] = an.norm(v)


def L(c, t):
    return ("lit", c, t)


# ---------------------------------------------------------------- examples
# Each check id is unique across the set; "expect" is what the core floor (IxT+L) must remove.
EXAMPLES = {
    "E01 contract a<=200, a+1":
        dict(params=[("a", "u8")], requires=[("le", "a", 200)],
             body=[("eval", ("add", "c01", "a", L(1, "u8")))]),
    "E02 no contract, a+1":
        dict(params=[("a", "u8")],
             body=[("eval", ("add", "c02", "a", L(1, "u8")))]),
    "E03 guard a<100, a+100":
        dict(params=[("a", "u8")],
             body=[("guard", ("lt", "a", 100)), ("eval", ("add", "c03", "a", L(100, "u8")))]),
    "E04 (a&0x0F)+240":
        dict(params=[("a", "u8")],
             body=[("let", "x", ("and", "a", L(0x0F, "u8"))), ("eval", ("add", "c04", "x", L(240, "u8")))]),
    "E05 ((a&0xF0)|0x08)+7":
        dict(params=[("a", "u8")],
             body=[("let", "x", ("or", ("and", "a", L(0xF0, "u8")), L(0x08, "u8"))),
                   ("eval", ("add", "c05", "x", L(7, "u8")))]),
    "E06 (a>>4)*15":
        dict(params=[("a", "u8")],
             body=[("let", "x", ("shr", "s06", "a", L(4, "u8"))), ("eval", ("mul", "c06", "x", L(15, "u8")))]),
    "E07 narrow u8 (b>>8), b:u16":
        dict(params=[("b", "u16")],
             body=[("let", "x", ("shr", "s07", "b", L(8, "u16"))), ("eval", ("narrow", "c07", "u8", "x"))]),
    "E08 narrow u8 (b&0x1FF)":
        dict(params=[("b", "u16")],
             body=[("eval", ("narrow", "c08", "u8", ("and", "b", L(0x1FF, "u16"))))]),
    "E09 a<<k with k=b&7":
        dict(params=[("a", "u8"), ("b", "u8")],
             body=[("let", "k", ("and", "b", L(7, "u8"))), ("eval", ("shl", "c09", "a", "k"))]),
    "E10 a<<b, no fact":
        dict(params=[("a", "u8"), ("b", "u8")],
             body=[("eval", ("shl", "c10", "a", "b"))]),
    "E11 x/(b|1)":
        dict(params=[("x", "u16"), ("b", "u16")],
             body=[("eval", ("div", "c11", "x", ("or", "b", L(1, "u16"))))]),
    "E12 x/b, guard b!=0":
        dict(params=[("x", "u16"), ("b", "u16")],
             body=[("guard", ("ne0", "b", 0)), ("eval", ("div", "c12", "x", "b"))]),
    "E13 x/b, no fact":
        dict(params=[("x", "u16"), ("b", "u16")],
             body=[("eval", ("div", "c13", "x", "b"))]),
    "E14 for i in 0..len(s): s[i]":
        dict(params=[("s", "slice")],
             body=[("for", "i", ("len", "s"), [("eval", ("idx", "c14", "s", "i"))])]),
    "E15 contract len(s)>=4, s[3]":
        dict(params=[("s", "slice")], requires=[("ge", "s", 4)],
             body=[("eval", ("idx", "c15", "s", L(3, "usize")))]),
    "E16 guard i<len(s), s[i]":
        dict(params=[("s", "slice"), ("i", "usize")],
             body=[("guard", ("lt", "i", ("len", "s"))), ("eval", ("idx", "c16", "s", "i"))]),
    "E17 s[i&3], contract len(s)>=4":
        dict(params=[("s", "slice"), ("i", "usize")], requires=[("ge", "s", 4)],
             body=[("eval", ("idx", "c17", "s", ("and", "i", L(3, "usize"))))]),
    "E18 i32 a+b, both in [-1000,1000]":
        dict(params=[("a", "i32"), ("b", "i32")],
             requires=[("ge", "a", -1000), ("le", "a", 1000), ("ge", "b", -1000), ("le", "b", 1000)],
             body=[("eval", ("add", "c18", "a", "b"))]),
    "E19 (a&0xFC)+3, aligned":
        dict(params=[("a", "u8")],
             body=[("eval", ("add", "c19", ("and", "a", L(0xFC, "u8")), L(3, "u8")))]),
    "E20 ((a|0x0F)^0x0F)+15":
        dict(params=[("a", "u8")],
             body=[("eval", ("add", "c20", ("xor", ("or", "a", L(0x0F, "u8")), L(0x0F, "u8")), L(15, "u8")))]),
    "E21 contract a&3==0, a+3":
        dict(params=[("a", "u8")], requires=[("mask", "a", (3, 0))],
             body=[("eval", ("add", "c21", "a", L(3, "u8")))]),
    "E22 contract a&3==0, a+4":
        dict(params=[("a", "u8")], requires=[("mask", "a", (3, 0))],
             body=[("eval", ("add", "c22", "a", L(4, "u8")))]),
    "E23 callee ensures r<=100, r+100":
        dict(params=[], calls=[("r", "u8", [("le", "r", 100)])],
             body=[("eval", ("add", "c23", "r", L(100, "u8")))]),
    "E24 callee ensures r<=200, r+100":
        dict(params=[], calls=[("r", "u8", [("le", "r", 200)])],
             body=[("eval", ("add", "c24", "r", L(100, "u8")))]),
    "E25 neg a, contract a>=-127 (i8)":
        dict(params=[("a", "i8")], requires=[("ge", "a", -127)],
             body=[("eval", ("neg", "c25", "a"))]),
    "E26 abs a, no fact (i8)":
        dict(params=[("a", "i8")],
             body=[("eval", ("abs", "c26", "a"))]),
    "E27 (not (a|0xF0))+240":
        dict(params=[("a", "u8")],
             body=[("eval", ("add", "c27", ("not", ("or", "a", L(0xF0, "u8"))), L(240, "u8")))]),
    "E28 s[i rem 4], contract len(s)>=4":
        dict(params=[("s", "slice"), ("i", "usize")], requires=[("ge", "s", 4)],
             body=[("eval", ("idx", "c28", "s", ("rem", "c30", "i", L(4, "usize"))))]),
    "E29 widen u16 a + 0xFF00, a:u8":
        dict(params=[("a", "u8")],
             body=[("eval", ("add", "c29", ("widen", "u16", "a"), L(0xFF00, "u16")))]),
    "E30 guard i<len(s), then i = i+1, s[i] (assignment kills the fact)":
        dict(params=[("s", "slice"), ("i", "usize")],
             body=[("guard", ("lt", "i", ("len", "s"))), ("let", "i", ("add", "c31", "i", L(1, "usize"))),
                   ("eval", ("idx", "c32", "s", "i"))]),
    "E31 guard i<len(s), then j = i+1, s[i] (another variable keeps it)":
        dict(params=[("s", "slice"), ("i", "usize")],
             body=[("guard", ("lt", "i", ("len", "s"))), ("let", "j", ("add", "c33", "i", L(1, "usize"))),
                   ("eval", ("idx", "c34", "s", "i"))]),
}

ANALYSES = {
    "const": Analysis(False, False, False, const_only=True),
    "I": Analysis(True, False, False),
    "T": Analysis(False, True, False),
    "IxT": Analysis(True, True, False),
    "IxT+L": Analysis(True, True, True),
}


def erase_contracts(fn):
    """fn as translated in a build mode that erases contract checks (spec 08 §2.3-2.4). No contract
    of the examples is `static`, so the erased requires and callee ensures are not facts and the
    program behaves as if they were absent."""
    out = dict(fn, requires=[])
    if fn.get("calls"):
        out["calls"] = [(n, t, []) for n, t, _ in fn["calls"]]
    return out


def has_contracts(fn):
    return bool(fn.get("requires")) or any(cs for _, _, cs in fn.get("calls", []))


def samples(t):
    lo, hi = trange(t)
    if hi - lo <= 255:
        return range(lo, hi + 1)
    pts = set(range(0, 40)) | {hi - d for d in range(40)} | set(range(lo, hi + 1, (hi - lo) // 200 + 1))
    pts |= {(1 << b) + d for b in range(TYPES[t][0]) for d in (-1, 0, 1)}
    if lo < 0:
        pts |= set(range(-1100, -900)) | set(range(900, 1100)) | {lo + d for d in range(40)}
    return sorted(x for x in pts if lo <= x <= hi)


def concrete(fn, args, lens):
    """run once; return the set of checks that fire (the first one stops the run)"""
    env = dict(args)

    def ok(v, t):
        a, b = trange(t)
        return a <= v <= b

    class Fire(Exception):
        pass

    def ev(e):
        if isinstance(e, tuple) and e[0] == "lit":
            return e[1], e[2]
        if isinstance(e, str):
            return env[e]
        op = e[0]
        if op == "len":
            return lens[e[1]], "usize"
        if op in ("add", "sub", "mul"):
            (a, t), (b, _) = ev(e[2]), ev(e[3])
            r = {"add": a + b, "sub": a - b, "mul": a * b}[op]
            if not ok(r, t):
                raise Fire(e[1])
            return r, t
        if op in ("and", "or", "xor"):
            (a, t), (b, _) = ev(e[1]), ev(e[2])
            w = TYPES[t][0]
            m = (1 << w) - 1
            r = {"and": a & b, "or": a | b, "xor": a ^ b}[op] & m
            if TYPES[t][1] and r >> (w - 1):
                r -= 1 << w
            return r, t
        if op in ("shl", "shr"):
            (a, t), (k, _) = ev(e[2]), ev(e[3])
            w = TYPES[t][0]
            if not 0 <= k < w:
                raise Fire(e[1])
            r = ((a << k) & ((1 << w) - 1)) if op == "shl" else a >> k
            return r, t
        if op == "narrow":
            a, _ = ev(e[3])
            if not ok(a, e[2]):
                raise Fire(e[1])
            return a, e[2]
        if op == "div":
            (a, t), (b, _) = ev(e[2]), ev(e[3])
            if b == 0:
                raise Fire(e[1])
            return a // b, t
        if op in ("neg", "abs"):
            a, t = ev(e[2])
            r = -a if op == "neg" else abs(a)
            if not ok(r, t):
                raise Fire(e[1])
            return r, t
        if op == "not":
            a, t = ev(e[1])
            w, sg = TYPES[t]
            return (~a if sg else (~a) & ((1 << w) - 1)), t
        if op == "rem":
            (a, t), (b, _) = ev(e[2]), ev(e[3])
            if b == 0:
                raise Fire(e[1])
            return a - b * int(a / b), t
        if op == "widen":
            a, _ = ev(e[2])
            return a, e[1]
        if op == "idx":
            i, _ = ev(e[3])
            if not 0 <= i < lens[e[2]]:
                raise Fire(e[1])
            return 0, "u8"
        raise ValueError(op)

    def block(stmts):
        for st in stmts:
            if st[0] == "let":
                env[st[1]] = ev(st[2])
            elif st[0] == "guard":
                op, x, rhs = st[1]
                v = env[x][0]
                r = lens[rhs[1]] if isinstance(rhs, tuple) else rhs
                if not {"lt": v < r, "le": v <= r, "ge": v >= r, "gt": v > r, "ne0": v != 0}[op]:
                    return
            elif st[0] == "eval":
                ev(st[1])
            elif st[0] == "for":
                n, _ = ev(st[2])
                for i in range(n):
                    env[st[1]] = (i, "usize")
                    block(st[3])
    try:
        block(fn["body"])
    except Fire as f:
        return {f.args[0]}
    return set()


def holds(op, v, rhs):
    if op == "mask":
        return (v & rhs[0]) == rhs[1]
    return {"lt": lambda: v < rhs, "le": lambda: v <= rhs, "ge": lambda: v >= rhs,
            "gt": lambda: v > rhs, "ne0": lambda: v != 0}[op]()


def witnesses(fn):
    """{check id: (args, lens)} -- the first sampled input (meeting the contracts) that fires it"""
    from itertools import product
    params = list(fn["params"]) + [(n, t) for n, t, _ in fn.get("calls", [])]
    names = [n for n, t in params if t != "slice"]
    types = dict(params)
    slices = [n for n, t in params if t == "slice"]
    doms = [samples(types[n]) if types[n] != "usize" else range(0, 12) for n in names]
    conds = list(fn.get("requires", [])) + [(op, n, rhs) for n, _, cs in fn.get("calls", []) for op, _, rhs in cs]
    out = {}
    for vals in product(*doms):
        for ls in product(*[range(0, 9) for _ in slices]):
            args = {n: (v, types[n]) for n, v in zip(names, vals)}
            lens = dict(zip(slices, ls))
            if all(holds(op, lens[x] if x in lens else args[x][0], rhs) for op, x, rhs in conds):
                for c in concrete(fn, args, lens):
                    out.setdefault(c, ({n: v for n, (v, _) in args.items()}, lens))
    return out


def check_kinds(fn):
    """{check id: kind} for the checks of an example, in body order"""
    kinds = {"add": "ovf", "sub": "ovf", "mul": "ovf", "neg": "ovf", "abs": "ovf", "narrow": "nar",
             "shl": "shc", "shr": "shc", "div": "div", "rem": "div", "idx": "idx"}
    out = {}

    def walk(x):
        if isinstance(x, (list, tuple)) and x:
            if isinstance(x[0], str):
                if x[0] in kinds and isinstance(x[1], str):
                    out.setdefault(x[1], kinds[x[0]])
                rest = x[1:]
            else:
                rest = x
            for y in rest:
                walk(y)
    walk(fn["body"])
    return out


def ground_truth(fn):
    """checks that some input satisfying the contracts makes fire"""
    from itertools import product
    params = list(fn["params"]) + [(n, t) for n, t, _ in fn.get("calls", [])]
    names = [n for n, t in params if t != "slice"]
    types = dict(params)
    slices = [n for n, t in fn["params"] if t == "slice"]
    doms = [samples(types[n]) if types[n] != "usize" else range(0, 12) for n in names]
    fired = set()
    for vals in product(*doms):
        for ls in product(*[range(0, 9) for _ in slices]):
            args = {n: (v, types[n]) for n, v in zip(names, vals)}
            lens = dict(zip(slices, ls))
            good = True
            conds = list(fn.get("requires", [])) + [(op, n, rhs) for n, _, cs in fn.get("calls", []) for op, _, rhs in cs]
            for op, x, rhs in conds:
                v = lens[x] if x in lens else args[x][0]
                good &= holds(op, v, rhs)
            if good:
                fired |= concrete(fn, args, lens)
    return fired


def main():
    res = {}
    for name, fn in EXAMPLES.items():
        res[name] = {a: run(fn, an) for a, an in ANALYSES.items()}
    if "--json" in sys.argv:
        print(json.dumps(res, indent=1, sort_keys=True))
        return
    print(f"{'example':38} {'check':5} " + " ".join(f"{a:6}" for a in ANALYSES))
    totals = {a: 0 for a in ANALYSES}
    nchk = 0
    for name, per in res.items():
        for cid in per["IxT+L"]:
            if not cid.startswith("c"):
                continue
            nchk += 1
            row = []
            for a in ANALYSES:
                r = per[a].get(cid)
                totals[a] += bool(r)
                row.append("remove" if r else "keep")
            print(f"{name:38} {cid:5} " + " ".join(f"{x:6}" for x in row))
    print(f"{'removed / ' + str(nchk):44} " + " ".join(f"{totals[a]:<6}" for a in ANALYSES))
    unsound, can_fire = [], 0
    for name, fn in EXAMPLES.items():
        fired = ground_truth(fn)
        can_fire += len(fired)
        for a in ANALYSES:
            unsound += [(name, a, c) for c, r in res[name][a].items() if r and c in fired]
    print(f"soundness: {'PASS' if not unsound else 'FAIL ' + str(unsound)} "
          f"(checks that some sampled input fires: {can_fire}; no analysis may remove those)")


if __name__ == "__main__":
    main()
