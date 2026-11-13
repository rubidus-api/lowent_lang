"""Allowed result sets for the Free Choice places of spec 08 §4.1 (FC-03, FC-04, FC-05, FC-07).

A scenario names the rule, what memory holds or which writes race, and what is observed.
`allowed` is the complete set of results a conforming processor may show; a runner passes
when every observation lies in it. Booleans are JSON booleans, integers decimal strings,
chars "U+XXXX", "trap" means execution stops, {"any_bits": n} means every n-bit pattern.
"""
CHAR_MAX = 0x10FFFF


def valid_char(v):
    return v <= 0xD7FF or 0xE000 <= v <= CHAR_MAX


def uchar(v):
    return f"U+{v:04X}"


def materialize(ty, raw, variants=None):
    """08 §4.4 / FC-07: the values a read of bit pattern `raw` as `ty` may produce"""
    if ty == "bool":
        return [raw == 1] if raw in (0, 1) else ["trap", False, True]
    if ty == "char":
        return [uchar(raw)] if valid_char(raw) else ["trap", "U+FFFD"]
    if ty == "tag":                                # tagged-union tag with `variants` alternatives (06 §3.1)
        return [str(raw)] if raw < variants else ["trap"] + [str(k) for k in range(variants)]
    raise ValueError(ty)


def materialize_twice(ty, raw, variants=None):
    """the value is fixed after materialization: two uses of one read agree"""
    return [x if x == "trap" else [x, x] for x in materialize(ty, raw, variants)]


def racy(width):
    """FC-05 (gate 12): per byte any value, fixed within one read (the level of LLVM `freeze`)"""
    return {"any_bits": 8 * width}


def checked_index(width, length):
    """08 §4.6: the idx check and the access see the same read, so an access never goes out of bounds"""
    return [f"elem {v}" for v in range(min(length, 1 << (8 * width)))] + (["trap"] if length < 1 << (8 * width) else [])
