#import "../../typst-ko/lib.typ": *

= Aggregates --- `struct` and `enum`

#chapter-toc()

#prereq(
  ([#chref("control"), Flow], [`match` must cover every case]),
  ([#chref("slices"), Sequences], [a slice carries its length]),
)

#deepqa[
  Besides the exhaustiveness check, what did #chref("control") give as the advantage of `match` over an `if` chain?
][
  What happens when cases grow. Add a variant to an enum and every `match` over it stops at translation and points to what must be fixed,
  while an `if` chain lets the new case slide silently into its last `else`. This chapter makes such enums.
]

#why[
  Numbers and slices alone cannot write shapes like "a point", "a segment", or "a circle or a rectangle or a dot". You need a collection of
  named fields (`struct`) and one-of-several (`enum`) for data to take the shape of the problem. In particular, the pairing of an `enum`
  carrying values with `match` is Lowent's main replacement for inheritance, and `option` and `result` (#chref("option-result")) and traits
  (#chref("traits")) all build on it.
]

#organizer[
  You will learn to declare a `struct`, make one with `lit` and read it with `field`, and why there is no glued dot. You will see that the
  variants of an `enum` can carry values, how to split them with `match` while binding those values to names, and why variants must be
  closed with full stops. You will also learn why a type that contains itself is rejected, and how to link tree structures by number.
]

#chapter-questions()

== `struct` --- a collection of named fields

#idx("struct")
A `struct` is a collection of named fields, each with its own type. The declaration's body is `do … end`, and each field is closed with a
full stop.

#demo("examples/ch10/points.low")

- `lit point do x 1 . y 1 . end` makes a value. *Every field must be filled* when making it. No unfilled field quietly becomes 0.
  To fill the rest with one value, end with `_ <value> .`: `lit point do x 1 . _ 0 . end`.
- `field s stop x` reads the `stop` field of `s` and the `x` field inside it. With several steps, it goes down one field at a time from
  the left.
- A field's type may be another `struct`. `segment` holds two `point`s.
- `moved` builds and returns a *new value* with one field changed. The original stays as it was.

There is no glued dot like `p.x`. Dots are already used for module names (`allocs.byte_allocator`) and variant names (`shape.dot`). If
looking inside a value were written with a dot too, what `a.b` means would only be settled after looking up whether `a` is a value or a
module. The `field` form settles it the moment you read it.

#qa[
  Can a field be named `to` or `in`?
][
  No. `to` and `in` were infix spellings for reading fields in the old grammar (`a to b`) and are now removed words. A spelling that is a word
  cannot be a name, so it is rejected with `E-VOCAB-REMOVED`. That is why this example uses `start` and `stop` rather than `from` and `to`.
]

`field` is both a place to read and a place to write. A field of a value received as `mut` is changed with `set (field p x) 3 .` --- reading
and writing use the same spelling. The rules for borrowing a value to change it are in #chref("references").

=== Array fields --- a field that holds bytes

When a field's type is an array with a length, like `array u8 4`, its bytes live *inside the record*. `field p body` is a slice that
sees those bytes, and `set (index (field p body) i) v .` writes an element.

#demo("examples/ch10/arrayfield.low")

- A struct is a value. `var q be pkt p .` copies the array field's bytes too, so changing `q` leaves `p` alone. A struct held in a
  field is copied along with it.
- Writing a field of a record received `mut` changes the caller's record. Writing a record received by value changes only the op's own copy.
- The elements are sized numbers or `bool`. A list given to the field must match its element type and length (`E-TYPE-FIELD`).
- Seen as bytes (`view`, `encode`, `size_of`) it is laid out as in C: the array field sits inside the struct's bytes, aligned
  to its element. `len u8 . body array u8 4 . tail u16 .` is 8 bytes (`body` starts at byte 1, `tail` at byte 6).

A record bound with `let` has fields that cannot change either --- and that includes the elements of an array field.

#demo("examples/ch10/mistake_letfield.low")

== `enum` --- one of several

#idx("enum")
An `enum` is one of several variants. A variant can carry values, written as `<field name> <type>` pairs.

#demo("examples/ch10/shapes.low")

- `circle r u32 .` carries one value named `r`, and `rect w u32 h u32 .` carries two. `dot .` carries nothing.
- A value is made with the *variant name*, as in `shape.circle 2`.
- `case rect w h .` in a `match` binds the two carried values to `w` and `h` when the variant is `rect`.

However many variants there are, the `match` must cover them all. Leaving out `dot` is rejected.

#demo("examples/ch10/missing_case.low")

This check is the greatest value of an `enum`. When a `triangle` variant is added later, every `match` over `shape`, `area` and `corners`
included, stops at translation. Nobody has to remember the places to fix.

To ask only which variant without a `match`, `isa n num` gives a `bool`.

== Variants are closed with full stops

A newline is not a closer (#chref("surface")). So writing variants one per line without full stops runs several variants together into one.

#demo("examples/ch10/enum_dot.low")

`red green blue` nearly became one variant's name plus field pairs. The compiler rejects it and tells you to close each variant, as in
`red .`.

#misconception[An `enum` is a named integer, like C's enumerations][
  C's `enum` gives names to integer constants and mixes freely with integers. Lowent's `enum` is a *type*. It does not mix with integers,
  each variant can carry values of a different shape, and splitting it must cover every variant. It is closer to Rust's `enum` or the
  algebraic data types of the ML family.
]

== Nothing may contain itself

A variant that holds its own type *by value* would have infinite size.

#demo("examples/ch10/infinite.low")

The same goes for `struct`. Holding itself as a field, directly or through other types, is rejected with `E-STRUCT-CYCLE`. When you need a
tree-like structure, link it *by number* as the diagnostic suggests. Put the nodes in a slice and point at children by their position in
that slice.

#demo("examples/ch10/tree.low")

The `l` and `r` of `add l u32 r u32 .` are not nodes but positions in `nodes`. `eval` finds children by position and recurses. The size is
fixed, and the `index` that follows a position still gets its bounds check. The storage for such node slices --- regions --- is covered in
#chref("regions").

#qa[
  Isn't linking by number less convenient than linking by pointer?
][
  In one way it is. If a node is removed and its number reused, whoever held the old number sees the wrong node. The standard library's
  `pool` catches this at run time with *generational handles* that carry a generation count along with the number (#chref("lib-alloc")).
  What you gain is large: numbers are safe to copy, can be written straight to a file, and keep nodes together in one slice, which suits the
  cache.
]

== Common mistakes

#antipattern[Leaving a field out of `lit`][
  #demo("examples/ch10/mistake_missfield.low")

  C fills missing struct fields with 0, and some languages insert a default. Lowent rejects it with `E-TYPE-FIELD`, because the source
  cannot tell whether the missing field was intended or forgotten. If you want 0, write `y 0 .` --- a written 0 is obviously intended. A
  misspelt field name produces the same code, saying "no such field".
]

#antipattern[Passing a value-less variant as `Type.variant`][
  #demo("examples/ch10/mistake_unitvariant.low")

  A variant that carries values is built with the type name in front, as in `shape.circle 2`, and this book writes a variant carrying
  nothing by *its name alone* (`green`). Both are the same value --- `pick_bare` and `pick_qualified` both answer 2. Until 2026-09-16 the
  tool lowered a value-less variant written as `light.green` to a *different representation* (a record with a tag), so it stopped at run
  time with `E-VM-TYPE`. Now both lower to the same index. Prefer the shorter spelling, and qualify where the enum would otherwise be
  hard to tell.
]

#antipattern[Comparing two structs with `eq`][
  #demo("examples/ch10/mistake_eqstruct.low")

  `eq` compares numbers and booleans. What it means for two structs to be "equal" differs by type --- must every field match, or only an
  identifying one? So it is rejected at translation time with `E-TYPE-KIND`. Write down what equality means as an op.

  #demo("examples/ch10/eqstruct_fixed.low")
]

#antipattern[Believing that putting a value under another name makes a copy][
  #demo("examples/ch10/mistake_alias.low")

  `var q be point p .` builds *a new value with `p`'s fields copied*. So changing `q`'s field to 99 leaves `p` at 1. A value without
  ownership is copied; a value with ownership is moved (#chref("ownership")). Until 2026-09-16 the tool made an alias to the same place, and
  `p` changed too --- the `let` promise broke there. Lowering now copies the fields.

  #demo("examples/ch10/alias_fixed.low")

  This is also why `moved` in this chapter's first example returns a new value instead of changing a field. Making a new value
  rather than modifying one sidesteps aliasing altogether.
]

== This chapter's syntax at a glance

#dtable(
  columns: 3,
  id: "structs-glance",
  caption: [Struct and enum syntax --- shape · meaning · why it looks this way],
  [*Shape*], [*Meaning*], [*Why*],
  [`struct point do x u64 . y u64 . end`], [a bundle of named fields], [one line per field, name and type],
  [`lit point do x 1 . y 2 . end`], [build a value --- fill every field], [no field silently becomes 0],
  [`field p x` · `field s stop x`], [read a field · walk down several levels], [no glued dot --- the meaning is fixed as you read],
  [`set (field p x) 3 .`], [write a field (of a value received `mut`)], [reading and writing are spelt the same],
  [`body array u8 4 .` · `set (index (field p body) 0) 1 .`], [an array field --- the record holds the bytes], [copying copies the bytes too],
  [`enum shape do dot . circle r u32 . end`], [one of several --- variants may carry values], [each variant is closed with a stop],
  [`shape.circle 2` · `dot`], [build a variant that carries a value · one that carries none], [the variant name is the constructor],
  [`match s do case circle r . … end`], [split on variants and bind their values], [every variant must be covered],
  [`isa s circle`], [is it that variant (`bool`)], [for asking without taking values out],
  [trees linked by index (`l u32` · `r u32`)], [slice indexes instead of containing itself], [the size is fixed and indexes are bounds-checked],
)

#recap[
  A `struct` is made with `lit`, filling every field, and read and written with `field`. There is no glued dot. The variants of an `enum`
  can carry values, are made as `<type>.<variant>`, and are split with a `match` covering every variant. Variants are closed with full stops.
  A type holding itself by value has infinite size and is rejected; tree structures are linked by number.
]
