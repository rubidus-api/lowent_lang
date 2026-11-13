"""Reference model of the fixed-point Q formats (spec 02 §4), canonical byte encodings (02 §6) and
the checks a build mode may remove (08 §1.2).

Q formats: q_s(m, n) is a signed two's-complement storage integer of 1+m+n bits with n fraction bits,
q_u(m, n) an unsigned one of m+n bits; the value of storage k is k / 2**n. add and sub act on the
storage integers; mul takes the exact product of the storage integers, drops n bits with the chosen
rounding, then applies the disposition. The default disposition is sat and the default rounding is
nearest, ties to even. Gate 13 adds the rounding nearest_pos_inf (ties toward +inf, the DSP `mult_r`
rule) and the conversions between Q formats, integers and f32.
"""
import struct

import intmodel as im

ROUNDINGS = ("nearest_even", "toward_zero", "toward_neg_inf", "nearest_pos_inf")


def q_type(name):
    """'q_s(3,4)' -> (storage IntType, n)"""
    signed = name.startswith("q_s(")
    m, n = (int(x) for x in name[4:-1].split(","))
    bits = (1 + m + n) if signed else (m + n)
    if bits not in (8, 16, 32):
        raise ValueError(f"{name}: storage width {bits} is not 8, 16 or 32 (02 §4)")
    return im.TYPES[("i" if signed else "u") + str(bits)], n


def shift_round(p, n, rounding):
    """p / 2**n rounded to an integer"""
    q, r = p >> n, p & ((1 << n) - 1)               # floor quotient and its non-negative remainder
    if rounding == "toward_neg_inf":
        return q
    if rounding == "toward_zero":
        return q + 1 if p < 0 and r else q
    half = 1 << (n - 1)
    if rounding == "nearest_pos_inf":                 # add half, then floor: (p + 2**(n-1)) >> n
        return q + 1 if r >= half else q
    return q + 1 if r > half or (r == half and q & 1) else q


def q_op(op, name, a, b, disposition, rounding="nearest_even"):
    t, n = q_type(name)
    if op == "add":
        exact = a + b
    elif op == "sub":
        exact = a - b
    elif op == "mul":
        exact = shift_round(a * b, n, rounding)
    else:
        raise ValueError(op)
    return im.dispose(t, exact, disposition)


# ---- conversions (02 §4, gate 13) ---------------------------------------------------------

def from_int(name, v, disposition="sat"):
    """integer v -> storage v * 2**n, then the disposition"""
    t, n = q_type(name)
    return im.dispose(t, v << n, disposition)


def to_int(name, k, to, disposition="sat"):
    """storage k -> k / 2**n rounded toward zero (as f32 -> integer, 02 §1.5), then the disposition into `to`"""
    _, n = q_type(name)
    v = -((-k) >> n) if k < 0 else k >> n
    return im.dispose(im.TYPES[to], v, disposition)


def convert(name, k, to, rounding="nearest_even", disposition="sat"):
    """Q -> Q: the value k / 2**n1 as storage of `to` (exact when n2 >= n1, else rounded), then the disposition"""
    _, n1 = q_type(name)
    t2, n2 = q_type(to)
    v = k << (n2 - n1) if n2 >= n1 else shift_round(k, n1 - n2, rounding)
    return im.dispose(t2, v, disposition)


def from_f32(name, bits, disposition="sat"):
    """f32 -> Q: the value times 2**n rounded toward zero; NaN, infinities and out-of-range values
    follow the disposition exactly as f32 -> integer does (02 §1.5)"""
    import math
    import castmodel as cm
    if disposition == "wrap":
        raise ValueError("f32 -> Q has no wrap disposition (02 §1.5, §4)")
    t, n = q_type(name)
    x = cm.f32_value(bits)
    if math.isnan(x):
        return {"chk": im.NONE, "sat": 0}.get(disposition, im.TRAP)
    if math.isinf(x):
        if disposition == "sat":
            return t.max if x > 0 else t.min
        return im.NONE if disposition == "chk" else im.TRAP
    return im.dispose(t, math.trunc(x * (1 << n)), disposition)


def to_f32(name, k):
    """Q -> f32: k / 2**n rounded to nearest, ties to even (exact rationals, no double rounding)"""
    from fractions import Fraction
    import castmodel as cm
    _, n = q_type(name)
    return cm.int_to_f32_bits(Fraction(k, 1 << n))


# ---- canonical encoding (02 §6) -----------------------------------------------------------

def encode(type_name, value, width=None):
    """little-endian canonical bytes as hex; value is an int, or the bit pattern for f32/f64"""
    if type_name == "f32":
        if value & 0x7FFFFFFF > 0x7F800000:
            value = 0x7FC00000
        elif value == 0x80000000:
            value = 0
        return struct.pack("<I", value).hex()
    if type_name == "f64":
        if value & 0x7FFFFFFFFFFFFFFF > 0x7FF0000000000000:
            value = 0x7FF8000000000000
        elif value == 0x8000000000000000:
            value = 0
        return struct.pack("<Q", value).hex()
    if type_name in ("usize", "idiff"):
        nbytes = max(8, width // 8)
    elif type_name.startswith("q_"):
        nbytes = q_type(type_name)[0].bits // 8
    else:
        nbytes = im.TYPES[type_name].bits // 8
    return (value % (1 << (8 * nbytes))).to_bytes(nbytes, "little").hex()


def encode_composite(type_name, value):
    """02 §6 (gate 14): struct = the fields' encodings in declaration order, no padding; bool one byte
    0/1; char as u32; Option/Result = a u8 tag (0 = None/ok, 1 = Some/err), then the chosen payload"""
    if type_name == "bool":
        return "01" if value else "00"
    if type_name == "char":
        return encode("u32", int(value[2:], 16))
    if type_name.startswith("struct{"):
        return "".join(encode_composite(t, v) for t, v in zip(type_name[7:-1].split(","), value))
    if type_name.startswith("Option("):
        return "00" if value is None else "01" + encode_composite(type_name[7:-1], value)
    if type_name.startswith("Result("):
        ok_t, err_t = type_name[7:-1].split(",")
        return ("00" + encode_composite(ok_t, value["ok"])) if "ok" in value else ("01" + encode_composite(err_t, value["err"]))
    return encode(type_name, int(value))


# ---- build modes (08 §1.2) -----------------------------------------------------------------

REMOVABLE = ("ovf", "shc", "nar")


def checked_and_erased(check, op, tn, args):
    """(result with the check kept, result in a build mode that removes the check)"""
    t = im.TYPES.get(tn)
    if check == "ovf":
        exact = im.EXACT_BINARY[op](*args) if len(args) == 2 else (-args[0] if op == "neg" else abs(args[0]))
        return im.dispose(t, exact, "trap"), t.wrap(exact)
    if check == "shc":
        return im.shift(op, t, args[0], args[1], "trap"), im.shift(op, t, args[0], args[1], "wrap")
    if check == "nar":
        return (args[0] if t.contains(args[0]) else im.TRAP), t.wrap(args[0])
    if check == "div":
        r = im.binary("div_trunc", t, args[0], args[1], "trap")
        return r, r
    if check in ("idx", "tag"):                       # index < length, tag < number of variants
        r = "ok" if 0 <= args[0] < args[1] else im.TRAP
        return r, r
    if check == "fcv":
        import castmodel as cm
        r = cm.f32_to_int(args[0], tn, "trap")
        return r, r
    raise ValueError(check)
