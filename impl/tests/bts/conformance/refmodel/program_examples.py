"""Accept/reject examples in the shared program notation (progmodel.py). Every reject example has an
accept twin that differs in one place. `verdict` and `rule` are written by hand; gen.py checks that
the judge agrees."""

V = lambda n: ["var", n]
L = lambda v, t="u32": ["lit", str(v), t]
OP = lambda name, disp, *a: ["op", name, disp, *a]
CALL = lambda f, *a: ["call", f, *a]
SEC = [["k", "secret u32"], ["p", "u32"]]


def pair(name, rule, bad, good, target=None, note=None):
    """(reject, accept) twins sharing the rule they are about"""
    out = []
    for verdict, fn in (("reject", bad), ("accept", good)):
        ex = {"name": f"{name}-{verdict}", "rule": rule, "verdict": verdict, "twin": f"{name}-{'accept' if verdict == 'reject' else 'reject'}",
              "fn": fn}
        if target:
            ex["target"] = target
        if note:
            ex["note"] = note
        out.append(ex)
    return out


def body(params, *stmts, **extra):
    return {"params": params, "body": list(stmts), **extra}


EXAMPLES = []
E = EXAMPLES.extend

# ---- secret values (08 §1.4, 04 §2.2) ----------------------------------------------
E(pair("secret-branch", "08 §1.4 (1)",
       body(SEC, ["if", OP("lt", None, V("k"), L(5)), [["do", CALL("send_secret", V("k"))]], []]),
       body(SEC, ["if", OP("lt", None, V("p"), L(5)), [["do", CALL("send_secret", V("k"))]], []])))
E(pair("secret-loop", "08 §1.4 (1)",
       body(SEC, ["let", "i", "u32", L(0)], ["while", OP("lt", None, V("i"), V("k")), [["set", "i", OP("add", "wrap", V("i"), L(1))]]]),
       body(SEC, ["let", "i", "u32", L(0)], ["while", OP("lt", None, V("i"), V("p")), [["set", "i", OP("add", "wrap", V("i"), L(1))]]])))
E(pair("secret-index", "08 §1.4 (2)",
       body(SEC + [["a", "array(u8,16)"]], ["let", "x", "u8", ["index", V("a"), V("k")]]),
       body(SEC + [["a", "array(u8,16)"]], ["let", "x", "u8", ["index", V("a"), V("p")]])))
E(pair("secret-offset", "08 §1.4 (2)",
       body(SEC + [["q", "*u8"]], ["let", "r", "*u8", ["offset", V("q"), V("k")]]),
       body(SEC + [["q", "*u8"]], ["let", "r", "*u8", ["offset", V("q"), V("p")]])))
E(pair("secret-vt-op", "08 §1.4 (3)",
       body(SEC, ["let", "d", "secret u32", OP("div_trunc", "trap", V("k"), L(3))]),
       body(SEC, ["let", "d", "u32", OP("div_trunc", "trap", V("p"), L(3))]),
       note="unsigned division by a public non-zero constant cannot fail, so only rule 3 applies"))
E(pair("secret-trap-disposition", "08 §1.4 (4)",
       body(SEC, ["let", "s", "secret u32", OP("add", "trap", V("k"), V("p"))]),
       body(SEC, ["let", "s", "secret u32", OP("add", "wrap", V("k"), V("p"))])))
E(pair("secret-to-public", "08 §1.4 (5)",
       body(SEC, ["let", "x", "u32", V("k")]),
       body(SEC, ["let", "x", "secret u32", V("k")])))
E(pair("secret-extern-arg", "08 §1.4 (6)",
       body(SEC, ["do", CALL("send", V("k"))]),
       body(SEC, ["do", CALL("send_secret", V("k"))])))
E(pair("secret-shift-count", "08 §1.4 (7)",
       body(SEC, ["let", "s", "secret u32", OP("shl", "wrap", V("p"), V("k"))]),
       body(SEC, ["let", "s", "secret u32", OP("shl", "wrap", V("k"), V("p"))])))
E(pair("secret-pointer-type", "04 §2.2",
       body([["q", "secret *u8"]]),
       body([["q", "*u8"]])))
E(pair("declassify-outside-unsafe", "08 §4.3",
       body(SEC, ["do", CALL("send", ["declassify", V("k")])]),
       body(SEC, ["unsafe", [["do", CALL("send", ["declassify", V("k")])]]])))

# ---- provenance (04 §2.1, 08 §4.3) ----------------------------------------------------
E(pair("int-to-pointer", "04 §2.1",
       body([["a", "uaddr"], ["base", "*u8"]], ["let", "q", "*u8", ["cast", V("a"), "*u8"]]),
       body([["a", "uaddr"], ["base", "*u8"]], ["let", "q", "*u8", ["with_addr", V("base"), V("a")]])))
E(pair("device-ptr-safe", "04 §2.1",
       body([], ["let", "d", "*u8", ["device_ptr", "data", L(32, "uaddr")]]),
       body([], ["unsafe", [["let", "d", "*u8", ["device_ptr", "data", L(32, "uaddr")]]]]),
       target="avr"))
E(pair("raw-deref-safe", "08 §4.3 T1",
       body([["q", "*u8"]], ["let", "x", "u8", ["deref", V("q")]]),
       body([["q", "*u8"]], ["unsafe", [["let", "x", "u8", ["deref", V("q")]]]])))

# ---- ownership (07 §1) ---------------------------------------------------------------
BOX = [["b", "owned_ptr(u8,static)"], ["c", "owned_ptr(u8,static)"], ["p", "u32"]]
E(pair("use-after-move", "07 §1.1",
       body(BOX, ["do", CALL("consume_box", V("b"))], ["do", CALL("consume_box", V("b"))], ["do", CALL("consume_box", V("c"))]),
       body(BOX, ["do", CALL("consume_box", V("b"))], ["do", CALL("use_u8", L(1, "u8"))], ["do", CALL("consume_box", V("c"))])))
E(pair("completion-missing", "07 §1.2",
       body([], ["let", "f", "File", CALL("open")]),
       body([], ["let", "f", "File", CALL("open")], ["do", CALL("close", V("f"))])))
E(pair("completion-twice", "07 §1.1",
       body([], ["let", "f", "File", CALL("open")], ["do", CALL("close", V("f"))], ["do", CALL("close", V("f"))]),
       body([], ["let", "f", "File", CALL("open")], ["do", CALL("close", V("f"))], ["do", CALL("use_u8", L(1, "u8"))])))
E(pair("completion-one-branch", "07 §1.1",
       body([["p", "u32"]], ["let", "f", "File", CALL("open")],
            ["if", OP("lt", None, V("p"), L(1)), [["do", CALL("close", V("f"))]], []]),
       body([["p", "u32"]], ["let", "f", "File", CALL("open")],
            ["if", OP("lt", None, V("p"), L(1)), [["do", CALL("close", V("f"))]], [["do", CALL("close", V("f"))]]])))
E(pair("move-in-loop", "07 §1.1",
       body(BOX, ["let", "i", "u32", L(0)],
            ["while", OP("lt", None, V("i"), V("p")), [["do", CALL("consume_box", V("b"))], ["set", "i", OP("add", "wrap", V("i"), L(1))]]],
            ["do", CALL("consume_box", V("c"))]),
       body(BOX, ["let", "i", "u32", L(0)],
            ["while", OP("lt", None, V("i"), V("p")), [["do", CALL("use_u8", L(1, "u8"))], ["set", "i", OP("add", "wrap", V("i"), L(1))]]],
            ["do", CALL("consume_box", V("c"))])))
E(pair("overwrite-without-completion", "07 §1.2",
       body([], ["let", "f", "File", CALL("open")], ["set", "f", CALL("open")], ["do", CALL("close", V("f"))]),
       body([], ["let", "f", "File", CALL("open")], ["do", CALL("close", V("f"))], ["set", "f", CALL("open")], ["do", CALL("close", V("f"))])))

# ---- borrowing (07 §2) ---------------------------------------------------------------
XY = [["x", "u8"], ["y", "u8"]]
E(pair("two-mut-borrows", "07 §2",
       body(XY, ["let", "m1", "mutref u8", ["mutref", "x"]], ["let", "m2", "mutref u8", ["mutref", "x"]],
            ["do", CALL("use_mut", V("m1"))], ["do", CALL("use_mut", V("m2"))]),
       body(XY, ["let", "m1", "mutref u8", ["mutref", "x"]], ["let", "m2", "mutref u8", ["mutref", "y"]],
            ["do", CALL("use_mut", V("m1"))], ["do", CALL("use_mut", V("m2"))])))
E(pair("ref-and-mut", "07 §2",
       body(XY, ["let", "r", "ref u8", ["ref", "x"]], ["let", "m", "mutref u8", ["mutref", "x"]],
            ["do", CALL("use_ref", V("r"))], ["do", CALL("use_mut", V("m"))]),
       body(XY, ["let", "r", "ref u8", ["ref", "x"]], ["let", "m", "mutref u8", ["mutref", "y"]],
            ["do", CALL("use_ref", V("r"))], ["do", CALL("use_mut", V("m"))])))
E(pair("many-refs-then-write", "07 §2",
       body(XY, ["let", "r1", "ref u8", ["ref", "x"]], ["let", "r2", "ref u8", ["ref", "x"]],
            ["set", "x", L(1, "u8")], ["do", CALL("use_ref", V("r1"))], ["do", CALL("use_ref", V("r2"))]),
       body(XY, ["let", "r1", "ref u8", ["ref", "x"]], ["let", "r2", "ref u8", ["ref", "x"]],
            ["set", "y", L(1, "u8")], ["do", CALL("use_ref", V("r1"))], ["do", CALL("use_ref", V("r2"))])))
E(pair("move-while-borrowed", "07 §2",
       body(BOX, ["let", "r", "ref u8", ["ref", "b"]], ["do", CALL("consume_box", V("b"))], ["do", CALL("use_ref", V("r"))]),
       body(BOX, ["let", "r", "ref u8", ["ref", "b"]], ["do", CALL("consume_box", V("c"))], ["do", CALL("use_ref", V("r"))])))
E(pair("disjoint-fields", "07 §2",
       body([["s", "struct S"]], ["let", "m1", "mutref u8", ["mutref", "s.a"]], ["let", "m2", "mutref u8", ["mutref", "s.a"]],
            ["do", CALL("use_mut", V("m1"))], ["do", CALL("use_mut", V("m2"))]),
       body([["s", "struct S"]], ["let", "m1", "mutref u8", ["mutref", "s.a"]], ["let", "m2", "mutref u8", ["mutref", "s.b"]],
            ["do", CALL("use_mut", V("m1"))], ["do", CALL("use_mut", V("m2"))]),
       note="different fields of one struct are different places"))
E(pair("ref-outlives-value", "07 §2",
       body([["y", "ref u8"]], ["let", "x", "u8", L(1, "u8")], ["return", ["ref", "x"]], ret="ref u8"),
       body([["y", "ref u8"]], ["let", "x", "u8", L(1, "u8")], ["return", V("y")], ret="ref u8")))

# ---- zero-size elements (04 §2.1) ----------------------------------------------------------
E(pair("diff-zero-size", "04 §2.1",
       body([["q", "*array(u8,0)"]], ["unsafe", [["let", "d", "idiff", ["diff", ["offset", V("q"), L(1, "idiff")], V("q")]]]]),
       body([["q", "*u8"]], ["unsafe", [["let", "d", "idiff", ["diff", ["offset", V("q"), L(1, "idiff")], V("q")]]]])))
E(pair("diff-nested-zero-size", "04 §2.1",
       body([["q", "*array(array(u8,0),4)"]], ["unsafe", [["let", "d", "idiff", ["diff", ["offset", V("q"), L(1, "idiff")], V("q")]]]]),
       body([["q", "*array(array(u8,2),4)"]], ["unsafe", [["let", "d", "idiff", ["diff", ["offset", V("q"), L(1, "idiff")], V("q")]]]]),
       note="an array of zero-size arrays has size 0 (06 §4.4)"))

# ---- slice permission (06 §4.3) --------------------------------------------------------------
ARR = [["a", "array(u8,4)"], ["b", "array(u8,4)"]]
E(pair("store-through-read-view", "06 §4.3",
       body([["s", "slice(u8)"]], ["store", V("s"), L(0, "usize"), L(1, "u8")]),
       body([["s", "slice_mut(u8)"]], ["store", V("s"), L(0, "usize"), L(1, "u8")])))
E(pair("simd-store-read-view", "06 §4.3",
       body([["s", "slice(u8)"], ["v", "simd(u8,16)"], ["n", "usize"]], ["do", CALL("store_masked", V("s"), V("v"), V("n"))]),
       body([["s", "slice_mut(u8)"], ["v", "simd(u8,16)"], ["n", "usize"]], ["do", CALL("store_masked", V("s"), V("v"), V("n"))]),
       note="03 §3.5 store_masked takes a write view"))
E(pair("write-view-from-read-view", "06 §4.3",
       body(ARR, ["let", "r", "slice(u8)", ["slice", "a"]], ["let", "w", "slice_mut(u8)", ["slice_mut", "r"]],
            ["store", V("w"), L(0, "usize"), L(1, "u8")]),
       body(ARR, ["let", "r", "slice(u8)", ["slice", "a"]], ["let", "w", "slice_mut(u8)", ["slice_mut", "a"]],
            ["store", V("w"), L(0, "usize"), L(1, "u8")])))
E(pair("two-write-views", "07 §2",
       body(ARR, ["let", "s", "slice_mut(u8)", ["slice_mut", "a"]], ["let", "t", "slice_mut(u8)", ["slice_mut", "a"]],
            ["store", V("s"), L(0, "usize"), L(1, "u8")], ["store", V("t"), L(0, "usize"), L(2, "u8")]),
       body(ARR, ["let", "s", "slice_mut(u8)", ["slice_mut", "a"]], ["let", "t", "slice_mut(u8)", ["slice_mut", "b"]],
            ["store", V("s"), L(0, "usize"), L(1, "u8")], ["store", V("t"), L(0, "usize"), L(2, "u8")])))
E(pair("read-view-and-write-view", "07 §2",
       body(ARR, ["let", "r", "slice(u8)", ["slice", "a"]], ["let", "w", "slice_mut(u8)", ["slice_mut", "a"]],
            ["store", V("w"), L(0, "usize"), L(1, "u8")], ["do", CALL("sum", V("r"))]),
       body(ARR, ["let", "r", "slice(u8)", ["slice", "a"]], ["let", "w", "slice_mut(u8)", ["slice_mut", "b"]],
            ["store", V("w"), L(0, "usize"), L(1, "u8")], ["do", CALL("sum", V("r"))])))
E(pair("write-view-is-moved", "07 §1.1",
       body(ARR, ["let", "s", "slice_mut(u8)", ["slice_mut", "a"]], ["let", "t", "slice_mut(u8)", V("s")],
            ["store", V("s"), L(0, "usize"), L(1, "u8")]),
       body(ARR, ["let", "s", "slice_mut(u8)", ["slice_mut", "a"]], ["let", "t", "slice_mut(u8)", V("s")],
            ["store", V("t"), L(0, "usize"), L(1, "u8")]),
       note="a read view is copied; a write view is only moved"))
E(pair("write-while-viewed", "07 §2",
       body(ARR, ["let", "r", "slice(u8)", ["slice", "a"]], ["set", "a", L(0, "array(u8,4)")], ["do", CALL("sum", V("r"))]),
       body(ARR, ["let", "r", "slice(u8)", ["slice", "a"]], ["set", "b", L(0, "array(u8,4)")], ["do", CALL("sum", V("r"))]),
       note="making a view from an array is a borrow of the array (06 §4.4)"))

# ---- object size limit (04 §3, 05 §1, gate 13) ------------------------------------------------
E(pair("object-size-limit", "04 §3",
       body([["buf", "ref array(u8,40000)"]]),
       body([["buf", "ref array(u8,32767)"]]), target="avr",
       note="on a 16-bit target an object is at most 32767 bytes, so diff of any two pointers into it fits idiff"))

# ---- constants in branches chosen at translation (02 §2.3, 08 §2, gate 14) -----------------------
BIG = ["let", "n", "usize", ["op", "mul", None, ["lit", "1024", "usize"], ["lit", "1024", "usize"]]]
SMALL = ["let", "n", "usize", ["lit", "1024", "usize"]]
E(pair("constant-overflow-live-branch", "02 §2.3",
       body([], ["if", ["op", "le", None, ["lit", "8", "u32"], ["query", "usize_bits"]], [BIG], [SMALL]]),
       body([], ["if", ["op", "le", None, ["lit", "32", "u32"], ["query", "usize_bits"]], [BIG], [SMALL]]),
       target="avr",
       note="1024 * 1024 overflows a 16-bit usize; it is rejected only in a branch the target's usize_bits chooses"))

# ---- arena reset (05 §3) ---------------------------------------------------------------------
AR2 = [["ar", "Arena"], ["other", "Arena"]]
E(pair("arena-reset-live-block", "05 §3",
       body(AR2, ["let", "k", "owned_ptr(u8,arena)", CALL("arena_alloc", ["ref", "ar"])],
            ["do", CALL("arena_reset", ["mutref", "ar"])], ["do", CALL("free_block", V("k"))]),
       body(AR2, ["let", "k", "owned_ptr(u8,arena)", CALL("arena_alloc", ["ref", "other"])],
            ["do", CALL("arena_reset", ["mutref", "ar"])], ["do", CALL("free_block", V("k"))]),
       note="resetting another arena does not touch the block"))
E(pair("arena-reset-unused-block", "05 §3",
       body([["ar", "Arena"]], ["let", "k", "owned_ptr(u8,arena)", CALL("arena_alloc", ["ref", "ar"])],
            ["do", CALL("arena_reset", ["mutref", "ar"])]),
       body([["ar", "Arena"]], ["let", "k", "owned_ptr(u8,arena)", CALL("arena_alloc", ["ref", "ar"])],
            ["do", CALL("free_block", V("k"))], ["do", CALL("arena_reset", ["mutref", "ar"])]),
       note="a block not used after reset is still live: it is dropped, and returned to the arena, at the end of the scope"))

# ---- program space (AVR annex §2.2) --------------------------------------------------------
TBL = [["tbl", "ref @program u8"], ["p", "u8"]]
E(pair("program-mutref", "AVR §2.2",
       body(TBL, ["let", "t", "mutref @program u8", V("tbl")]),
       body(TBL, ["let", "t", "ref @program u8", V("tbl")]), target="avr"))
E(pair("program-raw-pointer", "AVR §2.2",
       body([["q", "*u8 @program"]]),
       body([["q", "ref @program u8"]]), target="avr"))
E(pair("program-space-mismatch", "AVR §2.2",
       body(TBL, ["do", CALL("use_ref", V("tbl"))]),
       body(TBL, ["do", CALL("read_flash", V("tbl"))]), target="avr"))
E(pair("program-non-constant", "AVR §2.2",
       body(TBL, ["let", "c", "@program u8", V("p")]),
       body(TBL, ["let", "c", "@program u8", L(7, "u8")]), target="avr"))

# ---- capability targets (CHERI annex §4) ----------------------------------------------------
E(pair("cheri-packed-pointer", "CHERI §4",
       body([], structs=[{"name": "P", "packed": True, "fields": [["tag", "u8"], ["p", "*u8"]]}]),
       body([], structs=[{"name": "P", "packed": False, "fields": [["tag", "u8"], ["p", "*u8"]]}]), target="cheri-rv64"))

# ---- packed structs (06 §1, gate 12) ---------------------------------------------------------
PK = [{"name": "P", "packed": True, "fields": [["tag", "u8"], ["n", "u32"]]}]
PKP = [["p", "struct P"]]   # packing is a property of the declaration in structs
E(pair("packed-field-ref", "06 §1",
       body(PKP, ["let", "r", "ref u32", ["ref", "p.n"]], structs=PK),
       body(PKP, ["let", "r", "ref u8", ["ref", "p.tag"]], structs=PK), target="x86_64",
       note="u32 is 4-aligned on x86_64; the u8 field is aligned whatever the packing"))
E(pair("packed-field-mutref-unsafe", "06 §1",
       body(PKP, ["let", "r", "mutref u32", ["mutref", "p.n"]], structs=PK),
       body(PKP, ["unsafe", [["let", "r", "mutref u32", ["mutref", "p.n"]]]], structs=PK), target="x86_64",
       note="in unsafe the reference is allowed; using it while it is under-aligned breaks the promise T2 (08 §4.3)"))
E(pair("packed-vs-plain-struct", "06 §1",
       body(PKP, ["let", "r", "ref u32", ["ref", "p.n"]], structs=PK),
       body(PKP, ["let", "r", "ref u32", ["ref", "p.n"]], structs=[{"name": "P", "packed": False, "fields": [["tag", "u8"], ["n", "u32"]]}]),
       target="x86_64", note="the same reference into the struct without packing is aligned"))
E(pair("packed-field-copy", "06 §1",
       body(PKP, ["let", "r", "ref u32", ["ref", "p.n"]], structs=PK),
       body(PKP, ["let", "r", "u32", ["field", V("p"), "n"]], structs=PK), target="x86_64",
       note="reading the field's value is allowed; the processor uses byte accesses where the target needs them"))

# ---- liveness and reborrowing (07 §1.1, §2, gate 15; report J) -------------------------------
XYC = [["x", "u8"], ["y", "u8"], ["c", "bool"]]
E(pair("borrow-live-next-turn", "07 §2",
       body(XYC, ["let", "r", "ref u8", ["ref", "x"]],
            ["while", V("c"), [["do", CALL("use_ref", V("r"))], ["set", "x", L(1, "u8")]]]),
       body(XYC, ["let", "r", "ref u8", ["ref", "x"]],
            ["while", V("c"), [["do", CALL("use_ref", V("r"))], ["set", "y", L(1, "u8")]]]),
       note="the next turn reads through r after the write; Miri (Tree and Stacked Borrows) reports the reject twin"))
E(pair("borrow-carried-by-copy", "07 §2",
       body(XYC, ["let", "r", "ref u8", ["ref", "x"]], ["let", "s", "ref u8", V("r")], ["set", "x", L(1, "u8")],
            ["do", CALL("use_ref", V("s"))]),
       body(XYC, ["let", "r", "ref u8", ["ref", "x"]], ["let", "s", "ref u8", V("r")], ["set", "y", L(1, "u8")],
            ["do", CALL("use_ref", V("s"))]),
       note="s is a copy of r, so it carries the borrow of x"))
E(pair("reborrow-not-move", "07 §1.1",
       body(XYC, ["let", "m", "mutref u8", ["mutref", "x"]], ["do", CALL("use_mut", V("m"))], ["let", "n", "mutref u8", V("m")],
            ["do", CALL("use_mut", V("m"))]),
       body(XYC, ["let", "m", "mutref u8", ["mutref", "x"]], ["do", CALL("use_mut", V("m"))], ["do", CALL("use_u8", ["deref", V("m")])],
            ["do", CALL("use_mut", V("m"))]),
       note="passing m to a &mut parameter reborrows it; putting it in another name moves it"))
E(pair("reborrow-in-loop", "07 §2",
       body(XYC, ["let", "m", "mutref u8", ["mutref", "x"]],
            ["while", V("c"), [["do", CALL("use_u8", ["deref", V("m")])], ["do", CALL("use_u8", V("x"))]]]),
       body(XYC, ["let", "m", "mutref u8", ["mutref", "x"]],
            ["while", V("c"), [["do", CALL("use_u8", ["deref", V("m")])], ["do", CALL("use_u8", V("y"))]]]),
       note="reading through m in a loop is a reborrow, not a move; reading x while m is live (next turn) is not allowed"))
