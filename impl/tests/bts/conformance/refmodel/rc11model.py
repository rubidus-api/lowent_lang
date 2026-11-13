"""Axiomatic RC11 (spec 07 §3; Lahav et al., PLDI 2017, REF-MM-03) for small litmus tests.

A test is a list of threads; each instruction is one of
    {"st": loc, "v": int, "mo": m}                    atomic store
    {"ld": loc, "r": reg, "mo": m}                    atomic load
    {"rmw": "xchg"|"fadd", "loc": loc, "v": int, "r": reg, "mo": m}
    {"fence": m}
with m in rlx, acq, rel, acq_rel, sc. Every location starts at 0. All accesses are atomic, so
there are no races. `outcomes(test)` enumerates every candidate execution (rf, mo), keeps the
RC11-consistent ones and returns the set of outcomes: final register values and final memory.
"""
from itertools import permutations, product

STRENGTH = {"rlx": 0, "acq": 1, "rel": 1, "acq_rel": 2, "sc": 3}


def is_acq(m):
    return m in ("acq", "acq_rel", "sc")


def is_rel(m):
    return m in ("rel", "acq_rel", "sc")


class Ev:
    def __init__(self, i, tid, kind, loc=None, mo="rlx", ins=None):
        self.i, self.tid, self.kind, self.loc, self.mo, self.ins = i, tid, kind, loc, mo, ins

    def __repr__(self):
        return f"{self.kind}{self.i}"


def events(test):
    locs = sorted({ins.get("st") or ins.get("ld") or ins.get("loc") for th in test for ins in th} - {None})
    evs, init = [], {}
    for x in locs:                                # initial writes, before everything
        e = Ev(len(evs), -1, "W", x, "rlx", {"v": 0})
        evs.append(e)
        init[x] = e
    thr, rmw = [], []
    for t, th in enumerate(test):
        seq = []
        for ins in th:
            if "st" in ins:
                seq.append(Ev(len(evs), t, "W", ins["st"], ins["mo"], ins))
                evs.append(seq[-1])
            elif "ld" in ins:
                seq.append(Ev(len(evs), t, "R", ins["ld"], ins["mo"], ins))
                evs.append(seq[-1])
            elif "rmw" in ins:                    # one read and one write event linked by rmw
                r = Ev(len(evs), t, "R", ins["loc"], ins["mo"], ins)
                evs.append(r)
                w = Ev(len(evs), t, "W", ins["loc"], ins["mo"], ins)
                evs.append(w)
                seq += [r, w]
                rmw.append((r.i, w.i))
            else:
                seq.append(Ev(len(evs), t, "F", None, ins["fence"], ins))
                evs.append(seq[-1])
        thr.append(seq)
    return evs, thr, init, rmw, locs


def closure(rel, n):
    """transitive closure of a relation given as a set of pairs over 0..n-1"""
    succ = [set() for _ in range(n)]
    for a, b in rel:
        succ[a].add(b)
    out = set()
    for a in range(n):
        seen, stack = set(), list(succ[a])
        while stack:
            b = stack.pop()
            if b not in seen:
                seen.add(b)
                stack.extend(succ[b])
        out |= {(a, b) for b in seen}
    return out


def comp(r1, r2):
    by = {}
    for a, b in r2:
        by.setdefault(a, set()).add(b)
    return {(a, c) for a, b in r1 for c in by.get(b, ())}


def acyclic(rel, n):
    return all(a != b for a, b in closure(rel, n))


def outcomes(test):
    evs, thr, init, rmw, locs = events(test)
    n = len(evs)
    sb = {(a.i, b.i) for seq in thr for k, a in enumerate(seq) for b in seq[k + 1:]}
    sb |= {(w.i, e.i) for w in init.values() for seq in thr for e in seq}
    reads = [e for e in evs if e.kind == "R"]
    writes = {x: [e for e in evs if e.kind == "W" and e.loc == x] for x in locs}
    rmw_of_r = dict(rmw)
    rmw_of_w = {w: r for r, w in rmw}
    result = set()
    for rf_choice in product(*[writes[r.loc] for r in reads]):
        rf = {(w.i, r.i) for w, r in zip(rf_choice, reads)}
        if not acyclic(sb | rf, n):              # 07 §3.3: no out-of-thin-air
            continue
        src = {r.i: w.i for w, r in zip(rf_choice, reads)}
        val = _values(evs, src, rmw_of_w, n, sb | rf)
        if val is None:
            continue
        for mos in product(*[permutations([w for w in writes[x] if w is not init[x]]) for x in locs]):
            mo = set()
            for x, order in zip(locs, mos):
                chain = [init[x]] + list(order)
                mo |= {(a.i, b.i) for k, a in enumerate(chain) for b in chain[k + 1:]}
            if _consistent(evs, sb, rf, mo, rmw, n):
                regs = tuple(sorted((e.ins["r"], val[e.i]) for e in reads))
                mem = tuple((x, val[([init[x]] + list(order))[-1].i]) for x, order in zip(locs, mos))
                result.add((regs, mem))
    return result


def _values(evs, src, rmw_of_w, n, order):
    """values of reads and writes, in an order compatible with sb ∪ rf (acyclic)"""
    val = {}
    pending = set(range(n))
    while pending:
        progress = False
        for i in sorted(pending):
            e = evs[i]
            if e.kind == "F":
                val[i] = None
            elif e.kind == "R":
                if src[i] not in val:
                    continue
                val[i] = val[src[i]]
            elif i in rmw_of_w:
                r = rmw_of_w[i]
                if r not in val:
                    continue
                val[i] = e.ins["v"] if e.ins["rmw"] == "xchg" else val[r] + e.ins["v"]
            else:
                val[i] = e.ins["v"]
            pending.discard(i)
            progress = True
        if not progress:
            return None
    return val


def _consistent(evs, sb, rf, mo, rmw, n):
    E = range(n)
    loc = {(a, b) for a in E for b in E if evs[a].loc is not None and evs[a].loc == evs[b].loc}
    rb = comp({(b, a) for a, b in rf}, mo) - {(a, a) for a in E}
    eco = closure(rf | mo | rb, n)
    W = {e.i for e in evs if e.kind == "W"}
    R = {e.i for e in evs if e.kind == "R"}
    F = {e.i for e in evs if e.kind == "F"}
    rel_ = {e.i for e in evs if is_rel(e.mo) and e.kind in "WF"}
    acq_ = {e.i for e in evs if is_acq(e.mo) and e.kind in "RF"}
    ident = lambda s: {(a, a) for a in s}
    rmwr = set(rmw)
    # release sequence: [W] ; (sb|loc)? ; [W] ; (rf ; rmw)*
    rfrmw = closure(comp(rf, rmwr), n) | ident(E)
    sbloc = (sb & loc) | ident(E)
    rs = comp(comp(comp(ident(W), sbloc), ident(W)), rfrmw)
    # sw = [rel] ; ([F] ; sb)? ; rs ; rf ; [R] ; (sb ; [F])? ; [acq]
    pre = ident(rel_) | comp(ident(rel_ & F), sb)
    post = ident(acq_) | comp(sb, ident(acq_ & F))
    sw = comp(comp(comp(comp(pre, rs), rf), ident(R)), post)
    sw = {(a, b) for a, b in sw if (a in rel_ and (a in F or a in W)) and b in acq_}
    hb = closure(sb | sw, n)
    # coherence: hb ; eco? irreflexive
    if any(a == b for a, b in hb | comp(hb, eco)):
        return False
    # atomicity: rmw ∩ (rb ; mo) = ∅
    if rmwr & comp(rb, mo):
        return False
    # SC: psc acyclic
    SC = {e.i for e in evs if e.mo == "sc"}
    hbq = hb | ident(E)
    sbneq = sb - loc
    scb = sb | comp(comp(sbneq, hb), sbneq) | (hb & loc) | mo | rb
    left = ident(SC - F) | comp(ident(SC & F), hbq)
    right = ident(SC - F) | comp(hbq, ident(SC & F))
    psc = comp(comp(left, scb), right)
    psc |= comp(comp(ident(SC & F), hb | comp(comp(hb, eco), hb)), ident(SC & F))
    return acyclic(psc, n)


def sc_outcomes(test):
    """outcomes of all interleavings (sequential consistency); independent of the axioms above"""
    out = set()
    locs = sorted({ins.get("st") or ins.get("ld") or ins.get("loc") for th in test for ins in th} - {None})

    def go(pcs, mem, regs):
        done = True
        for t, th in enumerate(test):
            if pcs[t] < len(th):
                done = False
                ins = th[pcs[t]]
                m, r = dict(mem), dict(regs)
                if "st" in ins:
                    m[ins["st"]] = ins["v"]
                elif "ld" in ins:
                    r[ins["r"]] = m[ins["ld"]]
                elif "rmw" in ins:
                    r[ins["r"]] = m[ins["loc"]]
                    m[ins["loc"]] = ins["v"] if ins["rmw"] == "xchg" else m[ins["loc"]] + ins["v"]
                go(pcs[:t] + (pcs[t] + 1,) + pcs[t + 1:], m, r)
        if done:
            out.add((tuple(sorted(regs.items())), tuple((x, mem[x]) for x in locs)))
    go(tuple(0 for _ in test), {x: 0 for x in locs}, {})
    return out
