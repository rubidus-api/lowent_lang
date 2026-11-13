"""A small judge for the shared program notation of programs.json (gate 8, question 7).

It decides accept/reject for the translation-time rules that the examples exercise:
  secret      08 §1.4 rules 1-7, 04 §2.2 (no secret pointers)
  provenance  04 §2.1 (no integer-to-pointer operation, device_ptr only in unsafe), 08 §4.3 T1 (raw deref in unsafe)
  ownership   07 §1.1-1.2 (use after move, linear values consumed exactly once, completion required)
  borrow      07 §2 (one &mut or many ref, no move or write while borrowed, no reference outliving its value);
              a borrow is live up to its last use on any path, a later turn of a loop included, and a reference
              copied from it carries it (gate 15); using a &mut through `deref` or passing it to a &mut
              parameter reborrows it, it does not move it (07 §1.1, gate 15)
  slice       06 §4.3 (writes and SIMD stores need slice_mut; no write view from a read view)
  zero size   04 §2.1 (no diff on pointers to zero-size elements)
  arena       05 §3 (reset only while no block from that arena is live; a block lives until it is moved)
  space       AVR annex §2.2 (program-space references), CHERI annex §4 (no pointer fields in packed structs)
  packed      06 §1 (gate 12: no reference to a field of a packed struct whose type is aligned above 1 on the
              target, outside unsafe; value reads and writes stay allowed)
  size        04 §3 (an object is at most the largest idiff of the target, gate 13)
  constants   02 §2.3 (a constant expression that overflows with a trap disposition is rejected), 08 §2
              (gate 14: not in the branch a translation-time constant condition does not choose)
It is not a type checker for a whole language: it covers straight-line code with `if` and `while`
bodies, which is what the examples use. Each example also carries the verdict its author expects;
gen.py fails if the judge disagrees.

Program form
  fn      {"params": [[name, type]...], "body": [stmt...], "ret": type?}
  stmt    ["let", name, type, expr] | ["set", place, expr] | ["if", expr, [stmt], [stmt]]
          | ["while", expr, [stmt]] | ["do", expr] | ["return", expr] | ["unsafe", [stmt]]
          | ["store", ["var", slice], expr, expr]
  expr    ["var", name] | ["lit", value, type] | ["op", name, disposition|null, expr...]
          | ["index", expr, expr] | ["field", expr, name] | ["ref", place] | ["mutref", place]
          | ["deref", expr] | ["call", fname, expr...] | ["declassify", expr] | ["cast", expr, type]
          | ["addr", expr] | ["with_addr", expr, expr] | ["offset", expr, expr] | ["device_ptr", space, expr]
          | ["diff", expr, expr] | ["slice", place] | ["slice_mut", place] | ["query", "usize_bits"]
  place   name or "name.field"
Types are strings: "u32", "secret u32", "*u8", "ref u8", "mutref u8", "ref @program u8",
"mutref @program u8", "*u8 @program", "File" (needs completion), "owned_ptr(u8,static)",
"slice(u8)", "slice_mut(u8)", "array(u8,4)", "*array(u8,0)", "simd(u8,16)", "Arena", "struct S", "packed struct P".
"""
import intmodel as im

COPY_PREFIX = ("u", "i", "bool", "char", "*", "ref ", "f32", "f64", "usize", "idiff", "uaddr", "array(u8", "slice(")
LINEAR = {"File"}                          # completion needed (07 §1.2)
EXTERN = {                                 # name: ([param types], result type)
    "open": ([], "File"), "close": (["File"], "u32"), "use_ref": (["ref u8"], "u32"), "use_mut": (["mutref u8"], "u32"),
    "send": (["u32"], "u32"), "send_secret": (["secret u32"], "u32"), "consume_box": (["owned_ptr(u8,static)"], "u32"),
    "new_box": ([], "owned_ptr(u8,static)"), "read_flash": (["ref @program u8"], "u8"), "use_u8": (["u8"], "u32"),
    "store_masked": (["slice_mut(u8)", "simd(u8,16)", "usize"], "u32"), "sum": (["slice(u8)"], "u32"),
    "arena_alloc": (["ref Arena"], "owned_ptr(u8,arena)"), "arena_reset": (["mutref Arena"], "u32"),
    "free_block": (["owned_ptr(u8,arena)"], "u32"),
}
BORROW_EXPRS = {"ref": "ref", "mutref": "mut", "slice": "ref", "slice_mut": "mut"}
VT_OPS = {"div_trunc", "rem_trunc", "div_floor", "mod_floor", "div_euclid", "rem_euclid"}
SHIFTS = {"shl", "shr", "rotl", "rotr"}


class Reject(Exception):
    def __init__(self, rule, msg):
        super().__init__(f"{rule}: {msg}")
        self.rule = rule


def secret(t):
    return t.startswith("secret ")


def base(t):
    return t[len("secret "):] if secret(t) else t


def is_copy(t):
    b = base(t)
    if b in LINEAR or b.startswith("owned_ptr") or b.startswith("mutref"):
        return False
    return b.startswith(COPY_PREFIX) or b in ("struct S",)


def split_args(inner):
    """top-level comma split of a type's argument list"""
    out, depth, cur = [], 0, ""
    for ch in inner:
        if ch == "," and depth == 0:
            out.append(cur)
            cur = ""
            continue
        depth += (ch == "(") - (ch == ")")
        cur += ch
    return out + [cur]


def zero_size(t):
    """06 §4.4: array(T, 0) and arrays of zero-size elements have size 0"""
    if t.startswith("array(") and t.endswith(")"):
        elem, n = split_args(t[len("array("):-1])
        return int(n) == 0 or zero_size(elem)
    return False


def elem_of(t):
    """element type of array(T,N), slice(T), slice_mut(T)"""
    return split_args(t[t.index("(") + 1:-1])[0]


def object_size_limit(t, target):
    """04 §3, 05 §1 (gate 13): an object's size is at most the largest idiff of the target"""
    import re
    import layoutmodel as lm
    if target is None or target not in lm.TARGETS:
        return
    tg = lm.TARGETS[target]
    for m in re.finditer(r"array\((\w+),(\d+)\)", t):
        size = lm.layout_of(m.group(0), tg, {})[0]
        if size > (1 << (8 * tg.usize_bytes - 1)) - 1:
            raise Reject("04 §3", f"{m.group(0)} is larger than the largest idiff on {target}")


def check_type(t, target=None):
    object_size_limit(t, target)
    b = base(t)
    if secret(t) and (b.startswith(("*", "ref ", "mutref ")) or b == "uaddr"):
        raise Reject("04 §2.2", f"secret on an address or pointer type: {t}")
    if b.startswith("mutref @program"):
        raise Reject("AVR §2.2", "no &mut in program space")
    if b.startswith("*") and "@program" in b:
        raise Reject("AVR §2.2", "no raw pointer in program space")


class Judge:
    def __init__(self, fn, target=None):
        self.fn, self.target = fn, target
        self.env = {}          # name -> type
        self.state = {}        # name -> "live" | "moved" | "consumed"
        self.borrows = []      # (ref name, kind "ref"|"mut", place, last use index)
        self.uses = {}         # ref name -> last statement index it is used at (precomputed)
        self.unsafe = 0
        self.dead = 0          # depth inside branches a constant condition does not choose (08 §2, gate 14)
        self.pc = 0

    # ---- helpers -------------------------------------------------------------
    def fail(self, rule, msg):
        raise Reject(rule, msg)

    def place_root(self, place):
        return place.split(".")[0]

    def overlap(self, p, q):
        return p == q or p.startswith(q + ".") or q.startswith(p + ".")

    def packed_field_ref(self, place, t):
        """06 §1 (gate 12): a reference to an under-aligned field of a packed struct is rejected in safe code"""
        if "." not in place or self.unsafe or not base(t).startswith(("struct ", "packed struct ")):
            return
        import layoutmodel as lm
        sname, fname = base(t).rsplit(" ", 1)[1], place.split(".", 1)[1]
        s = next((x for x in self.fn.get("structs", []) if x["name"] == sname), None)
        if not s or not s.get("packed"):
            return
        ft = dict(s["fields"])[fname]
        tg = lm.TARGETS[self.target or "x86_64"]
        if lm.layout_of(ft, tg, {})[1] > 1:
            self.fail("06 §1", f"reference to the {ft} field {place} of a packed struct (alignment above 1 on {tg.name})")

    def live_borrows(self):
        return [b for b in self.borrows if b[3] >= self.pc and self.state.get(b[0]) == "live"]

    def touch(self, place, how):
        """a direct use of a place: how in read, write, move"""
        for r, kind, p, _ in self.live_borrows():
            if r == place or not self.overlap(p, place):
                continue
            if kind == "mut" or how in ("write", "move"):
                self.fail("07 §2", f"{how} of {place} while borrowed by {r}")

    def reborrow(self, e):
        """07 §1.1 (gate 15): a &mut or write view used through deref or passed to a parameter of the same kind is
        reborrowed for that use; it stays usable afterwards. Returns its type, or None when e is not such a use."""
        if e[0] != "var" or e[1] not in self.env:
            return None
        t = self.env[e[1]]
        if not base(t).startswith(("mutref ", "slice_mut(")):
            return None
        if self.state[e[1]] != "live":
            self.fail("07 §1.1", f"use of {e[1]} after it was {self.state[e[1]]}")
        self.touch(e[1], "read")
        return t

    # ---- expressions -------------------------------------------------------------
    def expr(self, e):
        """returns (type, secret?)"""
        k = e[0]
        if k == "var":
            n = e[1]
            if n not in self.env:
                self.fail("scope", f"unknown {n}")
            if self.state[n] != "live":
                self.fail("07 §1.1", f"use of {n} after it was {self.state[n]}")
            t = self.env[n]
            if not is_copy(t):
                self.touch(n, "move")
                self.state[n] = "consumed" if base(t) in LINEAR else "moved"
            else:
                self.touch(n, "read")
            return t
        if k == "lit":
            return e[2]
        if k == "query":
            return "u32"
        if k == "op":
            name, disp, args = e[1], e[2], e[3:]
            ts = [self.expr(a) for a in args]
            self.constant_overflow(e, disp)
            sec = any(secret(t) for t in ts)
            if name in SHIFTS and secret(ts[1]):
                self.fail("08 §1.4 (7)", "secret shift count")
            if name in VT_OPS:      # division fails on a zero divisor, and on MIN / -1 for signed types
                dep = secret(ts[1]) or (secret(ts[0]) and base(ts[0]).startswith("i")
                                        and not (args[1][0] == "lit" and args[1][1] != "-1"))
            else:
                dep = sec
            if disp in ("trap", "chk") and dep and name not in SHIFTS:
                self.fail("08 §1.4 (4)", f"{disp} disposition on a secret operand of {name}")
            if sec and (name in VT_OPS or im.timing(name, disp) == "vt"):
                self.fail("08 §1.4 (3)", f"secret operand of vt operation {name}")
            if name in ("lt", "eq", "le", "ne"):
                r = "bool"
            else:
                r = base(ts[0])
            if any(base(t).startswith("*") for t in ts) and name in ("lt", "le"):
                pass
            return ("secret " + r) if sec else r
        if k == "index":
            tb = self.expr(e[1])
            ti = self.expr(e[2])
            if secret(ti):
                self.fail("08 §1.4 (2)", "secret index")
            inner = base(tb)
            elem = inner[inner.index("(") + 1:inner.index(",") if "," in inner else inner.index(")")]
            return ("secret " + elem) if secret(tb) else elem
        if k == "field":
            t = self.expr(e[1])
            return "u32"
        if k in ("slice", "slice_mut"):
            place = e[1]
            root = self.place_root(place)
            if self.state.get(root) != "live":
                self.fail("07 §1.1", f"borrow of {root} after it was {self.state.get(root)}")
            t = base(self.env[root])
            if k == "slice_mut" and t.startswith("slice("):
                self.fail("06 §4.3", f"write view from the read view {root}")
            self.touch(place, "write" if k == "slice_mut" else "read")
            return f"{k}({elem_of(t)})"
        if k == "diff":
            t = base(self.expr(e[1]))
            self.expr(e[2])
            if zero_size(t[1:]):
                self.fail("04 §2.1", f"diff on pointers to the zero-size {t[1:]}")
            return "idiff"
        if k in ("ref", "mutref"):
            place = e[1]
            root = self.place_root(place)
            if self.state.get(root) != "live":
                self.fail("07 §1.1", f"borrow of {root} after it was {self.state.get(root)}")
            self.touch(place, "write" if k == "mutref" else "read")
            t = self.env[root]
            self.packed_field_ref(place, t)
            if "@program" in t:
                return ("ref " if k == "ref" else "mutref ") + t.replace("ref @program ", "@program ")
            return ("ref " if k == "ref" else "mutref ") + base(t)
        if k == "deref":
            t = self.reborrow(e[1]) or self.expr(e[1])
            if base(t).startswith("*") and not self.unsafe:
                self.fail("08 §4.3 T1", "raw pointer dereference outside unsafe")
            return base(t).split(" ", 1)[1] if " " in base(t) else base(t)[1:]
        if k == "call":
            fname, args = e[1], e[2:]
            params, ret = EXTERN[fname]
            if fname == "arena_reset":
                for r, _, p, _ in self.live_borrows():
                    if self.overlap(p, args[0][1]) and r != args[0][1]:
                        self.fail("05 §3", f"reset of {p} while block {r} from it is live")
            for a, pt in zip(args, params):
                ta = (pt.startswith(("mutref ", "slice_mut(")) and self.reborrow(a)) or self.expr(a)
                if pt.startswith("slice_mut(") and base(ta).startswith("slice("):
                    self.fail("06 §4.3", f"read view passed where {fname} needs a write view")
                if secret(ta) and not secret(pt) and not self.unsafe:
                    self.fail("08 §1.4 (6)", f"secret argument to {fname} outside unsafe/declassify")
                if base(ta).startswith("ref @program") != pt.startswith("ref @program"):
                    self.fail("AVR §2.2", f"address-space mismatch passing {ta} as {pt}")
            return ret
        if k == "declassify":
            if not self.unsafe:
                self.fail("08 §4.3", "declassify outside unsafe")
            return base(self.expr(e[1]))
        if k == "cast":
            t = self.expr(e[1])
            to = e[2]
            if to.startswith("*") and not base(t).startswith("*"):
                self.fail("04 §2.1", "no operation makes a pointer from an integer")
            if secret(t) and not secret(to):
                self.fail("08 §1.4 (5)", "secret value cast to a public type")
            if base(t).startswith("ref @program") and not to.startswith("ref @program"):
                self.fail("AVR §2.2", "program-space reference converted to another space")
            return to
        if k == "addr":
            self.expr(e[1])
            return "uaddr"
        if k in ("with_addr", "offset"):
            t = self.expr(e[1])
            ta = self.expr(e[2])
            if secret(ta):
                self.fail("08 §1.4 (2)", "secret pointer offset or address")
            return t
        if k == "device_ptr":
            self.expr(e[2])
            if not self.unsafe:
                self.fail("04 §2.1", "device_ptr outside unsafe")
            return "*u8"
        raise ValueError(k)

    # ---- statements -------------------------------------------------------------
    def assignable(self, dst, src):
        if secret(src) and not secret(dst):
            self.fail("08 §1.4 (5)", f"secret value into public {dst}")
        if base(src).startswith("ref @program") != base(dst).startswith("ref @program"):
            self.fail("AVR §2.2", "program-space reference into another space")

    def block(self, body):
        for st in body:
            self.stmt(st)

    def stmt(self, st):
        self.pc = self.counter
        self.counter += 1
        k = st[0]
        if k == "let":
            name, t, e = st[1], st[2], st[3]
            check_type(t, self.target)
            if "@program" in t and not t.startswith("ref @program") and e[0] != "lit":
                self.fail("AVR §2.2", "only translation-time constants live in program space")
            te = self.expr(e)
            self.assignable(t, te)
            if e[0] in BORROW_EXPRS:
                self.borrows.append((name, BORROW_EXPRS[e[0]], e[1], self.uses.get(name, self.pc)))
            if e[0] == "var":           # a reference copied or moved into name carries the source's borrows (gate 15)
                for r, kind, place, _ in list(self.borrows):
                    if r == e[1]:
                        self.borrows.append((name, kind, place, self.uses.get(name, self.pc)))
            if e[0] == "call" and te.startswith("owned_ptr(") and any(a[0] == "ref" for a in e[2:]):
                for a in e[2:]:        # a block borrows its arena until the block is moved (dropped at scope end)
                    if a[0] == "ref":
                        self.borrows.append((name, "ref", a[1], float("inf")))
            self.env[name], self.state[name] = t, "live"
        elif k == "set":
            place, e = st[1], st[2]
            te = self.expr(e)
            root = self.place_root(place)
            if base(self.env[root]) in LINEAR and self.state[root] == "live":
                self.fail("07 §1.2", f"{root} overwritten without completion")
            self.touch(place, "write")
            self.assignable(self.env[root], te)
            self.state[root] = "live"
        elif k == "if":
            tc = self.expr(st[1])
            if secret(tc):
                self.fail("08 §1.4 (1)", "secret branch condition")
            c = self.const(st[1])                    # a translation-time constant chooses one branch (08 §2)
            before = dict(self.state)
            self.dead += c is not None and not c
            self.block(st[2])
            self.dead -= c is not None and not c
            after_then = dict(self.state)
            self.state = before
            self.dead += c is not None and bool(c)
            self.block(st[3])
            self.dead -= c is not None and bool(c)
            for n in self.state:          # a value moved in either branch is moved afterwards
                if after_then.get(n) != "live" or self.state[n] != "live":
                    if base(self.env[n]) in LINEAR and (after_then.get(n) == "live") != (self.state[n] == "live"):
                        self.fail("07 §1.1", f"linear {n} consumed on only one branch")
                    self.state[n] = after_then.get(n) if after_then.get(n) != "live" else self.state[n]
        elif k == "while":
            tc = self.expr(st[1])
            if secret(tc):
                self.fail("08 §1.4 (1)", "secret loop condition")
            before = dict(self.state)
            self.block(st[2])
            for n, s in before.items():
                if s == "live" and self.state.get(n) != "live":
                    self.fail("07 §1.1", f"{n} moved inside a loop")
        elif k == "do":
            self.expr(st[1])
        elif k == "store":
            n = st[1][1]
            if self.state.get(n) != "live":
                self.fail("07 §1.1", f"use of {n} after it was {self.state.get(n)}")
            if not base(self.env[n]).startswith("slice_mut("):
                self.fail("06 §4.3", f"store through the read view {n}")
            self.touch(n, "write")
            for x in st[2:]:
                self.expr(x)
        elif k == "return":
            t = self.expr(st[1])
            e = st[1]
            if e[0] in ("ref", "mutref") and self.place_root(e[1]) in self.locals:
                self.fail("07 §2", "reference to a local value outlives it")
            if e[0] == "var" and any(b[0] == e[1] and self.place_root(b[2]) in self.locals for b in self.borrows):
                self.fail("07 §2", "reference to a local value outlives it")
            if self.fn.get("ret"):
                self.assignable(self.fn["ret"], t)
        elif k == "unsafe":
            self.unsafe += 1
            self.block(st[1])
            self.unsafe -= 1
        else:
            raise ValueError(k)

    # ---- translation-time constants (02 §2.3, 08 §2) ------------------------------
    def const(self, e):
        """the value of a constant expression (literals, target queries, + - * < <= ==), else None"""
        if e[0] == "lit":
            try:
                return int(e[1])
            except ValueError:
                return None
        if e[0] == "query" and e[1] == "usize_bits":
            import layoutmodel as lm
            return 8 * lm.TARGETS[self.target].usize_bytes if self.target in lm.TARGETS else None
        if e[0] == "op" and e[1] in ("add", "sub", "mul", "lt", "le", "eq"):
            v = [self.const(a) for a in e[3:]]
            if None in v:
                return None
            a, b = v
            return {"add": a + b, "sub": a - b, "mul": a * b, "lt": a < b, "le": a <= b, "eq": a == b}[e[1]]
        return None

    def constant_overflow(self, e, disp):
        """02 §2.3: a constant expression whose exact value leaves its type under a trap disposition is
        rejected, unless it sits in a branch a constant condition does not choose (gate 14)"""
        if self.dead or disp not in (None, "trap") or e[1] not in ("add", "sub", "mul"):
            return
        v, ty = self.const(e), next((a[2] for a in e[3:] if a[0] == "lit"), None)
        if v is None or ty is None:
            return
        if ty == "usize":
            import layoutmodel as lm
            if self.target not in lm.TARGETS:
                return
            lo, hi = 0, (1 << (8 * lm.TARGETS[self.target].usize_bytes)) - 1
        elif ty in im.TYPES:
            lo, hi = im.TYPES[ty].min, im.TYPES[ty].max
        else:
            return
        if not lo <= v <= hi:
            self.fail("02 §2.3", f"constant {e[1]} overflows {ty}")

    def run(self):
        fn = self.fn
        for s in fn.get("structs", []):
            if s.get("packed") and any(t.startswith("*") for _, t in s["fields"]) and (self.target or "").startswith("cheri"):
                raise Reject("CHERI §4", "pointer field in a packed struct")
        for n, t in fn["params"]:
            check_type(t, self.target)
            self.env[n], self.state[n] = t, "live"
        self.locals = {n for st in fn["body"] for n in _lets(st)}
        _number(fn["body"], self.uses, [0])
        self.pc = self.counter = 0
        self.block(fn["body"])
        for n, t in self.env.items():
            if base(t) in LINEAR and self.state[n] == "live":
                raise Reject("07 §1.2", f"{n} needs completion but is dropped")


def _lets(st):
    if st[0] == "let":
        yield st[1]
    for part in st[1:]:
        if isinstance(part, list) and part and isinstance(part[0], list):
            for s in part:
                yield from _lets(s)


def _names(e, out):
    if isinstance(e, list) and e:
        if isinstance(e[0], str):
            if e[0] == "var":
                out.add(e[1])
            elif e[0] in BORROW_EXPRS:
                out.add(e[1].split(".")[0])
            rest = e[1:]
        else:
            rest = e
        for x in rest:
            _names(x, out)
    return out


def _names_all(st, out):
    """every name a statement mentions, nested statements included"""
    if isinstance(st, list) and st and st[0] in ("let", "set", "store"):
        if st[0] != "store":
            out.add(st[1].split(".")[0])
    return _names(st, out)


def _number(body, uses, counter):
    """pre-order statement ids, as Judge.stmt assigns them; uses[name] = id of the last statement whose
    own expressions mention name (the live range of a borrow ends there)"""
    for st in body:
        here = counter[0]
        counter[0] += 1
        own = {"let": st[3:4], "set": st[2:3], "if": st[1:2], "while": st[1:2], "do": st[1:2],
               "return": st[1:2], "unsafe": [], "store": st[1:4]}[st[0]]
        for n in _names(own, set()):
            uses[n] = max(uses.get(n, -1), here)
        for part in st[1:]:
            if isinstance(part, list) and part and isinstance(part[0], list) and isinstance(part[0][0], str) \
                    and part[0][0] in ("let", "set", "if", "while", "do", "return", "unsafe", "store"):
                _number(part, uses, counter)
        if st[0] == "while":        # gate 15: a name from outside the loop used in it is live to the loop's end,
            inner = set(_names(st[1:2], set()))   # since the next turn uses it again
            for b in st[2]:
                _names_all(b, inner)
            declared = {n for b in st[2] for n in _lets(b)}
            for n in inner - declared:
                uses[n] = max(uses.get(n, -1), counter[0] - 1)


def judge(fn, target=None):
    """returns ("accept", None) or ("reject", rule)"""
    try:
        Judge(fn, target).run()
        return ("accept", None)
    except Reject as r:
        return ("reject", r.rule)
