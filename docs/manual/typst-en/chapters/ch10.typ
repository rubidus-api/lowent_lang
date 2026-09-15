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
  You will learn to declare a `struct`, make one with `make` and read it with `field`, and why there is no glued dot. You will see that the
  variants of an `enum` can carry values, how to split them with `match` while binding those values to names, and why variants must be
  closed with full stops. You will also learn why a type that contains itself is rejected, and how to link tree structures by number.
]

#chapter-questions()

== `struct` --- a collection of named fields

#idx("struct")
A `struct` is a collection of named fields, each with its own type. The declaration's body is `do … end`, and each field is closed with a
full stop.

#demo("examples/ch10/points.low")

- `make point do x 1 . y 1 . end` makes a value. *Every field must be filled* when making it. No unfilled field quietly becomes 0.
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

To ask only which variant without a `match`, `isa n lit` gives a `bool`.

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

#recap[
  A `struct` is made with `make`, filling every field, and read and written with `field`. There is no glued dot. The variants of an `enum`
  can carry values, are made as `<type>.<variant>`, and are split with a `match` covering every variant. Variants are closed with full stops.
  A type holding itself by value has infinite size and is rejected; tree structures are linked by number.
]
