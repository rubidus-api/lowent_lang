#import "../../book/lib.typ": *

= Borrowing --- `ref` and `mut_ref`

#chapter-toc()

#prereq(
  ([#chref("slices"), Sequences], [elements can be written only through a `mut slice`]),
  ([#chref("structs-enums"), Aggregates], [`field` is a place to read and to write]),
)

#deepqa[
  In #chref("slices"), what rejected writing to an element of a slice without `mut`? And how does immutability of a name made with `let`
  differ from immutability of a slice's elements?
][
  It was rejected with `E-TYPE-MUT`. `let` forbids *putting a different value into the name*, while whether elements can be written is decided
  by the slice's type (`slice` or `mut slice`). This chapter covers references that borrow a single value that is not a slice, and the rules
  for when borrows overlap.
]

#why[
  Copying a big value every time you pass it is slow, and passing a pointer leaves you unable to tell who changes the value and when. Lowent
  puts *borrowing* between the two. Borrowers for reading may be many; a borrower for writing must be alone. That rule is the foundation of
  memory safety without a garbage collector, and the proof that there are no data races in concurrency (Part VII) stands on it too. Before
  memory (Part V), learn the shape of borrowing.
]

#organizer[
  You will learn to borrow values with `ref t` and `mut_ref t` and read them with `deref`. You will see that a read borrow cannot write and that
  write permission only narrows in one direction. You will meet the exclusivity rule --- borrows of one value are "many readers or one writer"
  --- and the rule that a reference to a local cannot leave its op. You will also understand why `mut ref slice` is rejected.
]

#chapter-questions()

== Two kinds of borrow

#idx("reference")
A *reference* points to a value that lives elsewhere. There are only two kinds, and the name is the permission.

- `ref t` --- a read borrow. Many can hold one at the same time. It cannot change the value.
- `mut_ref t` --- a write borrow. There can be only one for a value at a time, and no read borrows meanwhile.

#demo("examples/ch12/borrow.low")

- `total` borrows the four-field `big` with `ref big` to read it without copying. `field v a` reads a field through the reference.
- `use_sum` borrows the same value twice with `ref v`. Read borrows may be many.
- `bump` takes a `mut_ref u64` and changes the caller's value with `set p …`. The value a reference points to is read with `deref p`.
- `use_bump` lends `var n` as `mut_ref n`. After `bump` returns, `n` is 8.

What matters is that the borrower *writes `ref x` or `mut_ref n` at the call site*. Someone reading the call sees from that place alone that
this call can change `n`.

#qa[
  How do you make a null reference?
][
  You cannot. A reference always points to a live value. To express "points to nothing", use an `option` (#chref("option-result")). Then the
  code that checks before taking out shows in the source, and taking out without checking stops.
]

== Write permission only narrows

Writing through a read borrow is rejected.

#demo("examples/ch12/ref_write.low")

The other direction is blocked too. Passing a value received only for reading to a position that takes `mut`, `mut_ref` or `owned` is
rejected with `E-TYPE-ARGMUT`. Something writable can be passed on for reading, but something read-only cannot be passed to a writable
position. Whoever received a value for reading must be able to trust that *it does not change while they look at it*, and that trust holds
only when nobody gains write permission behind their back.

== Many readers or one writer

#idx("exclusivity rule")
The trouble is overlapping borrows of the same value. Two overlapping write borrows are rejected.

#demo("examples/ch12/excl.low")

The body of `set_both` believes `a` and `b` are different values. Lending the same `n` twice breaks that belief, and whether 1 or 2 remains
depends on the order of writes. Overlapping read and write is rejected too.

#demo("examples/ch12/stale.low")

Writing 5 to `n` while `r` holds a read borrow of `n` means whoever reads through `r` cannot tell when the value they saw changed. These two
shapes --- another flow changing a value at the same time, and a value changing while being read --- are defects hard to find by reading the
source, so a rule removes them.

#misconception[The exclusivity rule only matters for multithreaded programs][
  The two examples above contain no threads. Even within one flow, when two names point at the same value, a write through one breaks what the
  other believes. That is also why C's `memcpy` is undefined for overlapping buffers. But once the rule holds within one flow, the absence of
  data races when the work is split into several flows follows, and that is where the proofs of Part VII lean
  (#chref("proofs-effects-concurrency")).
]

== A borrow cannot outlive what it borrows

Returning a reference to a local from its op is rejected.

#demo("examples/ch12/escape.low")

If this code were translated, the returned reference would point at a value already gone, and nobody knows what reading it would give.
Lowent knows this not by running it but *at translation time*. A borrow cannot leave the block that opened it (`E-BORROW-ESCAPE`), and a borrow
still alive after the lent value has moved is rejected too (`E-EXCL-MOVED`).

If a value must leave the op, return *the value* rather than a reference, or put it in storage the caller passed. If you need storage that
outlives the caller, use a region (#chref("regions")).

== There is no `mut ref slice`

Slices do not take `mut ref`.

#demo("examples/ch12/mref_slice.low")

Passing a `mut slice` already makes the elements writable. The slice value `{start, length}` is copied, but the bytes it points to are the
same. So what `mut ref slice` adds is exactly one thing --- *secretly changing the length and start of the caller's slice*. Then the caller's
loop has its length change even though its own code contains no assignment. Return the new slice instead.

#demo("examples/ch12/head.low")

`take_front` returns a new slice pointing at the first `n` elements. That the length changed shows in the return value and in `let front`.

#recap[
  `ref t` is a read borrow and `mut_ref t` a write borrow, and the borrower writes `ref x` or `mut_ref n` at the call site. The value a
  reference points to is read with `deref`. Write permission only narrows. Borrows of one value are many readers or one writer, and a borrow
  cannot outlive the lent value. `mut ref slice` is rejected; return the new slice.
]
