#import "../lib.typ": *

#sub("6.11", "Type-associated ops and traits")[
  #sub("6.11.1", "Attaching an op to a type")[
    #para("1")[
      If a type name and a dot are put in front of an op's name, that op becomes #strong[attached to that type];.
    ]
    #para("2")[
      The first input of an attached op is a value of that type.
    ]
  ]
  #sub("6.11.2", "Traits")[
    #para("1")[
      A #t("트레이트", "trait") is #strong[a list]; of ops a type shall provide. It writes the name and
      signature of each op.
    ]
    #plain[
      When there are several shapes (rectangle · square …), the #strong[method]; of finding the area differs by type, but the #strong[promise];
      #emph["it tells its area"]; is the same. A trait gives that promise a name. Then an op that takes #emph["anything that tells its area"];
      can be written #strong[once]; — the same op need not be copied for each type, and a type that does not keep the promise is caught at translation.
      The call site need not know what the type is. This language has no inheritance, so this is the way to handle
      several types under one name.
    ]
    #ex("what a trait is for — one op takes several types", "module ex_trait_why .

rem Each shape finds its area differently. But the promise «it tells its area» is the same.
trait shape do
  area input s self . output u64 .
end .

struct rect do
  satisfies shape .
  w u64 .
  h u64 .
end .

struct square do
  satisfies shape .
  side u64 .
end .

fn rect.area input s rect . output u64 .
do
  return mul field s. w . field s. h . . .
end .

fn square.area input s square . output u64 .
do
  return mul field s. side . field s. side . . .
end .

rem This op takes **any shape** — `requires shape t` pins it to «only types that tell their area».
fn double_area input comptime t type . input s t . output u64 .
  requires shape t. . .
do
  return mul 2 method s. area. . . .
end .

fn demo output u64 .
do
  let r lit rect do w 2 . h 3 . end . .
  let q lit square do side 4 . end . .
  return add double_area rect. r. . double_area square. q. . . .
end .",
      out: "demo() = 44")
    #para("1a")[
      A trait can write #strong[several]; ops. A signature #strong[begins with the op's name]; and that op's clauses follow,
      and the next name opens the next signature. It is customary to write one signature per line. The clauses of a signature follow #strong[the same order];
      as an op head (#cref("6.4.1") (3a)) — inputs · output · effects. There is no limit on the number of ops written.
    ]
    #para("1b")[
      `fn` · `proc` #strong[are not written]; in a signature. What that op can do is decided by the signature's `effects` line.
      - Without an `effects` line, it is an op with #strong[no]; effects. It can be provided by an `fn`, or by a `proc` that writes its effects.
      A `proc` always writes an `effects` line (#cref("7.1"); without one, `E-EFFECT-MISSING`) — a `proc` that provides an effect-free signature
      writes `effects none .`.
      - With an `effects` line, the providing side is a `proc` that writes those effects #strong[or fewer];, or an `fn` if there are no effects.
    ]
    #para("2")[
      Inside a trait, `self` refers to the type that will provide it.
    ]
    #para("3")[
      If a type writes a trait with `satisfies`, the processor checks that the ops of that list exist #strong[with exactly those
      signatures];. If even one is missing or mismatched, translation is refused.
    ]
    #para("4")[
      There are five kinds of mismatch.
    ]
    #tbl("Places that fail to provide a trait")[
      #table(columns: (auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
      [*Diagnostic*], [*What went wrong*],
      [`E-TRAIT-UNDEF`], [`satisfies` calls a trait that #strong[is not declared]; — a claim that points at nothing checks nothing],
      [`E-TRAIT-MISSING`], [an op of the list is missing],
      [`E-TRAIT-SIG`], [the op exists but #strong[the number of parameters]; differs — a trait that does not fit exactly cannot carry a contract],
      [`E-TRAIT-EFFECT`], [the providing side has #strong[effects the trait does not write];. The caller judges by the trait's contract, so doing what is not in that contract makes the judgment wrong],
      [`E-TRAIT-RECV`], [#strong[an op]; wrote that it provides a trait — what provides is #strong[a type];],
      )
    ]
    #para("5")[
      If an op of a trait writes `via self` in its effects line, the providing side may write #strong[allocation-family effects]; —
      `alloc` · `heap` · `lock` · `atomic` — for that op beyond the trait. Writing any other additional effect
      is still `E-TRAIT-EFFECT`. The additional effects are carried by a generic op that calls through `via <type parameter>` up to
      its callers (#cref("7.1.1") (6)).
    ]
    #plain[
      What a trait does is make «this type knows how to do these things» #strong[something the caller can trust];.
      So if only the names match and the effects differ, that trust breaks — only when the effects match too
      does a trait carry a contract.
    ]
    #ex("providing a trait", "module ex_trait .

rem A trait is the list of ops a type shall provide.
trait shape do
  area input s self . output u64 . effects none .
end .

rem Writing `satisfies` has it checked whether that list is provided.
struct rect do
  satisfies shape .
  w u8 .
  h u8 .
end .

fn rect.area input s rect . output u64 .
do
  return mul widen u64 field s. w . . widen u64 field s. h . . . .
end .")
    #rejected("writing that it provides and not providing", "module ex_trait_bad .

trait shape do
  area input s self . output u64 . effects none .
end .

struct rect do
  satisfies shape .     rem it wrote that it provides
  w u8 .
  h u8 .
end .

rem `rect.area` was not made", "E-TRAIT-MISSING")
    #ex("a trait with several ops", "module ex_trait_many .

rem One line per signature — it begins with the name, then inputs · output · effects.
trait shape do
  area input s self . output u64 .
  perimeter input s self . output u64 .
  grow input s self . input k u64 . output self .
  checked_area input s self . output u64 . effects panic .
end .

struct rect do
  satisfies shape .
  w u64 .
  h u64 .
end .

rem A signature without an effects line is provided with an `fn`.
fn rect.area input s rect . output u64 .
do
  return mul field s. w . field s. h . . .
end .

fn rect.perimeter input s rect . output u64 .
do
  return mul 2 add field s. w . field s. h . . . .
end .

fn rect.grow input s rect . input k u64 . output rect .
do
  return lit rect do w add field s. w . k. . . h add field s. h . k. . . end . .
end .

rem A signature that writes effects is provided with a `proc` that writes those effects.
proc rect.checked_area input s rect . output u64 . effects panic .
do
  if eq field s. w . 0 . do panic \"empty rect\" . end .
  return mul field s. w . field s. h . . .
end .")
    #rejected("trying to provide an effect-free signature with a proc without an effects line", "module ex_trait_proc_noeff .

trait shape do
  area input s self . output u64 .
end .

struct rect do
  satisfies shape .
  w u64 .
  h u64 .
end .

proc rect.area input s rect . output u64 .
do
  return mul field s. w . field s. h . . .
end .", "E-TRAIT-EFFECT")
    #plain[
      A trait is a way of writing #emph["this type can do these things"]; #strong[in a form that is checked];.
      Written as a comment, it stays the same even when the code changes, but with `satisfies`, translation is refused the moment
      it fails to provide.
    ]
    #caution("It is not inheritance")[
      A trait #strong[makes no hierarchy]; among types. That a type provides a trait is
      only the fact #emph["it has those ops"];; it inherits nothing from another type.
      This language has no inheritance.
    ]
  ]
  #sub("6.11.3", "Calling an attached op — method")[
    #para("1")[
      `method` is the form that calls an op attached to a value. Its shape is
      `method <value> <step> … <name> <argument> … .`.
    ]
    #para("2")[
      Steps and the name are told apart #strong[by following the type];. Going down one step at a time from the receiver's type,
      if that type has an attached op of that name, #strong[that is the name];, and everything
      after it is arguments. So `method o inner area` calls `area` attached to `inner` of `o`,
      and `method s plus 5` calls `plus` attached to `s` with `5`.
    ]
    #para("3")[
      The receiver need not be a name. It may be the result of another form, and then the receiver's type is
      that form's output type — so `method (method x grow 2) area` holds.
    ]
    #para("4")[
      If the receiver's type cannot be decided, the processor refuses the program. Attached ops
      are found #strong[by type];, so if the type is not known, the op to call is not decided either.
    ]
    #ex("calling an attached op", "module ex_method .

struct rect do
  w u64 .
  h u64 .
end .

fn rect.area input s rect . output u64 .
do
  return mul field s. w . field s. h . . .
end .

export fn twice_area input s rect . output u64 .
do
  return mul 2 method s. area. . . .
end .")
    #note[
      The infix notation (`s..area`) #strong[does not exist];. It was the only place in the language read from right to
      left, and there was no way to write a multi-step receiver. Unified as a form,
      a call is #strong[always name first];.
    ]
  ]
  #sub("6.11.4", "Where attached ops go wrong")[
    #para("1")[
      `method` requires both #strong[a receiver and a name]; (`E-METHOD-RECV`).
    ]
    #para("2")[
      If the receiver's type has no attached op of that name, it is refused (`E-METHOD-UNDEF`) —
      it shall be declared as `fn <type>.<name>`.
    ]
    #caution("")[
      An attached op is #strong[a device that divides the namespace];, not inheritance (#cref("6.11.2")). So calling a name that does not exist
      does not search upward; it is refused right there.
    ]
    #rejected("that type has no attached op of that name", "module ex_method_undef .

struct p do
  x u8 .
end .

fn f input s p . output u8 .
do
  return method s. nosuch. . .
end .", "E-METHOD-UNDEF")
  ]
]
