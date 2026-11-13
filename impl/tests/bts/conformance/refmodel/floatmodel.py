"""Reference model for binary floating point (spec 02 §3, report H §4.0).

Every result is computed exactly as a rational number (or, for sqrt, by integer square
root with an exactness test) and rounded once to the format: nearest, ties to even,
subnormals kept, overflow to infinity. No host floating point is used here, so the
host-float and C cross-checks in check.py are independent of this file.

Values are bit patterns (int). A NaN result is reported as the string "nan": its payload
is Free Choice (spec 08 §4.1 FC-01).
"""
from fractions import Fraction
from math import isqrt


class Fmt:
    def __init__(self, name, bits, ebits):
        self.name, self.bits, self.ebits = name, bits, ebits
        self.p = bits - ebits                    # significand precision, hidden bit included
        self.bias = (1 << (ebits - 1)) - 1
        self.emin = 1 - self.bias
        self.emax = self.bias
        self.sign = 1 << (bits - 1)
        self.inf = ((1 << ebits) - 1) << (self.p - 1)
        self.qnan = self.inf | (1 << (self.p - 2))
        self.fmask = (1 << (self.p - 1)) - 1


F32 = Fmt("f32", 32, 8)
F64 = Fmt("f64", 64, 11)
FORMATS = {"f32": F32, "f64": F64}


def is_nan(f, b):
    return (b & ~f.sign) > f.inf


def is_inf(f, b):
    return (b & ~f.sign) == f.inf


def neg(f, b):
    return b ^ f.sign


def decode(f, b):
    """finite bit pattern -> (sign, Fraction magnitude)"""
    s = 1 if b & f.sign else 0
    e = (b >> (f.p - 1)) & ((1 << f.ebits) - 1)
    m = b & f.fmask
    if e == 0:
        v = Fraction(m) * Fraction(2) ** (f.emin - (f.p - 1))
    else:
        v = Fraction(m | (1 << (f.p - 1))) * Fraction(2) ** (e - f.bias - (f.p - 1))
    return s, v


def floor_log2(q):
    e = q.numerator.bit_length() - q.denominator.bit_length()
    if q < Fraction(2) ** e:
        e -= 1
    return e


def _pack(f, s, n, qexp):
    """sign, integer significand n (already rounded), value = n * 2**qexp -> bits (inf on overflow)"""
    if n == 0:
        return s * f.sign
    if n >> f.p:                                  # carry out of rounding: renormalise
        n >>= 1
        qexp += 1
    top = qexp + f.p - 1                          # exponent of the leading bit if normal
    if n >> (f.p - 1):                            # normal
        if top > f.emax:
            return s * f.sign | f.inf
        return s * f.sign | ((top + f.bias) << (f.p - 1)) | (n & f.fmask)
    return s * f.sign | n                         # subnormal (qexp == emin - p + 1)


def _round_int(num, den):
    """nearest integer to num/den (both > 0), ties to even"""
    q, r = divmod(num, den)
    if 2 * r > den or (2 * r == den and q & 1):
        q += 1
    return q


def round_value(f, s, v):
    """round sign s, exact magnitude v (Fraction >= 0) to the format"""
    if v == 0:
        return s * f.sign
    e = max(floor_log2(v), f.emin)
    qexp = e - (f.p - 1)
    w = v / Fraction(2) ** qexp
    return _pack(f, s, _round_int(w.numerator, w.denominator), qexp)


def _round_sqrt(f, v):
    """correctly rounded sqrt of a positive Fraction v"""
    e = max(floor_log2(v) // 2, f.emin)
    qexp = e - (f.p - 1)
    r = v / Fraction(2) ** (2 * qexp)             # want nearest integer to sqrt(r)
    four = 4 * r
    h = isqrt(four.numerator // four.denominator)  # floor(2 sqrt(r))
    n = h // 2
    if h & 1:
        exact_half = four.denominator == 1 and h * h == four.numerator
        if not exact_half or n & 1:
            n += 1
    return _pack(f, 0, n, qexp)


def binary(op, f, a, b):
    if is_nan(f, a) or is_nan(f, b):
        return "nan"
    if op == "sub":
        return binary("add", f, a, neg(f, b))
    sa, sb = (1 if a & f.sign else 0), (1 if b & f.sign else 0)
    ia, ib = is_inf(f, a), is_inf(f, b)
    if op == "add":
        if ia or ib:
            if ia and ib and sa != sb:
                return "nan"
            return a if ia else b
        _, va = decode(f, a)
        _, vb = decode(f, b)
        x = (-va if sa else va) + (-vb if sb else vb)
        if x == 0:
            return f.sign if (sa and sb) else 0   # exact zero sum is +0 unless both are -0
        return round_value(f, 1 if x < 0 else 0, abs(x))
    s = sa ^ sb
    za, zb = (a & ~f.sign) == 0, (b & ~f.sign) == 0
    if op == "mul":
        if (ia and zb) or (ib and za):
            return "nan"
        if ia or ib:
            return s * f.sign | f.inf
        return round_value(f, s, decode(f, a)[1] * decode(f, b)[1])
    if op == "div":
        if (ia and ib) or (za and zb):
            return "nan"
        if ia or zb:
            return s * f.sign | f.inf
        if ib or za:
            return s * f.sign
        return round_value(f, s, decode(f, a)[1] / decode(f, b)[1])
    raise ValueError(op)


def sqrt(f, a):
    if is_nan(f, a):
        return "nan"
    if (a & ~f.sign) == 0:
        return a                                  # sqrt(-0) = -0
    if a & f.sign:
        return "nan"
    if is_inf(f, a):
        return a
    return _round_sqrt(f, decode(f, a)[1])


def compare(op, f, a, b):
    if is_nan(f, a) or is_nan(f, b):
        return False
    if is_inf(f, a) or is_inf(f, b):
        ka, kb = order_key(f, a), order_key(f, b)  # infinities and finite values order like their keys
    else:
        sa, va = decode(f, a)
        sb, vb = decode(f, b)
        ka, kb = (-va if sa else va), (-vb if sb else vb)
    return ka < kb if op == "lt" else ka == kb


def order_key(f, a):
    """spec 02 §3.3: negative -> invert all bits, otherwise set the top bit"""
    return (~a) & ((1 << f.bits) - 1) if a & f.sign else a | f.sign


def sum_tree(f, w, xs):
    """spec 02 §3.4: W accumulators from +0, element i into accumulator i mod W, then a pair tree"""
    acc = [0] * w
    for i, x in enumerate(xs):
        acc[i % w] = add_nan(f, acc[i % w], x)
    while len(acc) > 1:
        h = len(acc) // 2
        acc = [add_nan(f, acc[j], acc[j + h]) for j in range(h)]
    return acc[0]


def add_nan(f, a, b):
    """add that keeps a NaN as a quiet NaN bit pattern, for chained sums"""
    r = binary("add", f, a, b)
    return f.qnan if r == "nan" else r


def is_tie(f, op, a, b):
    """is the exact finite result of a op b exactly halfway between two neighbours?"""
    if any(is_nan(f, x) or is_inf(f, x) for x in (a, b)):
        return False
    sa, va = decode(f, a)
    sb, vb = decode(f, b)
    v = abs((-va if sa else va) + (-vb if sb else vb)) if op == "add" else va * vb
    if v == 0:
        return False
    e = max(floor_log2(v), f.emin)
    w = v / Fraction(2) ** (e - (f.p - 1))
    return w.denominator == 2
