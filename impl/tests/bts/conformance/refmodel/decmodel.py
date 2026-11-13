"""Reference model for `decimal` (spec 02 §5.2).

A value is (neg, c, e) for (-1)^neg * c * 10^e. Every result is computed exactly as a ratio of
integers and rounded once, by the rule of §5.2: to p significant digits of the value (trailing
zeros do not count), or for `quantize` to a multiple of 10^e. The integer arithmetic goes through
`bigmodel` (spec 02 §5.1), so the model also shows that §5.2 is built on §5.1. Python integers
appear only at the edges; check.py recomputes every row with the standard library's `decimal`.
"""
import re

import bigmodel as bm

TRAP = bm.TRAP
MODES = ("nearest_even", "nearest_away", "toward_zero", "toward_neg_inf", "toward_pos_inf")
ARITH = ("add", "sub", "mul", "div")
UNARY = ("neg", "abs")
COMPARE = bm.COMPARE


def _mul(a, b):
    return bm.binary("mul", a, b)


def _pow10(n):
    r = 1
    for _ in range(n):
        r = _mul(r, 10)
    return r


def parse(s):
    m = re.fullmatch(r"(-?)(\d+)E(-?\d+)", s)
    return m.group(1) == "-", int(m.group(2)), int(m.group(3))


def text(neg, c, e):
    """canonical row form: trailing zeros of the coefficient moved into the exponent, zero as 0E0"""
    if c == 0:
        return "0E0"
    while bm.binary("rem_trunc", c, 10) == 0:
        c, e = bm.binary("div_trunc", c, 10), e + 1
    return f"{'-' if neg else ''}{c}E{e}"


def _ilog10(n, d):
    """largest t with 10^t <= n/d, for n, d > 0"""
    t = 0
    while bm.compare("ge", n, _mul(d, _pow10(t + 1))):
        t += 1
    while bm.compare("lt", _mul(n, _pow10(-t)) if t < 0 else n, d if t < 0 else _mul(d, _pow10(t))):
        t -= 1
    return t


def _round_ratio(neg, n, d, scale, k, mode):
    """the integer q nearest (by mode) to n * 10^scale / (d * 10^k), for n >= 0, d > 0"""
    if scale >= k:
        n = _mul(n, _pow10(scale - k))
    else:
        d = _mul(d, _pow10(k - scale))
    q, r = bm.binary("div_trunc", n, d), bm.binary("rem_trunc", n, d)
    if r == 0:
        return q
    if mode == "toward_zero":
        up = False
    elif mode == "toward_neg_inf":
        up = neg
    elif mode == "toward_pos_inf":
        up = not neg
    else:
        c = bm.compare("cmp", _mul(r, 2), d)
        up = c > 0 or (c == 0 and (mode == "nearest_away" or bm.binary("rem_trunc", q, 2) == 1))
    return bm.binary("add", q, 1) if up else q


def _round(neg, n, d, scale, p, mode):
    """(-1)^neg * n/d * 10^scale rounded to p significant digits"""
    if n == 0:
        return text(False, 0, 0)
    t = _ilog10(n, d) + scale
    k = t - (p - 1)
    return text(neg, _round_ratio(neg, n, d, scale, k, mode), k)


def arith(op, a, b, p, mode):
    (an, ac, ae), (bn, bc, be) = parse(a), parse(b)
    if op == "div":
        if bc == 0:
            return TRAP
        return _round(an != bn, ac, bc, ae - be, p, mode)
    if op == "mul":
        return _round(an != bn, _mul(ac, bc), 1, ae + be, p, mode)
    if op == "sub":
        bn = not bn
    e = min(ae, be)
    x = bm.binary("mul", -ac if an else ac, _pow10(ae - e))
    y = bm.binary("mul", -bc if bn else bc, _pow10(be - e))
    s = bm.binary("add", x, y)
    return _round(s < 0, bm.unary("abs", s), 1, e, p, mode)


def unary(op, a):
    neg, c, e = parse(a)
    return text((not neg) if op == "neg" else False, c, e)


def quantize(a, e, mode):
    neg, c, ae = parse(a)
    return text(neg, _round_ratio(neg, c, 1, ae, e, mode), e)


def compare(op, a, b):
    (an, ac, ae), (bn, bc, be) = parse(a), parse(b)
    e = min(ae, be)
    x = bm.binary("mul", -ac if an else ac, _pow10(ae - e))
    y = bm.binary("mul", -bc if bn else bc, _pow10(be - e))
    return bm.compare(op, x, y)


def from_integer(v):
    return text(v < 0, bm.unary("abs", v), 0)
