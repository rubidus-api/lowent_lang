#!/usr/bin/env python3
"""Validate a runner results file (bts-results/1 or /2, harness-protocol.md §5).

Checks: the header names every vector file it used with the sha256 of the copy here; each row has a
known suite and status; fail/skip/error carry a reason and skip reasons come from the closed list;
the summary line matches the rows; a suite is reported as conforming only when it has no fail, no
error and no not-implemented skip.
bts-results/2 adds the header's `declaration` (harness §5.1): every suite is declared supported or
omitted; an omitted suite has only language-policy skips and is not-applicable; a supported suite
with no row run makes no claim; row `impl` names come from the declared variants (all rows name one
when there are several); a run that removes checks claims only build_modes; the summary `claim`
names suite, vector hash, target, build mode and variants, and says so when the build mode erases
contract checks (`erases_contracts`, spec 08 §2.4) or that the language is in the script profile
(`profile`, spec 01 §2.1; `system` when absent). In /2 a row's `row` is its global row number in
the suite (0 to N-1, in the row units of harness-protocol.md §5.2); a number outside the vector
file or reported twice for the same implementation is an error, and a suite claims conformance only
when every declared implementation ran a row and reported every one of the N rows (harness §3); with
one implementation a row without `impl` is that one, with several an unnamed skip counts for each, and
a variant name may not be declared twice.

    python3 -I docs/conformance/check_results.py results.jsonl
"""
import hashlib
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from gen import count_rows      # noqa: E402  (row units: harness-protocol.md §5.2, copied in gen.ROW_UNITS)
SUITES = {"integer_ops", "casting_rules", "struct_layout", "range_erasure", "device_access", "float_ops", "result_sets", "alloc_sequences", "litmus", "programs", "simd_ops", "fixed_ops", "canonical_encoding", "build_modes", "bigint_ops", "decimal_ops"}
STATUS = {"pass", "fail", "skip", "error"}
SCHEMAS = {"bts-results/1", "bts-results/2"}
PROFILES = {"system", "script"}             # language profiles (spec 01 §2.1)
REMOVABLE = {"ovf", "shc", "nar"}          # checks a build mode may remove (spec 08 §1.2)
SKIPS = {"language-policy", "target-unavailable", "not-implemented"}
ALL = object()                             # the variant of an unnamed skip row when several are declared


def check(lines):
    errs = []
    objs = []
    for n, line in enumerate(lines, 1):
        if line.strip():
            try:
                objs.append((n, json.loads(line)))
            except ValueError:
                errs.append(f"line {n}: not JSON")
    if len(objs) < 2:
        return errs + ["need a header and a summary line"]
    (hn, head), rows, (sn, summ) = objs[0], objs[1:-1], objs[-1]
    for k in ("schema", "runner", "language", "processor", "processor_version", "build_mode", "target", "vectors"):
        if k not in head:
            errs.append(f"header: missing {k}")
    v2 = head.get("schema") == "bts-results/2"
    if head.get("schema") not in SCHEMAS:
        errs.append("header: schema is not bts-results/1 or bts-results/2")
    decl = head.get("declaration", {}) if v2 else {}
    if v2:
        suites, impls, removes = decl.get("suites"), decl.get("implementations"), decl.get("removes")
        if not isinstance(suites, dict) or not set(suites) <= SUITES or not set(suites.values()) <= {"supported", "omitted"}:
            errs.append("header: declaration.suites must map known suites to supported or omitted")
            decl["suites"] = suites = {}
        if not isinstance(impls, list) or not impls or not all(isinstance(x, str) for x in impls):
            errs.append("header: declaration.implementations must be a non-empty list of names")
            decl["implementations"] = []
        elif len(set(impls)) != len(impls):
            errs.append("header: declaration.implementations names a variant twice")
        if not isinstance(removes, list) or not set(removes) <= REMOVABLE:
            errs.append("header: declaration.removes must be a subset of ovf, shc, nar")
            decl["removes"] = []
        if not isinstance(decl.get("erases_contracts", False), bool):
            errs.append("header: declaration.erases_contracts must be true or false")
            decl["erases_contracts"] = False
        if decl.get("profile", "system") not in PROFILES:
            errs.append("header: declaration.profile must be system or script")
            decl["profile"] = "system"
    for name, digest in head.get("vectors", {}).items():
        p = os.path.join(HERE, "vectors", name)
        if not os.path.exists(p):
            errs.append(f"header: unknown vector file {name}")
        elif hashlib.sha256(open(p, "rb").read()).hexdigest() != digest:
            errs.append(f"header: {name} sha256 does not match this copy")
    count, sizes, seen, ran = {}, {}, {}, {}
    for n, r in rows:
        if r.get("suite") not in SUITES or r.get("status") not in STATUS or "case" not in r or "row" not in r:
            errs.append(f"line {n}: row shape")
            continue
        if v2:
            suite = r["suite"]
            if suite not in sizes:
                with open(os.path.join(HERE, "vectors", suite + ".json"), encoding="utf-8") as f:
                    sizes[suite] = count_rows(json.load(f))
            row, impls = r["row"], decl["implementations"]
            impl = r.get("impl", impls[0] if len(impls) == 1 else ALL)    # an unnamed skip reports every variant
            rep = seen.setdefault(suite, {})
            if not isinstance(row, int) or isinstance(row, bool) or not 0 <= row < sizes[suite]:
                errs.append(f"line {n}: row {row!r} is not a row number of {suite} (0 to {sizes[suite] - 1})")
            elif row in rep.get(ALL, ()) or (any(row in x for x in rep.values()) if impl is ALL else row in rep.get(impl, ())):
                errs.append(f"line {n}: {suite} row {row} reported twice")
            else:
                rep.setdefault(impl, set()).add(row)
                if r["status"] != "skip":
                    ran.setdefault(suite, set()).add(impl)
        if r["status"] != "pass" and not r.get("reason"):
            errs.append(f"line {n}: {r['status']} without a reason")
        if r["status"] == "skip" and r.get("reason") not in SKIPS:
            errs.append(f"line {n}: skip reason outside the closed list")
        if v2:
            state = decl["suites"].get(r["suite"])
            if state is None:
                errs.append(f"line {n}: suite {r['suite']} is not declared")
            elif state == "omitted" and not (r["status"] == "skip" and r.get("reason") == "language-policy"):
                errs.append(f"line {n}: an omitted suite may only have language-policy skips")
            if "impl" in r and r["impl"] not in decl["implementations"]:
                errs.append(f"line {n}: impl {r['impl']} is not declared")
            if len(decl["implementations"]) > 1 and r["status"] != "skip" and "impl" not in r:
                errs.append(f"line {n}: several implementations are declared, so the row must name one")
        c = count.setdefault(r["suite"], {"pass": 0, "fail": 0, "skip": 0, "error": 0, "ni": 0})
        c[r["status"]] += 1
        c["ni"] += r["status"] == "skip" and r.get("reason") == "not-implemented"
    s = summ.get("summary")
    if not isinstance(s, dict):
        return errs + [f"line {sn}: last line is not a summary"]
    if v2:
        for suite in decl["suites"]:
            count.setdefault(suite, {"pass": 0, "fail": 0, "skip": 0, "error": 0, "ni": 0})
    for suite, c in count.items():
        got = s.get(suite, {})
        for k in ("pass", "fail", "skip", "error"):
            if got.get(k) != c[k]:
                errs.append(f"summary {suite}: {k} {got.get(k)} != rows {c[k]}")
        conforms = c["fail"] == 0 and c["error"] == 0 and c["ni"] == 0
        if v2:
            rep = seen.get(suite, {})
            conforms = conforms and claimable(suite, c, head, decl) and all(   # every variant ran, and reported all N rows
                i in ran.get(suite, ()) and len(rep.get(i, set()) | rep.get(ALL, set())) == sizes.get(suite)
                for i in decl["implementations"])
            want = claim(suite, head, decl) if conforms else (
                "not-applicable" if decl["suites"].get(suite) == "omitted" else None)
            if got.get("claim") != want:
                errs.append(f"summary {suite}: claim must be {want!r}")
        if got.get("conforms") != conforms:
            errs.append(f"summary {suite}: conforms must be {conforms}")
    return errs


def claimable(suite, c, head, decl):
    """bts-results/2: what a conformance claim needs besides no fail, error or not-implemented"""
    return (decl["suites"].get(suite) == "supported"
            and c["pass"] + c["fail"] + c["error"] > 0                     # something actually ran
            and (not decl["removes"] or suite == "build_modes")          # other suites need every check kept
            and f"{suite}.json" in head.get("vectors", {}))


def claim(suite, head, decl):
    """the scoped claim text: suite, vector hash, target, build mode, implementation variants"""
    return (f"{suite} vectors:{head['vectors'][suite + '.json'][:16]} target:{head['target']} "
            f"mode:{head['build_mode']} impl:{'+'.join(decl['implementations'])}"
            + (" contracts:erased" if decl.get("erases_contracts") else "")
            + (" profile:script" if decl.get("profile") == "script" else ""))


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    errs = check(open(sys.argv[1], encoding="utf-8").read().splitlines())
    for e in errs:
        print("FAIL", e)
    print("check_results:", "ok" if not errs else f"FAIL ({len(errs)})")
    return 1 if errs else 0


if __name__ == "__main__":
    raise SystemExit(main())
