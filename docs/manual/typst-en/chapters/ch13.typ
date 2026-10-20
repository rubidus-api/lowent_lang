#import "../../typst-ko/lib.typ": *

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

`meters` is just another name for `u64`, so the result of `mul w. h. .` can be returned as a `u64`. Aliases shorten long types
(`type found option u64 .`) or leave meaning in the source. `pct` is an alias for a type with a range attached, and every place that takes this
name inherits the range (covered below).

#idx("newtype")
`newtype <name> <type> .` makes a *different type* with the same representation as an existing one.

#demo("examples/ch13/ids.low")

`user_id` and `order_id` are both represented as 64-bit numbers, but cannot be swapped. Such mistakes are hard to find with tests --- when the
numbers happen to coincide, the wrong order is fetched without a sound. Split them with `newtype` and translation catches it.

```text
 type meters u64 .                newtype user_id u64 .

   meters ──┐                     user_id ◀── cast ──▶ u64
            ├──▶ u64
   u64 ─────┘                     same representation, two types
   two names, one type            swapping them is E-TYPE-NOMINAL
   nothing stops a mix            every crossing shows as a cast
```

When you mean to cross over, write `cast`.

#demo("examples/ch13/ids_ok.low")

With only one or two crossing ops like `user_of` and `raw_of`, the places where a raw number enters as a `user_id` gather into those two.
Those are the only places that need checking.

#qa[
  Does a `newtype` cost anything at run time?
][
  No. The representation is the same, so in native code `user_id` is just a 64-bit integer. The distinction exists only at translation time,
  and `cast user_id n. .` does not change the value. It is a free distinction, worth using freely.
]

== `range` --- a contract that became the shape of a parameter

#idx("range")
Writing `range <low> <high>` in a parameter's type position makes that parameter accept only values between the two ends, inclusive.

#demo("examples/ch13/ranges.low")

The body of `scale` uses the fact that `a` is at most 100. So `mul a. 2 .` does not exceed 200, `narrow u8` cannot fail, and both overflow checks
are removed. Keeping to the range is the caller's responsibility. If the type of the value passed is already wider than the range (such as
`u64`), translation is refused; a value of a type that can fit, such as `u8`, is checked at the call. Values that come from *outside* the
program (here, `--run`'s arguments) are also checked at the boundary, and 101 stops there. The specification says translation is refused when
the caller *cannot prove* the range, but the compiler in this edition hands unproven `u8` values to a run-time check.

Who measures the range, and where, in one picture (the literal `101` was measured on this edition).

```text
 value the caller passes                         before entering scale
 42 (literal)     ── measured at translation ─▶  passes
 101 (literal)    ── measured at translation ─▶  E-TYPE-RANGE (refused)
 a u8 value       ── checked at the call ─────▶  stops if 101
 a --run argument ── checked at the boundary ─▶  stops if 101
 a u64 value      ── already wider ───────────▶  E-TYPE-WIDTH (refused)
```

The same could be written as `requires le a. 100 . .`. The difference is *where it is written*. `range` becomes the shape of the parameter, so
the caller sees it from the signature alone, and it can be carried into several ops through an alias (`type pct range 0 100 .`).

== `cast` --- where a value may change

#idx("cast")
`widen` is only for places where the value does not change. Conversions that change sign or cross kinds (floating point ↔ integer) are
written `cast <type> <value>`.

#demo("examples/ch13/casts.low")

- Converting floating point to integer truncates towards zero: 7.9 becomes 7, −2.9 becomes −2. Discarding the fraction does not stop.
- If the integer part or the value does not fit the target type, execution stops. Three billion does not fit in an `i32`.
- To turn a boolean into a number, use `cast`: `cast u8 b. .` is 1 for true and 0 for false (integer types only). There is no `cast` the
  other way --- write the question, as in `ne n. 0 .`.

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
- `view wire_header b. .` reads a byte slice as a value of that layout *without copying*. If length or alignment do not fit, execution stops.

A header holding `magic 1 · length 2 · kind 9` lies in bytes like this. `big` puts the high byte first.

```text
 byte:    0    1    2    3    4    5    6
        ┌────┬────┬────┬────┬────┬────┬────┐
        │ 00 │ 00 │ 00 │ 01 │ 00 │ 02 │ 09 │
        └────┴────┴────┴────┴────┴────┴────┘
         └───── magic ─────┘ └─ length ┘ kind
          u32 big            u16 big     u8
```

A field can also carry an access mark such as `rw`, `ro` or `wo`. In a struct that maps device registers, reading a write-only field is refused
at translation (#chref("hardware")). Pinning the layout takes choices away from the processor, so pin it only where needed.

== Both directions of a layout, and a view that can fail

`view` *reads* bytes as a value of that layout. The opposite direction --- *making* bytes of that layout from a value --- is `encode`.

#demo("examples/ch13/encoded.low")

`encode wire_header h. .` produces seven bytes in the byte order written on each field (`big`). The result 7009 puts the length 7 and the 9 of
the last byte, `kind`, side by side. The side that builds a header going onto the wire uses `encode`; the side that reads a received header
uses `view`.

```text
 bytes 00 00 00 01 00 02 09 ── view · try_view ──▶ a wire_header value
 (wire · file · device)     ◀──── encode ───────── magic 1 · length 2 · kind 9
```

`view` *stops* when the length or alignment is off, because it treats that as a broken contract. Bytes from a network, however, are often
short. In such places use `try_view`, which does the same work but reports failure as a value (#chref("errors-design")).

#demo("examples/ch13/tryview.low")

With seven bytes it reads `kind` 9; with three it gets `none` and returns 0. Use the stopping `view` inside, where things are already checked,
and the value-reporting `try_view` at the boundary.

== What a struct view looks at

A view does *not copy* the bytes. Reading a field through the name that holds a view reads the bytes in that place; writing a field writes
them. It is the same thing as turning the address of a byte array into a struct pointer in C, except that the length and the alignment are
checked when the view is laid on, and the translation works out what may be written. The program below shows the difference in four shapes.

#demo("examples/ch13/structview.low")

```text
 b:  ┌────┬────┐            var h pt view pt b. .
     │ 01 │ 02 │  ◀──────── h.x  h.y      (the same bytes)
     └────┴────┘
       set field h. y . 9 .   →   b = [01, 09]

     let h pt view pt b. .  ·  var k pt h. .
       k = { x 1 · y 2 }      (a copy — it lives apart from b)
       set field k. y . 7 .   →   b = [01, 02] unchanged
```

- `write_through` --- writing a field of the view turns `b` into `[1,9]`. A view is not a copy.
- `copy_by_name` --- binding the name that holds a view to another name *copies the struct value*. Writing that copy leaves `b` as it was.
  The usual rule --- a struct value is copied when it is bound to a name --- holds for views too.
- `pass_mut` --- handed to an op that takes `mut pt`, the callee's write reaches `b`. A `mut` input passes the place, not a copy.
- `replace_whole` --- giving the name a whole new struct, as in `set h. <new value> .`, makes `h` let go of the bytes and hold the new value.
  `b` does not change. To rewrite the bytes whole, `set field` each field, or `copy` in the bytes `encode` built.

Two views laid on the same bytes see each other's writes. The `some_value` of the `option` that `try_view` gives is the same bytes, and so
is the result of an op that returns a view of its input. A name bound with the type word `view wire_header` (in `tryview.low` above) is
«an eye on the bytes», so binding it to another name does not copy --- the new name looks at the same bytes. A name bound with the struct
name `wire_header` copies, as `copy_by_name` does.

=== A view inherits whether its bytes may be changed

Whether a view can write is decided by *the bytes under it*. A slice received read-only, a slice bound without `mut` and a string literal
are bytes that cannot be written. Binding a view of them to a `var` is refused with `E-TYPE-MUT`, handing it to a `mut` input with
`E-TYPE-ARGMUT`. To only read, bind it with `let`. A struct bound with `let` cannot have its fields changed (`E-IMMUTABLE`), so the bytes
under it are safe too.

A `fn` cannot write through a view of bytes it took as `mut` either. That write is visible to the caller, so it breaks the `fn`'s purity
(`E-EFFECT-PURITY`). An op that changes bytes through a view is written as a `proc`.

=== Which structs a view can be laid on

A view reads the bytes as they are, so the struct must have a *fixed byte layout*, and every field must be a meaningful value whatever
bytes land in it.

#dtable(
  columns: 3,
  id: "structview-fields",
  caption: [fields a struct view can be laid on],
  [*field type*], [*view*], [*why*],
  [`u8`…`u64` · `i8`…`i64` · `f32` · `f64`], [yes], [fixed width, and every bit pattern is a value],
  [a fixed array of those · a struct of only such fields], [yes], [the layout sits inside the struct as it is],
  [`bool`], [`try_view` only --- `view` is `E-TYPE-BITCAST`], [a byte other than 0 or 1 would be neither true nor false --- `try_view` checks it],
  [an enum with a width (`layout u8 .`)], [`try_view` only --- `view` is `E-TYPE-BITCAST`], [a number no variant has may arrive --- `try_view` checks it],
  [an enum without a width · slice · `option` · owned handle], [no --- `E-IR-UNDEF`], [no fixed bytes (such a struct cannot be `encode`d either)],
)

You can measure why a `bool` field is a problem. Before it was refused, a field that read byte 2 was true to `if`, false to `eq … true`, and
false to `not` as well. It is the same reason `bit_cast bool` is refused (#chref("fixed-memory")). Laying such a struct, or `bool`, with
`view_array` is refused for the same reason. `encode`, which writes a value down into bytes, has no such limit --- it writes its own value.

=== `try_view` checks the fields

To read bytes from outside as a `bool` or an enum, use `try_view`. Besides the length and the alignment, it checks those fields: a `bool`
byte other than 0 or 1, or an enum number that no variant has, gives `none`. Fields of a nested struct are checked too.

To put an enum in bytes, write its *width* in the enum declaration --- one line, `layout u8 .` (#chref("structs-enums")). The bytes hold the
variant's number: reading gives the variant with that number, writing puts the variant's number. The compiler does not pick the width
itself, because adding one variant must not silently change a layout that crosses a wire.

#demo("examples/ch13/trycheck.low")

`kind_of` reads the 7 in byte 0 as `data` and returns 2; at 2, which is no variant's number, it gets `none` and returns 0. `active_of` does
not take byte 2 as a `bool` --- it gets `none`. If the bytes underneath change by another path after the check, so that a field holds no
variant's number, reading that field stops.

The length and alignment conditions are those of the previous section. `view` stops when they fail (`E-VM-VIEW` · `E-VM-ALIGN`);
`try_view` gives `none`.

== `bitset` --- a set of small numbers

#idx("bitset")
`bitset <bits>` is a set recording *whether each number* from 0 up to (not including) that count is present. Its name resembles `bits` (the
width of one integer), but the meaning differs: `bitset` is not a tool for the bits of a word but a set, and the bits of a word are handled by
bit operations such as `bit_and` and `shl`.

#demo("examples/ch13/sets.low")

- `bitset_new 64 .` makes an empty set. The width is a number fixed at translation time.
- `bitset_insert a 1 .` inserts and `bitset_remove a 1 .` removes. Both are statements that change the set *in place*. `bitset_count a. .` is the number of members.
- `bitset_intersect a. b. .` gives the intersection, `bitset_difference a. b. .` what is only in `a`, and `bitset_complement a. .` the complement, each *as a new set*.
- `bitset_is_subset x. y. .` asks whether all of `x` is in `y`; `bitset_is_empty x. .` asks whether it is empty.

In `overlap 5` the intersection is {3, 5} and what is only in `a` is {1}, giving 211. `bitset_complement` only means something within the width: put
0 into an eight-slot set and its complement is the other seven. Inserting or asking about a number outside the width stops the program ---
the range of the set is part of its type too.

== Scattered pieces as one view --- `segments`

Sometimes bytes do not lie in one row but are scattered over several pieces, like pieces received from the network or the two ends of a ring
buffer. Gathering them into one place means copying. `segments` views the pieces as one without copying.

#demo("examples/ch13/pieces.low")

- `view_segments back. d. .` binds the backing bytes `back` and the descriptor `d` into a view of type `segments u8`. The descriptor is a row of
  (start, length) pairs; here 4 bytes from 0 and 4 bytes from 8.
- From the standard module `segview`, `segview.piece_count ss. .` gives the piece count 2, and `segview.piece_at ss. 1 .` gives the second piece as an
  ordinary `slice u8`. Byte 1 of the second piece is the 30 at original position 9.
- There is no new machine instruction. Building lowers to building a grouping, and taking a piece to reading fields and slicing, so the cost is visible.

== Three kinds of type words

The words that may stand in a type position form a closed list set by the canon. Some of them are known by name only and have no meaning yet.

#demo("examples/ch13/notyet.low")

`byte` is accepted, but `W-NOT-YET` says "no meaning". Accepting it silently would make the writer think it works. Write a byte as `u8`.

#dtable(
  columns: 3,
  id: "named-types-words",
  caption: [Type words --- usable · known by name · refused],
  [*Kind*], [*Words*], [*What the tool does*],
  [usable], [numbers · `bool` · `void` · `slice` · `array` · `segments` · `set` · `stack` · `range` · `vec` · `bitset` · `mask` · `result` · `option` · `ref` · `mut_ref` · `mut` · `owned` · `region` · `cap` · `self`], [checks and lowers them by meaning],
  [known by name], [`byte` · `char` · `str` · `string` · `bytes_view` · `dyn` · `atomic`], [`W-NOT-YET` --- says there is no meaning],
  [refused for now], [`shared_read` · `lock` · `rwlock`], [`E-LOCK-NOTYET` --- state shared between flows (#chref("tasks-channels"))],
  [qualifier], [`unsafe_ptr`], [a C pointer mark placed before a type (#chref("ffi"))],
  [no meaning in the canon], [`list` · `raw` · `addr` · `rng`], [this edition's tool accepts them without a warning --- do not use them],
)

The last row is a hole in this edition. The four words are in the canon's list, but no clause gives them a meaning, and the tool accepts them
without a word. It is recorded as a defect in the development repository.

== Common mistakes

#antipattern[Passing a value of a wider type straight to a `range` parameter][
  #demo("examples/ch13/mistake_rangewide.low")

  `scale` says in its signature that it takes only 0 … 100. A `u64` value is wider than that, so unless the caller shows it is in
  range, translation rejects the call with `E-TYPE-WIDTH`. The point of rejecting is to settle who is responsible: only the caller
  knows what to do with an out-of-range value. Check the range with `guard`, answer separately when it is outside, and pass the
  value on with `narrow` when it is inside.

  #demo("examples/ch13/rangewide_fixed.low")
]

#antipattern[Using `widen` to put a signed number into an unsigned type][
  #demo("examples/ch13/mistake_widensign.low")

  Going from `i32` to `u64` makes the width larger, but a negative number such as −1 has no place in `u64`. `widen` is only for
  places where *no value changes*, so this is `E-WIDEN-SIGN`. If you know no negative value can arrive, write `cast u64 x. .` to leave
  that judgement in the source; `cast` stops if a negative value does arrive. If negatives need their own handling, put
  `guard ge x 0` first.
]

#antipattern[Naming a slice, an array or a matrix with `type` or `newtype`][
  #demo("examples/ch13/mistake_rowname.low")

  `slice`, `array` and `matrix` are types whose element type, length and dimensions are spelled out in the words themselves. Give one
  another name and the reader has to find the declaration to learn what is behind it. So `type` and `newtype` do not take any of
  the three *as the whole type* --- a slice is rejected with `E-TYPE-ROWNAME`, an array with `E-TYPE-ARRAY`, a matrix with
  `E-MATRIX-USE`. Write the type where it is used. One *inside* another type may be named (`type found option slice u8 .`). When a
  type with a name of its own is needed, make it a struct field --- a struct with a different name is a different type.

  #demo("examples/ch13/rowname_fixed.low")
]

#antipattern[Turning a number into a `bool` with `cast`][
  #demo("examples/ch13/mistake_boolcast.low")

  C treats any non-zero number as true, but which values of a number count as true is decided by the question asked at that place. So
  `bool` cannot be the target of `cast`, and it is rejected with `E-TYPE-KIND`. Write the question, as in `ne n. 0 .`; that one line puts
  the meaning in the source. The other direction works --- `cast u8 b. .` is 1 for true and 0 for false.
]

#antipattern[Binding a view of bytes received read-only to a `var`][
  #demo("examples/ch13/mistake_viewro.low")

  A view is not a copy, so writing a field of `h` writes `b`. But `b` was received read-only --- the caller trusts that this op does not
  change its bytes. So it is refused with `E-TYPE-MUT`. To only read, bind it with `let`; to change it, take the bytes as `mut`. Then the
  caller sees that in the signature.

  #demo("examples/ch13/viewro_fixed.low")
]

#antipattern[A `bool` field in a struct read through a view][
  #demo("examples/ch13/mistake_viewbool.low")

  Bytes from outside can hold any value, not just 0 and 1. If a `bool` field gets 2, it would be neither true nor false, so it is
  `E-TYPE-BITCAST`. To sort out a wrong input, lay it with `try_view` (the section above --- a 2 gives `none`). If 2 should count as true,
  take the field as `u8` and write the question. Then whether 2 counts as true or as a wrong input is left in the source.

  #demo("examples/ch13/viewbool_fixed.low")
]

#misconception[Giving a type another name with `type` keeps units from mixing][
  #demo("examples/ch13/type_units.low")

  `meters` and `feet` are both just *other names* for `u64`, so nothing stops you adding them: 100 meters plus 100 feet comes out as
  200 meters. Meanings that must not mix are separated with `newtype`; then, as in `ids.low`, translation stops the mix with
  `E-TYPE-NOMINAL`, and every crossing shows up as a `cast`. Use `type` to shorten long names or to hand down a range.
]

#misconception[`cast` of a negative number into an unsigned type gives a big number, as in C][
  #demo("examples/ch13/negcast.low")

  In C, `(uint32_t)-1` is 4294967295. That wrap-around is defined by the standard, but it usually hides a bug. Lowent's `cast` moves
  the value and stops with `E-VM-CAST` when it does not fit. If you really want wrapping, use a word that carries that meaning in its
  name, such as `narrow_wrap` (#chref("numbers")).
]

== This chapter's syntax at a glance

#dtable(
  columns: 3,
  id: "named-types-glance",
  caption: [Named type syntax --- shape · meaning · why it looks this way],
  [*Shape*], [*Meaning*], [*Why*],
  [`type meters u64 .`], [another name for the same type], [shortens a type and records meaning --- does not stop mixing],
  [`newtype user_id u64 .`], [a new type with the same representation], [translation stops ids from mixing --- no run-time cost],
  [`cast user_id n. .` · `cast u64 u. .`], [cross between the new type and the original], [crossings gather in one place in the code],
  [`input a range 0 100 .`], [accept only values in the range, both ends included], [the contract becomes the shape of the signature],
  [`type pct range 0 100 .`], [put a range on an alias], [many ops inherit the same range],
  [`cast i32 x. .`], [a conversion that may change the value --- stops if it does not fit], [a mark that says "I know the value may change here"],
  [`type ten_bits bits 10 .`], [an integer of 1 … 64 bits], [the width is the contract],
  [`layout packed .` · `magic u32 big .`], [layout without padding · byte order], [make bytes mean the same outside],
  [`view wire_header b. .`], [read bytes in that layout without copying], [stops if length or alignment is off],
  [`try_view wire_header b. .` · `encode wire_header h. .`], [a view that gives `none` on failure · a value into bytes of that layout], [at a boundary, the non-stopping one],
  [`var h pt view pt b. .` · `let h pt view pt b. .`], [a name on the bytes (a field write changes them) · a read-only name], [`var` is refused when the bytes cannot be written],
  [`var a bitset 64 bitset_new 64 . .` · `bitset_insert a 1 .` · `bitset_intersect a. b. .`], [a set of small numbers and its operations], [a set, not the bits of a word],
  [`view_segments back. d. .` · `segview.piece_count ss. .` · `segview.piece_at ss. i. .`], [scattered pieces as one view without copying · piece count · piece i], [removes the gathering copy],
  [type words such as `byte` · `lock`], [`W-NOT-YET` · `E-LOCK-NOTYET`], [if there is no meaning, the tool says so],
)

#recap[
  `type` is an alias and `newtype` a new type with the same representation; `cast` crosses between them. `range lo hi` is a contract that has
  become the shape of a parameter: the caller keeps it and the body uses it as a fact. `cast` is the word for conversions that may change the
  value and stops when the value does not fit. `bits N` is an arbitrary-width integer. `layout packed`, byte-order marks and `view` match byte
  layout with the outside world.
]
