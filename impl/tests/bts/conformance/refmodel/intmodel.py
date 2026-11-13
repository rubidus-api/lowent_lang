"""Reference model for integer operations (report C, R5.1, R5.4).

Every operation is defined in two stages (report C §2.1):
  1. the exact result over the integers (Python int),
  2. a disposition that brings it back into the result type.

The model is the definition the conformance vectors are generated from. It uses
only the Python standard library.
"""

TRAP = "trap"          # execution stops
NONE = "none"          # the `chk` disposition's failure value


class IntType:
    def __init__(self, name, bits, signed):
        self.name, self.bits, self.signed = name, bits, signed
        self.min = -(1 << (bits - 1)) if signed else 0
        self.max = (1 << (bits - 1)) - 1 if signed else (1 << bits) - 1

    def contains(self, v):
        return self.min <= v <= self.max

    def wrap(self, v):
        m = 1 << self.bits
        v %= m
        return v - m if self.signed and v > self.max else v

    def sat(self, v):
        return max(self.min, min(self.max, v))

    def __repr__(self):
        return self.name


TYPES = {n: IntType(n, b, s) for n, b, s in [
    ("i8", 8, True), ("i16", 16, True), ("i32", 32, True), ("i64", 64, True), ("i128", 128, True),
    ("u8", 8, False), ("u16", 16, False), ("u32", 32, False), ("u64", 64, False), ("u128", 128, False)]}
REQUIRED = ["i8", "i16", "i32", "u8", "u16", "u32"]          # R1.1
OPTIONAL = ["i64", "u64", "i128", "u128"]                   # R1.2


def dispose(t, exact, disposition):
    """Stage 2. `exact` is None when the exact result does not exist (division by zero)."""
    if exact is None:
        return NONE if disposition == "chk" else TRAP
    if t.contains(exact):
        return exact
    return {"wrap": t.wrap, "sat": t.sat}.get(disposition, lambda v: NONE if disposition == "chk" else TRAP)(exact)


# ---- stage 1: exact results ----
def _trunc(a, b):
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b > 0) else -q


EXACT_BINARY = {
    "add": lambda a, b: a + b,
    "sub": lambda a, b: a - b,
    "mul": lambda a, b: a * b,
    "div_trunc": lambda a, b: None if b == 0 else _trunc(a, b),
    "rem_trunc": lambda a, b: None if b == 0 else a - b * _trunc(a, b),
    "div_floor": lambda a, b: None if b == 0 else a // b,
    "mod_floor": lambda a, b: None if b == 0 else a % b,
    "div_euclid": lambda a, b: None if b == 0 else (a - a % abs(b)) // b,
    "rem_euclid": lambda a, b: None if b == 0 else a % abs(b),
}
EXACT_UNARY = {"neg": lambda a: -a, "abs": lambda a: abs(a)}
DIVISION = {"div_trunc", "rem_trunc", "div_floor", "mod_floor", "div_euclid", "rem_euclid"}

# Dispositions each canonical operation admits (report C §4.0).
ADMITS = {op: ["wrap", "sat", "chk", "trap"] for op in ("add", "sub", "mul", "neg", "abs")}
ADMITS.update({op: ["wrap", "chk", "trap"] for op in DIVISION})


def binary(op, t, a, b, disposition):
    exact = EXACT_BINARY[op](a, b)
    if op in DIVISION and disposition == "wrap":
        # division by zero has no value even under wrap; MIN / -1 wraps to MIN, its remainder is 0
        if exact is None:
            return TRAP
    return dispose(t, exact, disposition)


def unary(op, t, a, disposition):
    return dispose(t, EXACT_UNARY[op](a), disposition)


# ---- shifts and rotates: the count is normalized before stage 1 (report C §4.0 note) ----
def shift(op, t, a, k, count_disposition):
    if k < 0 or k >= t.bits:
        if count_disposition == "trap":
            return TRAP
        k %= t.bits
    if op == "shl":
        return t.wrap(a << k)                  # bits shifted out are dropped
    if op == "shr":
        return a >> k                          # arithmetic for signed, logical for unsigned values
    raise ValueError(op)


def rotate(op, t, a, k):
    k %= t.bits
    u = a & ((1 << t.bits) - 1)
    r = ((u << k) | (u >> (t.bits - k))) if op == "rotl" else ((u >> k) | (u << (t.bits - k)))
    return t.wrap(r & ((1 << t.bits) - 1))


# ---- carry, borrow, wide multiply (unsigned; report C, BT-2) ----
def add_carry(t, a, b, c):
    s = a + b + c
    return [s & t.max, s >> t.bits]


def sub_borrow(t, a, b, c):
    d = a - b - c
    return [d % (1 << t.bits), 1 if d < 0 else 0]


def mul_wide(t, a, b):
    p = a * b
    return [p & t.max, p >> t.bits]            # {lo, hi}, lo first (report C §5)


def mul_wide_s(t, a, b):
    """signed widening multiply (02 §2.6): exact a*b as 2N-bit two's complement, lo unsigned, hi signed"""
    p = a * b
    lo = p & t.max if not t.signed else p % (1 << t.bits)
    return [lo, (p - lo) >> t.bits]


# ---- comparison across types (BT-6), select, masks ----
def cmp(op, a, b):
    return {"cmp_eq": a == b, "cmp_lt": a < b, "cmp_le": a <= b}[op]


def select(t, m, a, b):
    allones = (1 << t.bits) - 1
    mu, au, bu = m & allones, a & allones, b & allones
    return t.wrap((au & mu) | (bu & ~mu & allones))


def eq_mask(t, a, b):
    return t.wrap(((1 << t.bits) - 1) if a == b else 0)


# ---- order keys (02 §3.3, owner-approved item Z4) ----
def order_key(t, a):
    u = a & ((1 << t.bits) - 1)
    return u ^ (1 << (t.bits - 1)) if t.signed else u


def order_key_f32(bits):
    return (~bits & 0xFFFFFFFF) if bits & 0x80000000 else bits | 0x80000000


# ---- division by a public constant (02 §2.4, gate 13: every constant divisor, every dividend) ----
def div_const_magic(t, d):
    """Granlund and Montgomery (PLDI 1994), figures 4.1 and 5.1: the multiplier and shifts that give the
    truncated quotient x / d for every x of type t with one N-bit high multiply and a fix-up"""
    N = t.bits
    if d == 0 or (t.signed and d == -1) or not t.contains(d):
        return None                                   # rejected at translation (02 §2.4)
    if not t.signed:
        l = (d - 1).bit_length()                      # ceil(log2 d)
        m = ((1 << N) * ((1 << l) - d)) // d + 1
        return {"m": m, "sh1": min(l, 1), "sh2": max(l - 1, 0)}
    a = abs(d)
    l = max((a - 1).bit_length(), 1)
    m = 1 + (1 << (N + l - 1)) // a - (1 << N)        # m' as a signed N-bit multiplier
    return {"m": m, "sh": l - 1, "neg": d < 0}


def div_const(t, x, d):
    """the quotient computed the way a processor would: high half of an N-bit multiply plus fix-ups"""
    N, mask = t.bits, (1 << t.bits) - 1
    g = div_const_magic(t, d)
    if not t.signed:
        t1 = (g["m"] * x) >> N                        # MULUH
        return (t1 + ((x - t1) >> g["sh1"])) >> g["sh2"]
    q0 = x + ((g["m"] * x) >> N)                      # n + MULSH(m', n), exact in Python, wraps below
    q0 = (q0 >> g["sh"]) - (-1 if x < 0 else 0)       # SRA, minus XSIGN(n)
    q = -q0 if g["neg"] else q0
    return t.wrap(q)


def rem_const(t, x, d):
    return t.wrap(x - div_const(t, x, d) * d)


# ---- timing classes (R5.4, report C §4.0) ----
def timing(op, disposition=None):
    if op in DIVISION:
        return "vt"
    if op in ("add", "sub", "mul", "neg", "abs"):
        return "ct" if disposition in ("wrap", "sat") else "vt"
    return "ct"   # shl/shr/rotl/rotr (public count), carry ops, cmp_*, select, eq_mask, order_key, div_const, rem_const


# ---- surface names in the two languages (informative; current public specs) ----
SURFACE = {
    ("add", "trap"): {"lowent": "add", "pinax": "add"},
    ("add", "wrap"): {"lowent": "wrap_add", "pinax": "wrap_add"},
    ("add", "sat"): {"lowent": "sat_add", "pinax": "sat_add"},
    ("add", "chk"): {"lowent": "chk_add", "pinax": "chk_add"},
    ("sub", "trap"): {"lowent": "sub", "pinax": "sub"},
    ("sub", "wrap"): {"lowent": "wrap_sub", "pinax": "wrap_sub"},
    ("sub", "sat"): {"lowent": "sat_sub", "pinax": "sat_sub"},
    ("sub", "chk"): {"lowent": "chk_sub", "pinax": "chk_sub"},
    ("mul", "trap"): {"lowent": "mul", "pinax": "mul"},
    ("mul", "wrap"): {"lowent": "wrap_mul", "pinax": "wrap_mul"},
    ("mul", "sat"): {"lowent": "sat_mul", "pinax": "sat_mul"},
    ("mul", "chk"): {"lowent": "chk_mul", "pinax": "chk_mul"},
    ("neg", "trap"): {"lowent": None, "pinax": "neg"},
    ("abs", "trap"): {"lowent": None, "pinax": "abs"},
    ("div_trunc", "trap"): {"lowent": "div", "pinax": "div"},
    ("div_trunc", "chk"): {"lowent": None, "pinax": "chk_div"},
    ("rem_trunc", "trap"): {"lowent": None, "pinax": "mod"},
    ("div_floor", "trap"): {"lowent": None, "pinax": "floor_div"},
    ("mod_floor", "trap"): {"lowent": "mod", "pinax": "floor_mod"},
    ("shl", "trap"): {"lowent": "shl", "pinax": None},
    ("shl", "wrap"): {"lowent": "wrap_shl", "pinax": "shl"},
    ("shr", "trap"): {"lowent": "shr", "pinax": None},
    ("shr", "wrap"): {"lowent": "wrap_shr", "pinax": "shr"},
    ("rotl", None): {"lowent": "rotl", "pinax": "rotl"},
    ("rotr", None): {"lowent": "rotr", "pinax": "rotr"},
}


def surface(op, disposition, type_name=None):
    names = dict(SURFACE.get((op, disposition), {"lowent": None, "pinax": None}))
    # pinax states the count rule (mod 64) only for 64-bit operands; for narrow types the rule
    # was not found in its public spec (report C §6.1), so no name is claimed there.
    if op in ("shl", "shr") and type_name is not None and TYPES[type_name].bits != 64:
        names["pinax"] = None
    return names
