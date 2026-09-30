#import "../../typst-ko/lib.typ": *

= Sequences --- arrays and slices

#chapter-toc()

#prereq(
  ([#chref("control"), Flow], [`for` walks a slice, and `guard` turns a condition into a fact]),
  ([#chref("ops"), Ops], [writes visible to the caller need a `proc`]),
)

#deepqa[
  In #chref("control")'s `head_or_zero`, why was `index data 0` safe?
][
  Because the `guard ge (len data) 1 . else return 0 .` right before it handed the code below the fact that the slice was not empty. Code
  after a `guard` lives only in a world where the condition is true. This chapter covers what that slice is, and when bounds checks remain
  and when they disappear.
]

#why[
  The most expensive defects in C come from people remembering separately how many items are at the address a pointer points to, and
  getting it wrong. That is the buffer overflow. Lowent has no pointers; pointing at several values is the job of a slice, which *carries its
  length with it*. Slices open Part III because nearly all data --- strings, buffers, file contents --- travels as slices.
]

#organizer[
  You will learn the difference between `array t n` and `slice t`, how to read with `len`, `index` and `for`, and that out-of-range access
  stops. You will pick up the rule that writing elements requires a `mut slice`, and how to narrow a window with `subslice`. You will also
  see the principle by which contracts remove bounds checks in the body, and that a string literal is a table that can be indexed directly.
]

#chapter-questions()

== Start and length together

#idx("slice")
`slice t` is a contiguous run of values of type `t`. A slice value carries *its start address and its length* together, so nobody has to
remember separately "how many are here".

#demo("examples/ch09/basics.low")

- `len data` is the number of elements, and `index data 0` is the first element. Numbering starts at 0.
#idx("array")
- `array u8 4` in `last4` is a sequence of exactly 4. An `array` in an input position means it receives a slice of length 4, and that length
  is checked on entry, like `requires eq (len xs) 4 .`.
- `for x xs do … end` walks the elements in order.
- Passing a slice of length 3 and 5 to `at` stops with `E-VM-BOUNDS`. It does not read someone else's memory.

An `array` writes the element type *first* and the length *after* it --- the same order as `vec u32 4`. The reverse order is rejected.

#demo("examples/ch09/array_order.low")

The earlier tool read a wrongly ordered `array` as a plain slice and silently dropped the length --- a place where a written
promise vanished. Today `array` is used only in op input positions; in outputs, locals and struct fields, write `slice` with a contract.

#qa[
  Is there no separate string type?
][
  There is not. A string literal `"hello"` is a `slice u8`. Questions such as how many characters it has or whether it is valid UTF-8 are
  answered by library ops, not by a type (#chref("lib-text")). It is a choice to avoid the defects that start the moment bytes and
  characters are treated as the same thing.
]

== Writing needs `mut slice`

To write the elements of a slice, the slice must be `mut`. And changing bytes the caller passed is visible to the caller, so it must be a
`proc` (#chref("ops")).

#demo("examples/ch09/fill.low")

The `fill_two([10,20,5])` the VM shows is the argument's state after the op ended; the caller's bytes changed. Writing without `mut` is
rejected.

#demo("examples/ch09/readonly.low")

`mut` is a permission on the slice's *elements*. It is a different question from whether the name holding the slice was made with `let`
(#chref("locals")). It is followed by the rule that a slice whose elements can be written may be borrowed in only one place at a time
(#chref("references")).

== Narrowing the window

`subslice s from to` makes a new slice pointing from position `from` of `s` up to, but not including, `to`. It copies no bytes; it just
places another, narrower window over the same bytes.

#demo("examples/ch09/windows.low")

`middle_sum` sums the middle elements, leaving out both ends. `prefix_char` indexes a string literal directly, because a literal is a
`slice u8`.

#realcase[From an `if` chain to one table][
  An op returning a constant per index is easy to write as a chain like `if eq k 0 . do return 47 . end`. The emitted C then has one
  comparison and branch per character. Indexing a string literal becomes one static table and one index. In a benchmark that reads HTTP
  requests, replacing such a chain with a table lookup brought branch mispredictions down to the level of hand-written C and cut run time by
  about 25%. The answer did not change by a single bit.
]

#demo("examples/ch09/table.low")

The two ops give the same answer. Looking with `--emit-c`, `prefix_char` is one string table and one index, while `prefix_chain` is a chain of
comparisons and `goto`s. In this edition, though, the index check in `prefix_char` remains --- the analysis does not connect the literal's length
11 with the contract `lt k 11`.

== Writing a sequence directly --- list literals

When you need a sequence written out as values, list it with `lit`. `lit array u32 5 2 3 5 7 11 .` is an
`array u32 5` value written as element type, length, then the elements; `lit slice u32 4 5 6 .` is a slice value
with no length written (the element count is the length). A list *closes with its own period*. At the end of a
statement the list's period and the statement's period come together (`. .`); in the middle of a call one period
closes the list and the next argument follows.

#demo("examples/ch09/listlit.low")

If there are fewer elements than the length, end the list with `_` to say "the remaining cells are zero". Without
it the list is `E-LIT-COUNT` --- cells are never filled silently. Each element must fit its type (`E-TYPE-WIDTH`).

A `let` list whose elements are all constants is read-only bytes baked into the program. Passing that name to a
`mut` position is refused (`E-TYPE-ARGMUT`). When you need a sequence you can change, bind it with `var`. A list
bound with `var`, and a list with elements computed at run time, live *in this op's frame* --- like a local array
in C. Write a cell with `set (index buf i) v .`.

#demo("examples/ch09/framelist.low")

A frame slot lives until the end of *the block that declares it*. When the declaration runs again (the next pass
of a loop), the slot is filled afresh. So a slice that sees the slot may not leave that block --- returning it,
storing it in a name declared further out or in a parameter's cell, or sending it to an actor is refused
(`E-LIT-ESCAPE`). Reading a cell out as a scalar is fine.

#demo("examples/ch09/mistake_frameescape.low")

Re-pointing the array name itself with `set buf …` is `E-ARRAY-SET` --- write the cells one by one. The frame
lists of one op may not add up to more than 64 KiB (4 KiB on a small bare-metal machine) (`E-FRAME-SIZE`). An op holding frame lists
may not recurse (`E-FRAME-RECURSIVE`) --- the frame would repeat at every level, and the depth is not known at translation. If you need a
buffer inside a recursion, take it from an allocator (#chref("fixed-memory")). And if the lists alive on the stack at once along a call
path exceed the budget (4 MiB), that is `E-STACK-BUDGET`. An op that runs as a task (`spawn`) runs on a small stack, so its budget
is 64 KiB. Running out of stack is ruled out before the program runs. `lowentc --stack-report <file>` prints, for every entry op and
task, the list bytes on its deepest path and its budget.

When only a few cells of a long array differ, fill the cells you *choose*. Inside `do … end` write `<index> <value> .`,
and a final `_ <value> .` fills every remaining cell. Every cell must be decided, so a leftover cell without `_` is
`E-LIT-COUNT`. An index is a constant inside the length, written once (`E-LIT-INDEX`). The block closes the list, so
the list has no period of its own.

#demo("examples/ch09/cellfill.low")

Values may be expressions and are computed in the order written.

The elements may be structs too, as long as the struct has a byte layout --- every field a sized number.

#demo("examples/ch09/structlist.low")

- Each element is a *copy* of the value given. Changing a field in the list after putting `p` in it leaves `p` as it was.
- An element's field is written as `set (field ps 2 y) 100 .` --- the list's name, the position, then the field.
- Cell fills (`do <index> <value> . _ <value> . end`) and `lit slice pt …` work the same way. A `let` list of only `_` is read-only zeros.
- A struct with a slice, `owned` or array field cannot be an element yet (`E-LIT-UNBUILT`). A number where a struct goes is
  `E-TYPE-FIELD`, and a different struct is `E-TYPE-STRUCT`.

When the elements are fixed-length arrays, the list is a *list of rows*. Its type is `slice (array u8 4)`.

#demo("examples/ch09/rows.low")

- `len g` counts rows (3), and `index g 1` is a `slice u8` that *views* the second row. It is not a copy, so writing to
  `row` changes `g`.
- A cell is written through its row: `set (index (index g 2) 3) 9 .`. Replacing a whole row with `set (index g 1) …` is
  `E-TYPE-SET`.
- A row value must have the same shape (`lit array u8 4 …`). A row of a frame list views the frame's bytes, so returning
  it is `E-LIT-ESCAPE`.

== Contracts remove bounds checks

An `index` bounds check remains only where it is not proven. Write the length condition as `requires`, and the check happens *once* on
entry to the op while the index checks in the body disappear.

#demo("examples/ch09/bounds.low")

With `requires le n (len a) .`, `i` inside `while lt i n .` is always less than `len a`. The compiler's interval analysis works that out and
removes the check in `index a i`. The rules behind this reasoning are proven in Coq, and an independent checker re-verifies the arithmetic
evidence the compiler leaves at every removed check (#chref("proofs-bounds")).

One thing to watch: a count `n` uses `le`, but an *index* itself uses `lt`. `requires le i (len a) .` allows `i = len a`, which is one past
the end.

#misconception[Putting `len` in a loop condition recounts every time][
  `len s` is neither a call nor a memory read. It just takes the length field out of the slice value `{start, length}`. Writing elements
  does not change the length; the only way the length changes is putting a different slice into the name. In fact, storing the length ahead
  as `let m be u64 len xs .` and then replacing `xs` with a shorter slice leaves `m` as a *stale length* that can go out of range.
  Storing it ahead is a choice of meaning, not an optimisation.
]

#demo("examples/ch09/stale_len.low")

`fresh` reads the current length each time round, so when `s` shrinks to two elements the loop stops there too. `stale` stored the first length 4
in `m`, so on the third round it tries to read element 2 of the two-element `s` and stops. `len` reads the current value every time, and that is
what keeps it safe.

#qa[
  Can `[3,4,5]` given to `--run` be used for a `slice u64`?
][
  Bracketed `--run` arguments are sequences of bytes. That is why this book's runnable examples take slices as `slice u8`. Passing one to a
  `slice u64` stops the VM, because the byte count is not a multiple of 8. Slices of wider elements are usually built inside the program and
  passed along.
]

== Common mistakes

Almost every slice mistake comes down to *being off by one*. Where C would read someone else's memory and carry on, Lowent stops.

#antipattern[Looping one step too far with `le`][
  #demo("examples/ch09/mistake_offbyone.low")

  A slice of length 3 has indexes 0, 1 and 2. `le i (len xs)` is still true when `i` is 3, so reading the fourth cell stops with
  `E-VM-BOUNDS`. Because indexes start at 0, the right condition is "less than the count" --- `while lt i (len xs) .`. If you are walking
  every element, `for x xs do` uses no index at all and removes the mistake completely.
]

#antipattern[Reading with brackets, as in `xs[0]`][
  #demo("examples/ch09/mistake_cindex.low")

  There is no bracket indexing. Reading an element is `index xs 0`, writing it is `set (index xs 0) v .`. Using names instead of symbols
  makes reading, writing and the out-of-range stop all the same shape of form, with fewer symbols to remember.
]

#antipattern[Reading the first element of what may be an empty slice][
  #demo("examples/ch09/mistake_empty.low")

  It is easy to forget that an input may be empty. Index 0 exists only when there is at least one element. Filter first, then read.

  #demo("examples/ch09/empty_fixed.low")

  Below the `guard`, "not empty" is a fact, so `index xs 0` is safe --- and the compiler uses the same fact to remove the bounds check
  (#chref("control")).
]

#antipattern[Swapping the start and end of `subslice`][
  #demo("examples/ch09/mistake_subrev.low")

  `subslice s from to` needs `from ≤ to ≤ len s`. Swapping them does not give an empty window; it stops --- because a miscalculated boundary
  that quietly became an empty result would only surface much later.
]

#misconception[`subslice s 1 3` is the three cells from 1 to 3][
  #demo("examples/ch09/subslice_end.low")

  The end index is *not included*: indexes `1` and `2`, two cells. With this rule the length is simply `to − from`, and `subslice s 0 k` and
  `subslice s k (len s)` split the slice with no cell overlapping or missing. That is why most languages use the same rule (a half-open
  range).
]

#antipattern[Taking a string literal as `mut slice` and changing it][
  #demo("examples/ch09/mistake_litwrite.low")

  A string literal is bytes baked into the program, not a place to change. So binding it to a name declared `mut`, or passing it where
  a `mut` place is taken, is refused with `E-TYPE-ARGMUT`. Until 2026-09-16 both passed, and when run the VM returned 65 while native
  code returned 97 --- the same program with two answers --- and a standard-library op writing those bytes killed the native build with
  a segmentation fault. Take bytes you will change from `alloc_bytes` or from the caller's buffer, and copy the literal into them.
]

== This chapter's syntax at a glance

#dtable(
  columns: 3,
  id: "slices-glance",
  caption: [Slice syntax --- shape · meaning · why it looks this way],
  [*Shape*], [*Meaning*], [*Why*],
  [`slice u8`], [a run of `u8` values (start + length)], [no separate count to carry around],
  [`mut slice u8`], [a run whose elements may be written], [writability is visible in the type],
  [`input xs array u8 4 .`], [take a run of exactly 4 (input position only)], [element type, then length --- checked at entry],
  [`len xs`], [number of elements], [just reads the length field; no cost],
  [`index xs i`], [element `i` (from 0)], [stops when out of range --- never reads someone else's memory],
  [`set (index xs i) v .`], [write element `i`], [needs a `mut slice` and a `proc`],
  [`for x xs do … end`], [each element in turn], [no room for index mistakes],
  [`subslice xs from to`], [a window from `from` up to, not including, `to` (no copy)], [half-open --- the length is `to − from`],
  [`"hello"`], [a literal of type `slice u8` --- indexable as is], [there is no separate string type],
  [`lit array u32 3 1 2 3 .`], [an array written as values --- closes with its own period], [the length is a promise --- end with `_` if short],
  [`lit slice u32 4 5 .`], [a slice written as values], [the element count is the length],
  [`var buf be lit array u8 8 _ . .`], [a writable local array --- in the op's frame], [cannot leave its declaring block],
  [`lit array u8 8 do 2 5 . _ 0 . end`], [an array with chosen cells filled], [every cell must be decided --- the rest by `_`],
  [`requires le n (len xs) .`], [a length condition as a contract], [bounds checks in the body are removed],
)

#recap[
  A slice carries its start and length together. `len` takes the length field, `index` stops when out of range, and `for` walks the
  elements. `array t n` writes the element type and then the length and is used in input positions; a sequence written out as values is listed with `lit array` or `lit slice`. A list bound with `var` is a local array in the frame that you may write inside its block; writing someone else's elements needs a `mut slice` and a `proc`.
  `subslice` narrows the window without copying. Writing the length condition as a contract removes bounds checks in the body, and a string
  literal is a table that can be indexed directly.
]
