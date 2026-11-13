"""Reference model for `bigint` (spec 02 §5.1).

Values are kept as a sign and a magnitude of 32-bit digits (least significant first), and the
digit arithmetic uses only the fixed-width operations of spec 02 §2.6 (`add_carry`, `sub_borrow`,
`mul_wide` on u32) plus u32 shifts. That is the point of the model: it shows the operations can be
built from the core's carry operations. Python integers appear only at the edges (to read the
operands and write the result); check.py recomputes every row with host integers.
"""
import intmodel as im

U32 = im.TYPES["u32"]
BASE_BITS = 32
TRAP = im.TRAP


# ---- magnitudes: lists of u32 digits, least significant first, no high zero digits ----
def _norm(m):
    while m and m[-1] == 0:
        m.pop()
    return m


def _from_int(a):
    neg, a = a < 0, abs(a)
    m = []
    while a:
        m.append(a & 0xFFFFFFFF)
        a >>= BASE_BITS
    return neg, m


def _to_int(neg, m):
    v = 0
    for d in reversed(m):
        v = (v << BASE_BITS) | d
    return -v if neg and v else v


def _cmp_mag(x, y):
    if len(x) != len(y):
        return -1 if len(x) < len(y) else 1
    for a, b in zip(reversed(x), reversed(y)):
        if a != b:
            return -1 if a < b else 1
    return 0


def _add_mag(x, y):
    out, c = [], 0
    for i in range(max(len(x), len(y))):
        s, c = im.add_carry(U32, x[i] if i < len(x) else 0, y[i] if i < len(y) else 0, c)
        out.append(s)
    if c:
        out.append(c)
    return _norm(out)


def _sub_mag(x, y):
    """x - y for x >= y"""
    out, b = [], 0
    for i in range(len(x)):
        d, b = im.sub_borrow(U32, x[i], y[i] if i < len(y) else 0, b)
        out.append(d)
    assert b == 0
    return _norm(out)


def _mul_mag(x, y):
    out = [0] * (len(x) + len(y))
    for i, a in enumerate(x):
        carry = 0
        for j, b in enumerate(y):
            lo, hi = im.mul_wide(U32, a, b)
            s, c1 = im.add_carry(U32, out[i + j], lo, 0)
            s, c2 = im.add_carry(U32, s, carry, 0)
            out[i + j] = s
            carry, c3 = im.add_carry(U32, hi, c1, c2)
            assert c3 == 0
        k = i + len(y)
        while carry:
            out[k], carry = im.add_carry(U32, out[k], carry, 0)
            k += 1
    return _norm(out)


def _shl_mag(x, k):
    words, bits = divmod(k, BASE_BITS)
    out = [0] * words
    carry = 0
    for d in x:
        out.append(((d << bits) & 0xFFFFFFFF) | carry)
        carry = d >> (BASE_BITS - bits) if bits else 0
    if carry:
        out.append(carry)
    return _norm(out)


def _shr_mag(x, k):
    """(x >> k, whether any 1 bit was shifted out)"""
    words, bits = divmod(k, BASE_BITS)
    lost = any(x[:words]) or (bits and words < len(x) and x[words] & ((1 << bits) - 1) != 0)
    x = x[words:]
    out = []
    for i, d in enumerate(x):
        hi = x[i + 1] if i + 1 < len(x) else 0
        out.append(((d >> bits) | ((hi << (BASE_BITS - bits)) & 0xFFFFFFFF)) if bits else d)
    return _norm(out), bool(lost)


def _divmod_mag(x, y):
    """schoolbook binary long division on magnitudes: (q, r) with x = q*y + r, 0 <= r < y"""
    q, r = [], []
    nbits = len(x) * BASE_BITS
    for i in reversed(range(nbits)):
        r = _shl_mag(r, 1)
        if (x[i // BASE_BITS] >> (i % BASE_BITS)) & 1:
            r = _add_mag(r, [1])
        if _cmp_mag(r, y) >= 0:
            r = _sub_mag(r, y)
            q = _add_mag(q, _shl_mag([1], i))
    return q, r


# ---- signed values: (neg, magnitude) ----
def _add(a, b):
    (an, am), (bn, bm) = a, b
    if an == bn:
        return an, _add_mag(am, bm)
    c = _cmp_mag(am, bm)
    if c == 0:
        return False, []
    return (an, _sub_mag(am, bm)) if c > 0 else (bn, _sub_mag(bm, am))


def _neg(a):
    return (not a[0] and bool(a[1])), a[1]


def _divmod_trunc(a, b):
    q, r = _divmod_mag(a[1], b[1])
    return ((a[0] != b[0]) and bool(q), q), (a[0] and bool(r), r)


ONE = (False, [1])


def _div(conv, a, b):
    q, r = _divmod_trunc(a, b)
    if not r[1]:
        return q, r
    if conv == "floor" and r[0] != b[0]:            # remainder takes the divisor's sign
        q, r = _add(q, _neg(ONE)), _add(r, b)
    elif conv == "euclid" and r[0]:                  # remainder is never negative
        if b[0]:
            q, r = _add(q, ONE), _add(r, _neg(b))
        else:
            q, r = _add(q, _neg(ONE)), _add(r, b)
    return q, r


DIV = {"div_trunc": ("trunc", 0), "rem_trunc": ("trunc", 1), "div_floor": ("floor", 0), "mod_floor": ("floor", 1),
       "div_euclid": ("euclid", 0), "rem_euclid": ("euclid", 1)}
BINARY = ("add", "sub", "mul") + tuple(DIV)
UNARY = ("neg", "abs")
SHIFTS = ("shl", "shr")
COMPARE = ("lt", "le", "gt", "ge", "eq", "ne", "cmp")


def binary(op, a, b):
    x, y = _from_int(a), _from_int(b)
    if op == "add":
        r = _add(x, y)
    elif op == "sub":
        r = _add(x, _neg(y))
    elif op == "mul":
        m = _mul_mag(x[1], y[1])
        r = (x[0] != y[0] and bool(m), m)
    else:
        if not y[1]:
            return TRAP
        conv, which = DIV[op]
        r = _div(conv, x, y)[which]
    return _to_int(*r)


def unary(op, a):
    x = _from_int(a)
    return _to_int(*(_neg(x) if op == "neg" else (False, x[1])))


def shift(op, a, k):
    neg, m = _from_int(a)
    if op == "shl":
        return _to_int(neg, _shl_mag(m, k))
    q, lost = _shr_mag(m, k)
    if neg and lost:                                 # floor: round the magnitude away from zero
        q = _add_mag(q, [1])
    return _to_int(neg, q)


def compare(op, a, b):
    x, y = _from_int(a), _from_int(b)
    d = _add(x, _neg(y))
    c = 0 if not d[1] else (-1 if d[0] else 1)
    return {"lt": c < 0, "le": c <= 0, "gt": c > 0, "ge": c >= 0, "eq": c == 0, "ne": c != 0, "cmp": c}[op]


def narrow(a, type_name, disposition):
    """bigint -> fixed-width integer (02 §1.5)"""
    return im.dispose(im.TYPES[type_name], a, disposition)
