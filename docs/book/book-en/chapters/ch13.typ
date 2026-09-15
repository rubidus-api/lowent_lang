#import "../../book/lib.typ": *

= Named types --- `type`, `newtype`, `range`, `cast`

#chapter-toc()

#prereq(
  ([#chref("numbers"), Numbers], [widening is automatic, narrowing is written]),
  ([#chref("structs-enums"), Aggregates], [`struct` and `field`]),
)

#deepqa[
  #chref("numbers", cap: true) said `usize` and `u64` do not convert automatically even on machines where they have the same width. Why?
][
  To make places mixing address counts with plain numbers visible in the source. When the meaning differs, the type differs even if the
  representation is the same, and crossing over has to be written. This chapter covers how users apply the same principle themselves ---
  `newtype` --- and how to write ranges, widths and layouts into types.
]

#why[
  If user numbers and order numbers are both `u64`, swapping them goes unnoticed by the compiler. So does passing 300 to a parameter meant for a
  percentage. These defects arise because a type says only its *representation* and not its *meaning*. As the last chapter of Part III, it
  gathers the ways to attach names, ranges and layouts to types. It sits right before contracts (Part IV) because `range` has the same standing
  as a contract.
]

#organizer[
  You will learn that `type` is another name for the same type and `newtype` a different type with the same representation, and that `cast`
  crosses between them. You will pick up what `range lo hi` in a parameter position gives the caller and the body, and how `cast` differs from
  `widen` and `narrow`. You will also see `bits` integers from 1 to 64 bits, and `layout packed` and `view`, which pin down byte layout.
]

#chapter-questions()

== `type` is an alias, `newtype` a new type

`type <name> <type> .` gives an existing type another name. The two names are the same type and interchangeable.

#demo("examples/ch13/aliases.low")

`meters` is just another name for `u64`, so the result of `mul w h` can be returned as a `u64`. Aliases shorten long types
(`type bytes slice u8 .`) or leave meaning in the source. `pct` is an alias for a type with a range attached, and every place that takes this
name inherits the range (covered below).

A type declaration has no `be`.

#demo("examples/ch13/type_be.low")

`be` is the word with which `let` and `var` bind *values*. What is bound here is a type. One meaning, one spelling.

#idx("newtype")
`newtype <name> <type> .` makes a *different type* with the same representation as an existing one.

#demo("examples/ch13/ids.low")

`user_id` and `order_id` are both represented as 64-bit numbers, but cannot be swapped. Such mistakes are hard to find with tests --- when the
numbers happen to coincide, the wrong order is fetched without a sound. Split them with `newtype` and translation catches it.

When you mean to cross over, write `cast`.

#demo("examples/ch13/ids_ok.low")

With only one or two crossing ops like `user_of` and `raw_of`, the places where a raw number enters as a `user_id` gather into those two.
Those are the only places that need checking.

#qa[
  Does a `newtype` cost anything at run time?
][
  No. The representation is the same, so in native code `user_id` is just a 64-bit integer. The distinction exists only at translation time,
  and `cast user_id n` does not change the value. It is a free distinction, worth using freely.
]

== `range` --- a contract that became the shape of a parameter

#idx("range")
Writing `range <low> <high>` in a parameter's type position makes that parameter accept only values between the two ends, inclusive.

#demo("examples/ch13/ranges.low")

The body of `scale` uses the fact that `a` is at most 100. So `mul a 2` does not exceed 200, `narrow u8` cannot fail, and both overflow checks
are removed. Keeping to the range is the caller's responsibility. If the type of the value passed is already wider than the range (such as
`u64`), translation is refused; a value of a type that can fit, such as `u8`, is checked at the call. Values that come from *outside* the
program (here, `--run`'s arguments) are also checked at the boundary, and 101 stops there. The specification says translation is refused when
the caller *cannot prove* the range, but the compiler in this edition hands unproven `u8` values to a run-time check.

The same could be written as `requires le a 100 .`. The difference is *where it is written*. `range` becomes the shape of the parameter, so
the caller sees it from the signature alone, and it can be carried into several ops through an alias (`type pct range 0 100 .`).

== `cast` --- where a value may change

#idx("cast")
`widen` is only for places where the value does not change. Conversions that change sign or cross kinds (floating point ↔ integer) are
written `cast <type> <value>`.

#demo("examples/ch13/casts.low")

- Converting floating point to integer truncates towards zero: 7.9 becomes 7, −2.9 becomes −2. Discarding the fraction does not stop.
- If the integer part or the value does not fit the target type, execution stops. Three billion does not fit in an `i32`.
- A `bool` can be neither the target nor the source of a `cast`. To go between booleans and numbers, write an `if`.

`cast` is not a mark meaning "anything goes". It is a mark meaning *I know the value may change here*. It does not break the principle that no
value-losing conversion happens implicitly (#chref("numbers")), because the author wrote the word.

#misconception[`cast` moves the bits unchanged, like a C cast][
  C's `(int32_t)x` truncates or wraps values that do not fit, depending on the implementation. Lowent's `cast` moves the value and stops when it
  does not fit. Keeping the bits and changing only how they are read is a separate job, done by `bit_cast` (#chref("fixed-memory")). Two jobs do
  not share one word.
]

== `bits` --- from 1 to 64 bits

`type <name> bits <count> .` makes an integer type with that many bits. The width need not be a power of two.

#demo("examples/ch13/tiny.low")

`ten_bits` is 0 … 1023. Adding 1 to 1023 overflows ten bits and stops. When dealing with protocol bit fields or packed tables, the width becomes
the contract. But when the width does not match the machine's word, there is no promise of speed. What the language promises is the *correct*
answer computed in ten bits.

== Pinning down byte layout

A struct's layout is normally chosen by the processor to suit the machine. When bytes must *mean the same outside* --- a header going onto the
wire, a register a device reads --- write the layout.

#demo("examples/ch13/packed.low")

- `layout packed .` puts no padding between fields. Fields sit next to each other in declaration order.
- `big` and `little` after a field set its byte order. If not written, the machine's order is used.
- `view wire_header b` reads a byte slice as a value of that layout *without copying*. If length or alignment do not fit, execution stops.

A field can also carry an access mark such as `rw`, `ro` or `wo`. In a struct that maps device registers, reading a write-only field is refused
at translation (#chref("hardware")). Pinning the layout takes choices away from the processor, so pin it only where needed.

#recap[
  `type` is an alias and `newtype` a new type with the same representation; `cast` crosses between them. `range lo hi` is a contract that has
  become the shape of a parameter: the caller keeps it and the body uses it as a fact. `cast` is the word for conversions that may change the
  value and stops when the value does not fit. `bits N` is an arbitrary-width integer. `layout packed`, byte-order marks and `view` match byte
  layout with the outside world.
]
