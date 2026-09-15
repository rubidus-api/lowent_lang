#import "../../book/lib.typ": *

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
  You will learn the difference between `array n t` and `slice t`, how to read with `len`, `index` and `for`, and that out-of-range access
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
- `array 4 u8` in `last4` is a sequence of exactly 4. An `array` in an input position means it receives a slice of length 4, and that length
  is checked on entry, like `requires eq (len xs) 4 .`.
- `for x xs do … end` walks the elements in order.
- Passing a slice of length 3 and 5 to `at` stops with `E-VM-BOUNDS`. It does not read someone else's memory.

An `array` writes its length *first*. The reverse order is rejected.

#demo("examples/ch09/array_order.low")

According to the diagnostic, the old tool read `array u8 4` as a plain slice and silently dropped the length --- a place where a written
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

== Contracts remove bounds checks

An `index` bounds check remains only where it is not proven. Write the length condition as `requires`, and the check happens *once* on
entry to the op while the index checks in the body disappear.

#demo("examples/ch09/bounds.low")

With `requires le n (len a) .`, `i` inside `while lt i n .` is always less than `len a`. The compiler's interval analysis works that out and
removes the check in `index a i`. The rules behind this reasoning are proven in Coq, and an independent checker re-verifies the arithmetic
evidence the compiler leaves at every removed check (#chref("proofs-numbers")).

One thing to watch: a count `n` uses `le`, but an *index* itself uses `lt`. `requires le i (len a) .` allows `i = len a`, which is one past
the end.

#misconception[Putting `len` in a loop condition recounts every time][
  `len s` is neither a call nor a memory read. It just takes the length field out of the slice value `{start, length}`. Writing elements
  does not change the length; the only way the length changes is putting a different slice into the name. In fact, storing the length ahead
  as `let m u64 be len xs .` and then replacing `xs` with a shorter slice leaves `m` as a *stale length* that can go out of range.
  Storing it ahead is a choice of meaning, not an optimisation.
]

#qa[
  Can `[3,4,5]` given to `--run` be used for a `slice u64`?
][
  Bracketed `--run` arguments are sequences of bytes. That is why this book's runnable examples take slices as `slice u8`. Passing one to a
  `slice u64` stops the VM, because the byte count is not a multiple of 8. Slices of wider elements are usually built inside the program and
  passed along.
]

#recap[
  A slice carries its start and length together. `len` takes the length field, `index` stops when out of range, and `for` walks the
  elements. `array n t` writes the length first and is used in input positions. Writing elements needs a `mut slice` and a `proc`.
  `subslice` narrows the window without copying. Writing the length condition as a contract removes bounds checks in the body, and a string
  literal is a table that can be indexed directly.
]
