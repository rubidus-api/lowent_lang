"""Reference model of fixed-width SIMD (spec 03 §3) and the variable-length vectors of the
variable-SIMD annex §3. Lane operations are the scalar operations of intmodel.py applied per lane.
"""
import intmodel as im


def lanes_op(op, tn, disp, a, b=None):
    t = im.TYPES[tn]
    if op in ("and", "or", "xor"):
        f = {"and": lambda x, y: x & y, "or": lambda x, y: x | y, "xor": lambda x, y: x ^ y}[op]
        return [t.wrap(f(x & ((1 << t.bits) - 1), y & ((1 << t.bits) - 1))) for x, y in zip(a, b)]
    if op in ("shl", "shr"):           # 03 §3.3: the count is always k mod (lane bits)
        return [im.shift(op, t, x, k % t.bits, "wrap") for x, k in zip(a, b)]
    if op in ("eq", "lt"):             # lane masks: all ones when true
        ones = -1 if t.signed else t.max
        return [ones if (x == y if op == "eq" else x < y) else 0 for x, y in zip(a, b)]
    return [im.binary(op, t, x, y, disp) for x, y in zip(a, b)]


def to_int(tn, lanes):
    """03 §3.2: bit reinterpretation as one integer, lane 0 in the lowest bits"""
    bits = im.TYPES[tn].bits
    return sum((x & ((1 << bits) - 1)) << (bits * i) for i, x in enumerate(lanes))


def reduce(op, tn, lanes):
    t = im.TYPES[tn]
    if op == "sum":
        return t.wrap(sum(lanes))
    return min(lanes) if op == "min" else max(lanes)


def lane_lookup16(table, idx):
    """03 §3.6: low 4 bits select; any high bit set gives 0"""
    return [0 if i & 0xF0 else table[i & 0x0F] for i in idx]


def load_masked(s, n, lanes):
    """03 §3.5: the first min(n, N, len(s)) elements, the other lanes 0"""
    m = min(n, lanes, len(s))
    return list(s[:m]) + [0] * (lanes - m)


def store_masked(dst, v, n):
    out = list(dst)
    for i in range(min(n, len(v), len(dst))):
        out[i] = v[i]
    return out


def load_full(s, lanes, disp):
    if len(s) < lanes:
        return "trap" if disp == "trap" else "none"
    return list(s[:lanes])


# ---- variable-length vectors (variable-SIMD annex §3) ------------------------------------

def svec_load(s, i, n, lanes):
    m = min(n, lanes)
    if i + m > len(s):
        return "trap"                  # the idx check over s[i..i+m]
    return list(s[i:i + m]) + [0] * (lanes - m)


KERNELS = {}


def kernel(name):
    def reg(f):
        KERNELS[name] = f
        return f
    return reg


@kernel("sum_u32_wrap")
def k_sum(lanes, s):
    """wrapping sum of a u32 slice with a vector accumulator, then a lane reduction"""
    t = im.TYPES["u32"]
    acc, i = [0] * lanes, 0
    while i < len(s):
        n = min(lanes, len(s) - i)
        v = svec_load(s, i, n, lanes)
        acc = [t.wrap(x + y) for x, y in zip(acc, v)]
        i += n
    return [str(reduce("sum", "u32", acc))]


@kernel("add_sat_u8")
def k_sat(lanes, s):
    """element-wise saturating add of the two halves of a u8 slice"""
    h = len(s) // 2
    a, b, out, i = s[:h], s[h:2 * h], [], 0
    while i < h:
        n = min(lanes, h - i)
        r = lanes_op("add", "u8", "sat", svec_load(a, i, n, lanes), svec_load(b, i, n, lanes))
        out += r[:n]
        i += n
    return [str(x) for x in out]


@kernel("count_eq_u8")
def k_count(lanes, s):
    """how many bytes after the first equal the first byte (lane masks, then a sum of 1 bits)"""
    c, rest, total, i = s[0], s[1:], 0, 0
    while i < len(rest):
        n = min(lanes, len(rest) - i)
        m = lanes_op("eq", "u8", None, svec_load(rest, i, n, lanes), [c] * lanes)
        total += sum(1 for x in m[:n] if x)
        i += n
    return [str(total)]


@kernel("max_i16")
def k_max(lanes, s):
    """maximum of an i16 slice; inactive lanes start at the first element"""
    acc, i = [s[0]] * lanes, 0
    while i < len(s):
        n = min(lanes, len(s) - i)
        v = svec_load(s, i, n, lanes)
        acc = [max(x, y) if j < n else x for j, (x, y) in enumerate(zip(acc, v))]
        i += n
    return [str(reduce("max", "i16", acc))]


KERNEL_TYPES = {"sum_u32_wrap": "u32", "add_sat_u8": "u8", "count_eq_u8": "u8", "max_i16": "i16"}
