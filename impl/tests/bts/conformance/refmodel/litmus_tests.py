"""Classic litmus tests (spec 07 §3.2-3.3) in the instruction form of rc11model.py.
`probe` names the outcome the test is about: registers and, where needed, final memory."""


def st(x, v, m):
    return {"st": x, "v": v, "mo": m}


def ld(x, r, m):
    return {"ld": x, "r": r, "mo": m}


def fence(m):
    return {"fence": m}


def rmw(op, x, v, r, m):
    return {"rmw": op, "loc": x, "v": v, "r": r, "mo": m}


def sb(m_st, m_ld, f=None):
    a = [st("x", 1, m_st)] + ([fence(f)] if f else []) + [ld("y", "r0", m_ld)]
    b = [st("y", 1, m_st)] + ([fence(f)] if f else []) + [ld("x", "r1", m_ld)]
    return [a, b]


def mp(m_st, m_ld, f_w=None, f_r=None):
    a = [st("x", 1, "rlx")] + ([fence(f_w)] if f_w else []) + [st("y", 1, m_st)]
    b = [ld("y", "r0", m_ld)] + ([fence(f_r)] if f_r else []) + [ld("x", "r1", "rlx")]
    return [a, b]


def lb(m):
    return [[ld("x", "r0", m), st("y", 1, m)], [ld("y", "r1", m), st("x", 1, m)]]


def iriw(m_st, m_ld):
    return [[st("x", 1, m_st)], [st("y", 1, m_st)],
            [ld("x", "r0", m_ld), ld("y", "r1", m_ld)], [ld("y", "r2", m_ld), ld("x", "r3", m_ld)]]


def w22(m):
    return [[st("x", 1, m), st("y", 2, m)], [st("y", 1, m), st("x", 2, m)]]


def wrc(m_st, m_ld):
    return [[st("x", 1, "rlx")], [ld("x", "r0", "rlx"), st("y", 1, m_st)], [ld("y", "r1", m_ld), ld("x", "r2", "rlx")]]


def isa2(m_st, m_ld):
    return [[st("x", 1, "rlx"), st("y", 1, m_st)], [ld("y", "r0", m_ld), st("z", 1, m_st)],
            [ld("z", "r1", m_ld), ld("x", "r2", "rlx")]]


def r_test(m):
    return [[st("x", 1, m), st("y", 1, m)], [st("y", 2, m), ld("x", "r0", m)]]


def s_test(m_st, m_ld):
    return [[st("x", 2, "rlx"), st("y", 1, m_st)], [ld("y", "r0", m_ld), st("x", 1, "rlx")]]


# name -> (test, probe outcome {reg/loc: value})
TESTS = {
    "SB+rlx":          (sb("rlx", "rlx"), {"r0": 0, "r1": 0}),
    "SB+rel+acq":      (sb("rel", "acq"), {"r0": 0, "r1": 0}),
    "SB+sc":           (sb("sc", "sc"), {"r0": 0, "r1": 0}),
    "SB+rlx+scfences": (sb("rlx", "rlx", "sc"), {"r0": 0, "r1": 0}),
    "MP+rlx":          (mp("rlx", "rlx"), {"r0": 1, "r1": 0}),
    "MP+rel+acq":      (mp("rel", "acq"), {"r0": 1, "r1": 0}),
    "MP+rel+rlx":      (mp("rel", "rlx"), {"r0": 1, "r1": 0}),
    "MP+fences":       (mp("rlx", "rlx", "rel", "acq"), {"r0": 1, "r1": 0}),
    "MP+sc":           (mp("sc", "sc"), {"r0": 1, "r1": 0}),
    "LB+rlx":          (lb("rlx"), {"r0": 1, "r1": 1}),
    "IRIW+rlx":        (iriw("rlx", "rlx"), {"r0": 1, "r1": 0, "r2": 1, "r3": 0}),
    "IRIW+rel+acq":    (iriw("rel", "acq"), {"r0": 1, "r1": 0, "r2": 1, "r3": 0}),
    "IRIW+sc":         (iriw("sc", "sc"), {"r0": 1, "r1": 0, "r2": 1, "r3": 0}),
    "2+2W+rlx":        (w22("rlx"), {"x": 1, "y": 1}),
    "2+2W+sc":         (w22("sc"), {"x": 1, "y": 1}),
    "WRC+rlx":         (wrc("rlx", "rlx"), {"r0": 1, "r1": 1, "r2": 0}),
    "WRC+rel+acq":     (wrc("rel", "acq"), {"r0": 1, "r1": 1, "r2": 0}),
    "ISA2+rel+acq":    (isa2("rel", "acq"), {"r0": 1, "r1": 1, "r2": 0}),
    "R+rlx":           (r_test("rlx"), {"y": 2, "r0": 0}),
    "R+sc":            (r_test("sc"), {"y": 2, "r0": 0}),
    "S+rel+acq":       (s_test("rel", "acq"), {"x": 2, "r0": 1}),
    "S+rlx":           (s_test("rlx", "rlx"), {"x": 2, "r0": 1}),
    "CoRR":            ([[st("x", 1, "rlx")], [ld("x", "r0", "rlx"), ld("x", "r1", "rlx")]], {"r0": 1, "r1": 0}),
    "CoWW":            ([[st("x", 1, "rlx"), st("x", 2, "rlx")]], {"x": 1}),
    "CoWR":            ([[st("x", 1, "rlx"), ld("x", "r0", "rlx")], [st("x", 2, "rlx")]], {"r0": 2, "x": 1}),
    "CoRW":            ([[ld("x", "r0", "rlx"), st("x", 1, "rlx")], [st("x", 2, "rlx")]], {"r0": 2, "x": 2}),
    "RMW-atomic":      ([[rmw("fadd", "x", 1, "r0", "rlx")], [rmw("fadd", "x", 1, "r1", "rlx")]], {"r0": 0, "r1": 0}),
    "RMW-xchg":        ([[rmw("xchg", "x", 1, "r0", "rlx")], [rmw("xchg", "x", 2, "r1", "rlx")]], {"r0": 0, "r1": 0}),
    "MP+rel-rmw-acq":  ([[st("x", 1, "rlx"), st("y", 1, "rel")], [rmw("fadd", "y", 1, "r2", "rlx")],
                        [ld("y", "r0", "acq"), ld("x", "r1", "rlx")]], {"r0": 2, "r1": 0}),
}
