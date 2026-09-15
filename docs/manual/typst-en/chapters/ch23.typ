#import "../../typst-ko/lib.typ": *

= Traits --- one promise kept by many types

#chapter-toc()

#prereq(
  ([#chref("structs-enums"), Aggregates], [`struct` and `field`]),
  ([#chref("effects"), Effects], [the effects line narrows and spreads to callers]),
  ([#chref("generics"), Generics], [`requires <trait> t .` is a type condition]),
)

#deepqa[
  What did `max_of` in #chref("generics") call in its body, trusting `requires ordered t .`? And what happened when `plain`, which did not satisfy the
  condition, was given?
][
  It called `method a less b`. Giving `plain` was rejected with `E-BOUND-UNSAT`, because the condition is a promise and making an instance past it would make
  the callee's contract a lie. This chapter covers declaring and satisfying that promise --- the trait --- in full.
]

#why[
  A rectangle and a square compute their areas differently, but the promise "can tell its area" is the same. Naming that promise lets you write, *once*, an op
  that accepts "anything that can tell its area". This language has no inheritance, so traits are the way to treat several types under one name. This chapter
  properly unfolds the tool already used for allocators (#chref("fixed-memory")) and sorting (#chref("generics")).
]

#organizer[
  You will learn to attach ops to types (`fn rect.area`) and call them with `method`. You will pick up how to declare a trait and satisfy it with `satisfies`,
  and how to use traits with several ops. You will see the rule that trait signatures do not say `fn` or `proc` and that the `effects` line decides the
  implementer, the diagnostics for failing to satisfy a trait, `via self`, and why a trait is not inheritance.
]

#chapter-questions()

== What they are for

#demo("examples/ch23/why.low")

#idx("trait")
- `trait shape do … end` is the promise "has `area`". `self` in a signature is the type that will satisfy the promise.
- `rect` and `square` write `satisfies shape .` in their bodies, declaring they will satisfy it.
- `fn rect.area` and `fn square.area` actually satisfy it. The `rect.` in the name means the op is attached to `rect`.
- `double_area` accepts *any shape*. `requires shape t .` pins it down to "only types that can tell their area".
- `method s area` calls the `area` attached to the type of `s` --- `rect.area` for a `rect`, `square.area` for a `square`.

`double_area rect r` and `double_area square q` are each monomorphised to their own instance (#chref("generics")), so `method` is resolved at translation, not
looked up at run time. There is no virtual function table.

== Ops attached to types, and `method`

#idx("method")
An op can be attached to a type even without a trait. Put the type name and a dot in front of the op name, and its first input is a value of that type.
`method <value> <name> <args…>` calls that op. The receiver may be the result of another form --- `method (method r grow 1) area`. If no such op is attached,
the call is rejected.

#demo("examples/ch23/method_undef.low")

Calling a name that does not exist does not search upwards. Attached ops are a device for dividing the name space, not inheritance.

== Traits with several ops

#demo("examples/ch23/many.low")

There are three things to know about writing them.

- One line per signature. A line starts with the op's *name* and then writes clauses in the same order as an op head --- inputs, output, effects. The next name
  starts the next signature. Write as many as you like.
- When the output is `self`, as in `grow`, the implementer returns its own type (`rect`).
- An implementing op is named `<type>.<name>`.

`demo_empty` calls `checked_area` on a rectangle of width 0 and stops. The signature promised `effects panic`, so callers know it may stop.

#realcase[A defect: effects of ops called through `method` do not spread][
  Running `--check` on the example above attaches `W-EFFECT-OVER` (`panic` declared but never performed) to `demo`. It is a false warning --- `demo_empty` really
  does stop. While writing this book it turned out that the processor in this edition does not spread the effects of ops called through `method` to the caller.
  So even a pure `fn` can call an op that `panic`s through `method` and pass translation. Calling the same op directly as `rect.checked_area r` is correctly
  rejected. The behaviour the specification (#chref("effects")) requires is the direct call's; the `method` side is a defect. Until it is fixed, do not call
  effectful attached ops through `method` inside pure ops.
]

== Signatures do not say `fn` or `proc`

Writing `fn` or `proc` in a trait signature is rejected.

#demo("examples/ch23/sig_kind.low")

What the op may do is decided by the signature's `effects` line.

#dtable(
  columns: 2,
  id: "traits-effects",
  caption: [A signature's `effects` and the implementer],
  [*Signature's `effects`*], [*Implementer*],
  [none], [a `fn`, or a `proc` that writes `effects`],
  [present, e.g. `effects panic`], [a `proc` declaring those effects or fewer; a `fn` if it uses none],
)

A `proc` *without* an `effects` line is not narrowed and reads as able to do anything (#chref("ops")). So implementing an effect-free signature with such a
`proc` is rejected.

#demo("examples/ch23/proc_noeff.low")

Declaring more effects than the signature is rejected too.

#demo("examples/ch23/more_effect.low")

Callers reason against *the trait's contract*. If the implementer does something outside that contract, the reasoning goes wrong. A trait whose names match but
whose effects differ cannot carry a contract.

== When a trait is not satisfied

#dtable(
  columns: 2,
  id: "traits-diags",
  caption: [Where a trait is not satisfied],
  [*Diagnostic*], [*Meaning*],
  [`E-TRAIT-UNDEF`], [`satisfies` names an undeclared trait],
  [`E-TRAIT-MISSING`], [one op in the list is missing],
  [`E-TRAIT-SIG`], [`fn`/`proc` written in a signature, or the op exists with a different parameter count],
  [`E-TRAIT-EFFECT`], [the implementing op has more effects than the signature],
  [`E-TRAIT-RECV`], [`satisfies` was written on an op instead of a type],
)

#demo("examples/ch23/missing.low")

`satisfies` is not a comment. The moment you write that you will satisfy a trait, the processor checks the whole list.

#qa[
  Can a trait op have a default implementation?
][
  No. A trait is only a list and holds no code. If several types want to share an implementation, write it as a generic op (like `double_area`) and have the
  types satisfy only the minimum that op needs. With default implementations you have to search upwards to find where a type's `area` came from, and that is
  the entropy inheritance creates.
]

== `via self` --- more allocation effects only

Some promises, like the allocator trait, have allocation effects that differ by implementation. When a signature's effects line says `via self`, the
implementer may declare *more* allocation-family effects (`alloc`, `heap`, `lock`, `atomic`) than the signature. Declaring more of any other effect is still
`E-TRAIT-EFFECT`.

```lowent
export trait byte_allocator do
  reserve input s self . input n u64 . output option mut slice u8 . effects state via self .
  grow input s self . input old mut slice u8 . input newn u64 . output option mut slice u8 . effects state via self .
  used input s self . output u64 . effects state .
end .
```

A bump allocator's `reserve` is just `state`, while `heap_bytes`, carving from the heap, has `heap state`. That difference rises through a generic op's `via a`
all the way to its callers (#chref("fixed-memory")).

#misconception[Satisfying a trait inherits something from it][
  Nothing is inherited. Satisfying a trait is only the *fact* that "these ops exist", and no hierarchy arises between types. `rect` and `square` have no relation
  to each other even after satisfying `shape`. This language has no inheritance.
]

== Common mistakes

#antipattern[Writing the name before the value after `method`][
  #demo("examples/ch23/mistake_methodorder.low")

  Translating `r.area()` from an object-oriented language backwards easily gives `method area r`. `method` takes *the value first*, because
  the value's type decides which attached op to look for. With the name first, the tool reads `area` as the value, looks for an op `r` on
  its type, and reports `E-METHOD-UNDEF`. Write `method r area`.
]

#antipattern[Leaving the type prefix off the name of the implementing op][
  #demo("examples/ch23/mistake_noprefix.low")

  `fn area input s rect .` is an *ordinary op* that takes a `rect`, not an op attached to `rect`. The trait looks for `rect.area`, so this
  is `E-TRAIT-MISSING`. The tool does not attach ops by looking at their input types because, when several ordinary ops take the same
  type, the name alone should tell you which one fulfils the promise. This diagnostic lacks the file name; its line number points at the type
  declaration that says `satisfies`.
]

#antipattern[Treating a type as adopting a trait just because the op exists][
  #demo("examples/ch23/mistake_nosatisfies.low")

  With `rect.area` in place, `rect` is indeed "a type that reports an area". Without `satisfies shape .`, though, `double_area rect r` is
  `E-BOUND-UNSAT`. Under duck typing, where matching shape is enough, an op that happens to share a name is wrongly read as fulfilling the
  promise. `satisfies` declares "I will keep this contract", and only that declaration makes the processor check the whole list.
]

#antipattern[Putting the receiver somewhere other than the first input][
  #demo("examples/ch23/mistake_recvlast.low")

  The first input of an attached op is the receiver. `method r scaled 5` puts `r` in the first position and 5 in the second. `scaled` takes
  the first as `k` and the second as `s`, so it tries to multiply a struct and stops. By meaning, translation should reject this head, but
  this edition's tool accepts it (recorded as a defect in the development repository). Always put `input s rect .` first in an attached op.
]

#misconception[An op attached to a type can only be called through `method`][
  #demo("examples/ch23/direct_call.low")

  `rect.area r` calls it directly by name, and `method r area` finds the same op from the value's type. They are the same op, and the results
  are 6 and 6. `method` earns its place where the type is a generic parameter and its name cannot be written (`method s area` in
  `double_area`). Where the type is known, a direct call also propagates effects correctly (see the defect case above).
]

== This chapter's syntax at a glance

#dtable(
  columns: 3,
  id: "traits-glance",
  caption: [Trait syntax --- shape · meaning · why it looks this way],
  [*Shape*], [*Meaning*], [*Why*],
  [`fn rect.area input s rect . …`], [attach an op to a type --- the first input is the receiver], [a way to divide the name space, not inheritance],
  [`method r area` · `rect.area r`], [call by the value's type · call directly by name], [fixed at translation time --- no virtual table],
  [`trait shape do area input s self . output u64 . end .`], [the list of ops a type must have], [`self` is the adopting type itself],
  [signature line: name · inputs · output · effects], [same order as an op head; no `fn`/`proc`], [the effects line caps the implementer],
  [`struct rect do satisfies shape . … end .`], [declare that this type keeps the promise], [the declaration triggers the full check],
  [`requires shape t .`], [type condition of a generic op], [a type that does not adopt it: `E-BOUND-UNSAT`],
  [`effects state via self .` (signature)], [only allocation effects may be added], [allocators differ in effects],
  [`E-TRAIT-MISSING` · `-SIG` · `-EFFECT` · `-RECV` · `-UNDEF`], [one diagnostic per way of falling short], [`satisfies` is not a comment],
)

#recap[
  `fn <type>.<name>` attaches an op to a type and `method` calls it. A trait is a list of ops a type must have, and a type declares it satisfies one with
  `satisfies` in its body. Signatures start with a name and write inputs, output and effects in order, without `fn` or `proc`. A signature's `effects` is the
  upper bound on the implementer's effects. Mismatches are rejected with `E-TRAIT-*`, and `via self` allows only more allocation-family effects. A trait is not
  inheritance.
]
