"""Layout reference model: struct, bitfield and built-in type layouts (specs 04 and 06).

A target is described by a small ABI table: pointer size and alignment, address
width (`uaddr`), `usize` width, and the size and alignment of each scalar. The
layout rules are the same on every target; only the table changes.

Rules (06 §2):
  default / ffi : fields in declaration order; each field at the next offset that is a
                  multiple of its alignment; struct alignment = max field alignment
                  (raised by an explicit align attribute); size rounded up to it.
  packed        : no padding, alignment 1 (an explicit align attribute still raises it). A packed
                  struct may not hold Atomic(T), directly or nested (gate 12): translation is rejected.
Built-ins (06 §4): ref/&mut/*T are one pointer; slice and slice_mut are {ptr, usize}; array is N
contiguous elements; Option/Result are {u8 tag, payload} except the guaranteed niche
set (ref, &mut, owned_ptr), which is one pointer; Atomic(T) has the size of T and
alignment max(align T, size T) up to the target's widest atomic size
(align T beyond it), and exists only up to the target's lock-free width; owned_ptr is one pointer for a stateless allocator
and {ptr, ctx} otherwise.
"""

import json
import os
from dataclasses import dataclass, field


@dataclass(frozen=True)
class Target:
    name: str
    clang: tuple            # clang flags for the C ABI oracle, () if none
    ptr: tuple              # (size, align) of a pointer
    addr_bytes: int         # width of uaddr
    usize_bytes: int
    scalars: dict = field(default_factory=dict)   # name -> (size, align)
    atomic_max: int = 8     # widest size Atomic(T) aligns to its size (the target C ABI _Atomic rule)
    atomic_ldst: int = 0    # widest lock-free atomic load/store (report D D4); Atomic(T) exists up to this size
    atomic_rmw: int = 0     # widest lock-free read-modify-write (report D D4)
    masking: bool = False   # widths come from interrupt masking on a declared single-core target (D5);
                            # a masked section covers any size, so this model lists 8 for them
    note: str = ""
    cache_line: int = 0     # spec 09 §1: bytes between falsely shared data; 0 = the target declares none
    simd_align_max: int = 0  # spec 03 §2 (gate 14): cap on simd(T, N) alignment; 0 = no cap


# The target values live in the target descriptions docs/conformance/targets/*.json
# (format bts-target/1, report G G1); this model reads them. ORDER fixes the order of
# the vectors.
ORDER = ["x86_64", "i386", "rv32", "rv64", "cortex-m", "msp430", "avr", "avr-xt", "ppc32", "cheri-rv64", "cheriot"]
TARGET_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "targets")


def load_target(name, path=None):
    with open(path or os.path.join(TARGET_DIR, name + ".json"), encoding="utf-8") as f:
        d = json.load(f)
    return Target(d["name"], tuple(d["oracle"]), (d["ptr_bytes"], d["ptr_align"]), d["addr_bits"] // 8,
                  d["usize_bits"] // 8, {k: tuple(v) for k, v in d["scalars"].items()},
                  d["atomic_max"], d["atomic_ldst"], d["atomic_rmw"], d["masking"], d["note"],
                  d["cache_line"] or 0, d.get("simd_align_max") or 0)


TARGETS = {n: load_target(n) for n in ORDER}

NICHE = ("ref", "mutref", "owned_ptr")    # guaranteed null-pointer optimisation (04 §3, 06 §4.1)


class Unavailable(Exception):
    """The type is optional and the target does not provide it (R1.2)."""


class Rejected(Exception):
    """The declaration is rejected on every target (06 §1, gate 12: no Atomic inside packed)."""


def holds_atomic(t, structs):
    """does type t contain Atomic(...) directly or through a struct field, array or wrapper?"""
    if "Atomic(" in t:
        return True
    names = [w.split(":", 1)[1].rstrip(")") for w in t.replace(",", " ").replace("(", " ").split() if w.startswith("struct:")]
    return any(holds_atomic(ft, structs) for n in names for _, ft in structs[n]["fields"])


def align_up(x, a):
    return (x + a - 1) // a * a


def layout_of(t, tg, structs):
    """(size, align) of a type expression on target tg."""
    if isinstance(t, dict):                       # nested anonymous struct or union
        r = struct_layout(t, tg, structs)
        return r["size"], r["align"]
    if t in tg.scalars:
        return tg.scalars[t]
    if t in ("u128", "i128"):
        raise Unavailable(t)
    if t == "usize" or t == "idiff":
        n = tg.usize_bytes
        return tg.scalars[f"u{8 * n}"]
    if t == "uaddr":
        n = tg.addr_bytes
        return tg.scalars[f"u{8 * n}"]
    if t in ("ptr", "ref", "mutref"):
        return tg.ptr
    if t.startswith("struct:"):
        r = struct_layout(structs[t[7:]], tg, structs)
        return r["size"], r["align"]
    head, args = t.split("(", 1)
    args = _split(args[:-1])
    if head in ("ptr", "ref", "mutref"):
        return tg.ptr
    if head == "array":
        s, a = layout_of(args[0], tg, structs)
        return s * int(args[1]), a
    if head == "simd":                             # 03 §2 (gate 14): N lanes, alignment capped by the target
        s, _ = layout_of(args[0], tg, structs)
        size = s * int(args[1])
        al = 1 << (size - 1).bit_length()
        return size, min(al, tg.simd_align_max) if tg.simd_align_max else al
    if head in ("slice", "slice_mut"):           # same layout; the permission is static (06 §4.3)
        return _record([tg.ptr, layout_of("usize", tg, structs)])
    if head == "Atomic":
        s, a = layout_of(args[0], tg, structs)
        if s not in (1, 2, 4, 8, 16):
            raise ValueError("Atomic(T) needs a power-of-two size (06 §4.7)")
        if s > tg.atomic_ldst:
            raise Unavailable(f"Atomic of {s} bytes exceeds the lock-free width {tg.atomic_ldst} (07 §3.4)")
        return s, (max(a, s) if s <= tg.atomic_max else a)
    if head == "owned_ptr":
        return tg.ptr if args[1] == "static" else _record([tg.ptr, tg.ptr])
    if head in ("Mutex", "RwLock"):                # standard-layout lock structs (07 §4.4)
        words = [layout_of("Atomic(u32)", tg, structs)] * (1 if head == "Mutex" else 2)
        return _record(words + [layout_of(args[0], tg, structs)])
    if head == "Option":
        inner = args[0].split("(", 1)[0]
        if inner in NICHE:                        # null is the None value; the pointer field holds it
            return layout_of(args[0], tg, structs)
        return _record([(1, 1), layout_of(args[0], tg, structs)])
    if head == "Result":
        a = layout_of(args[0], tg, structs)
        b = layout_of(args[1], tg, structs)
        u = (align_up(max(a[0], b[0]), max(a[1], b[1])), max(a[1], b[1]))
        return _record([(1, 1), u])
    raise ValueError(f"unknown type {t}")


def builtin_detail(t, tg, structs):
    """Size, align, field offsets and a niche flag for one built-in type."""
    size, align = layout_of(t, tg, structs)
    head = t.split("(", 1)[0]
    offs, niche = None, False
    if head in ("slice", "slice_mut"):
        offs = {"ptr": 0, "len": align_up(tg.ptr[0], layout_of("usize", tg, structs)[1])}
    elif head in ("Option", "Result"):
        inner = _split(t.split("(", 1)[1][:-1])
        if head == "Option" and inner[0].split("(", 1)[0] in NICHE:
            niche = True
        else:
            pa = max(layout_of(x, tg, structs)[1] for x in inner)
            offs = {"tag": 0, "payload": align_up(1, pa)}
    elif head in ("Mutex", "RwLock"):
        w = layout_of("Atomic(u32)", tg, structs)
        nwords = 1 if head == "Mutex" else 2
        end = 0
        for _ in range(nwords):
            end = align_up(end, w[1]) + w[0]
        d = layout_of(t.split("(", 1)[1][:-1], tg, structs)
        offs = {"state": 0, "data": align_up(end, d[1])}
        if head == "RwLock":
            offs["writer_notify"] = align_up(w[0], w[1])
    elif head == "owned_ptr" and size > tg.ptr[0]:
        offs = {"ptr": 0, "ctx": tg.ptr[0]}
    return {"size": size, "align": align, "offsets": offs, "niche": niche}


def _record(parts):
    off, al = 0, 1
    for s, a in parts:
        off = align_up(off, a) + s
        al = max(al, a)
    return align_up(off, al), al


def _split(s):
    out, depth, cur = [], 0, ""
    for ch in s:
        if ch == "," and depth == 0:
            out.append(cur.strip())
            cur = ""
            continue
        depth += ch == "("
        depth -= ch == ")"
        cur += ch
    out.append(cur.strip())
    return out


def struct_layout(decl, tg, structs):
    """decl = {"fields": [[name, type], ...], "layout": "default"|"ffi"|"packed",
               "align": n or "cache_line" or None, "union": bool, "reorder": bool}.
    reorder (spec 09 §2): fields are placed by alignment descending, declaration order on ties;
    offsets are still reported in declaration order."""
    packed = decl.get("layout") == "packed"
    if packed and any(holds_atomic(ft, structs) for _, ft in decl["fields"]):
        raise Rejected("Atomic(T) inside a packed struct (06 §1)")
    is_union = decl.get("union", False)
    reorder = decl.get("reorder", False)
    if reorder and (packed or is_union or decl.get("layout") == "ffi"):
        raise ValueError("reorder cannot be combined with ffi, packed or union (spec 09 §2)")
    sl = [layout_of(ft, tg, structs) for _, ft in decl["fields"]]
    order = range(len(sl))
    if reorder:
        order = sorted(order, key=lambda i: -sl[i][1])
    off, al, offsets = 0, 1, [0] * len(sl)
    for i in order:
        s, a = sl[i]
        a = 1 if packed else a
        o = 0 if is_union else align_up(off, a)
        offsets[i] = o
        off = max(off, s) if is_union else o + s
        al = max(al, a)
    want = decl.get("align")
    if want == "cache_line":
        want = tg.cache_line      # none: natural alignment, no padding (spec 09 §1)
    if want:
        al = max(al, want)
    size = align_up(off, al)
    return {"size": size, "align": al, "offsets": offsets, "padding": padding(decl, tg, structs, offsets, size)}


def padding(decl, tg, structs, offsets, size):
    """Byte ranges [start, length] not covered by any field (unions: tail only)."""
    covered = [False] * size
    for (_, ft), o in zip(decl["fields"], offsets):
        s, _ = layout_of(ft, tg, structs)
        for i in range(o, o + s):
            covered[i] = True
    out, i = [], 0
    while i < size:
        if covered[i]:
            i += 1
            continue
        j = i
        while j < size and not covered[j]:
            j += 1
        out.append([i, j - i])
        i = j
    return out


def bitfield_pack(unit_bits, order, fields, values):
    """Pack bitfield values into one storage-unit integer (06 §2.4).

    fields = [[name, width] or [name, width, "signed"], ...] in declaration order; order = "lsb"
    puts the first field at bit 0, "msb" puts it at the top bit. A signed field (gate 14) holds
    -2**(w-1) .. 2**(w-1)-1 in two's complement and is sign-extended when read. Returns (unit value,
    bit positions)."""
    if sum(f[1] for f in fields) > unit_bits:
        raise ValueError("bitfields exceed the storage unit")
    pos, word, at = [], 0, 0
    for f, v in zip(fields, values):
        w, signed = f[1], len(f) > 2 and f[2] == "signed"
        lo_v, hi_v = (-(1 << (w - 1)), (1 << (w - 1)) - 1) if signed else (0, (1 << w) - 1)
        if not lo_v <= v <= hi_v:
            raise ValueError("value does not fit its bitfield")
        lo = at if order == "lsb" else unit_bits - at - w
        pos.append(lo)
        word |= (v % (1 << w)) << lo
        at += w
    return word, pos


def unit_bytes(word, unit_bits, endian):
    n = unit_bits // 8
    return list(word.to_bytes(n, "little" if endian == "little" else "big"))
