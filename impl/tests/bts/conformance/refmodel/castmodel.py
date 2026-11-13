"""Reference model for numeric conversions (report A, R5.3).

Lossless means value-set inclusion, never bit width (report A §2.1). The shared
core fixes the value of each explicit conversion; which conversions a language
applies implicitly, and how it picks a common type, are language policies.
"""
import math
import struct

from intmodel import TYPES, TRAP, NONE

F32_MANT = 24          # significand bits including the hidden bit
F32_MAX = (2 - 2 ** -23) * 2 ** 127


def f32_value(bits):
    return struct.unpack("<f", struct.pack("<I", bits))[0]


def int_lossless_to_int(s, t):
    return TYPES[t].min <= TYPES[s].min and TYPES[s].max <= TYPES[t].max


def int_lossless_to_f32(s):
    """Every value of s is exactly a binary32: |v| < 2**24, or all values multiples allowing exactness."""
    ty = TYPES[s]
    return max(abs(ty.min), ty.max) <= (1 << F32_MANT)


def lossless(s, t):
    """Value-set inclusion V(s) ⊆ V(t) for the fixed-width integer types and f32/f64."""
    if s == t:
        return True
    if s in TYPES and t in TYPES:
        return int_lossless_to_int(s, t)
    if s in TYPES and t == "f32":
        return int_lossless_to_f32(s)
    if s in TYPES and t == "f64":
        return max(abs(TYPES[s].min), TYPES[s].max) <= (1 << 53)
    if s == "f32" and t == "f64":
        return True
    return False


# ---- implicit conversion and common type: language policies (report A §4) ----
def lowent_implicit(s, t):
    """lowent widening table (canon §6.2.5(4)): uN->uM (N<=M), iN->iM (N<=M), uN->iM (N<M), f32->f64.
    128-bit targets are excluded from automatic widening (§6.2.5(2b))."""
    if s == t:
        return True
    if s in TYPES and t in TYPES:
        if TYPES[t].bits == 128 and TYPES[s].bits != 128:
            return False
        return int_lossless_to_int(s, t)
    return s == "f32" and t == "f64"


def pinax_implicit(s, t):
    return s == t


def lowent_common(s, t):
    """Policy P3: the operand that contains the other, never a third type."""
    if lowent_implicit(s, t):
        return t
    if lowent_implicit(t, s):
        return s
    return None


def pinax_common(s, t):
    """Policy P4: identical types only."""
    return s if s == t else None


def lub(s, t, available):
    """Least upper bound among `available` integer types (may be a third type, may not exist)."""
    ups = [u for u in available if int_lossless_to_int(s, u) and int_lossless_to_int(t, u)]
    least = [u for u in ups if all(int_lossless_to_int(u, v) for v in ups)]
    return least[0] if least else None


# ---- explicit conversions: shared value semantics ----
def narrow(s, t, v, disposition):
    ty = TYPES[t]
    if ty.contains(v):
        return v
    return {"wrap": ty.wrap(v), "sat": ty.sat(v), "chk": NONE}.get(disposition, TRAP)


def f32_to_int(bits, t, disposition="trap"):
    """Round toward zero; NaN and out-of-range values follow the disposition (lowent cast: trap)."""
    x = f32_value(bits)
    if math.isnan(x):
        return {"chk": NONE, "sat": 0}.get(disposition, TRAP)
    if math.isinf(x):
        ty = TYPES[t]
        if disposition == "sat":
            return ty.max if x > 0 else ty.min
        return NONE if disposition == "chk" else TRAP
    v = math.trunc(x)
    ty = TYPES[t]
    if ty.contains(v):
        return v
    if disposition == "sat":
        return ty.sat(v)
    return NONE if disposition == "chk" else TRAP


def int_to_f32_bits(v):
    """Integer to binary32, round to nearest, ties to even. Computed with exact rationals,
    so there is no double rounding through binary64."""
    from fractions import Fraction
    x = Fraction(v)
    if x == 0:
        return 0
    sign = 0x80000000 if x < 0 else 0
    x = abs(x)
    e = x.numerator.bit_length() - x.denominator.bit_length()
    if Fraction(2) ** e > x:
        e -= 1
    # x in [2^e, 2^(e+1)); keep 24 significant bits
    scaled = x / (Fraction(2) ** (e - 23))
    q, r = divmod(scaled.numerator, scaled.denominator)
    half = Fraction(r, scaled.denominator)
    if half > Fraction(1, 2) or (half == Fraction(1, 2) and q & 1):
        q += 1
    if q == 1 << 24:
        q >>= 1
        e += 1
    if e > 127:
        return sign | 0x7F800000
    return sign | ((e + 127) << 23) | (q & 0x7FFFFF)
