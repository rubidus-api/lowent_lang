#import "../../typst-ko/lib.typ": *

= Parallel loops and atomic operations

#chapter-toc()

#prereq(
  ([#chref("references"), Borrowing], [many readers or one writer]),
  ([#chref("tasks-channels"), Tasks and channels], [flows are bound to blocks and every ordering can be tested]),
  ([#chref("capabilities"), Capabilities], [`effects atomic` pairs with `cap atomic`]),
)

#deepqa[
  What did #chref("references") say the exclusivity rule ("many readers or one writer") has to do with multithreaded programs?
][
  That once the rule holds within one flow, the absence of data races follows when the work is split across several flows. This chapter actually uses that
  property --- it splits one loop into pieces run by several flows together, and has translation confirm that splitting gives the same answer.
]

#why[
  Splitting the same computation over several cores is the most common parallelisation and the most common place for defects. If the pieces read each other's
  elements, accumulate into the same variable, or use operations whose answer depends on order, the program becomes "fast but sometimes wrong". Lowent *does not
  trust a declaration that splitting is possible; it checks it*. If it cannot check, it does not quietly run sequentially --- it refuses translation. And for the
  rare places where several flows really must touch the same memory, atomic operations are chosen by name. This is the last chapter of Part VII.
]

#organizer[
  You will learn to declare in an op head that a loop may be split with `parallel <slice> split .`, and the three conditions the processor checks (read only
  your own share, write only your own share, do not write places that live across iterations). You will state accumulation with `reduce <place> <op> .` and see
  why non-associative operations are rejected. You will also see atomic operations such as `atomic_add` and `atomic_load`, memory orderings (`order`), and the
  rules for combining them.
]

#chapter-questions()

== Declaring that a loop may be split

#demo("examples/ch27/split.low")

#idx("parallel")
- `double_all`'s head has `parallel s split .` --- a declaration that "this loop may be split into pieces of `s` run by several together". Each step reads and
  writes only `index s i`, its own element.
- `total` accumulates a sum. The accumulator `acc` lives across steps, so as is it cannot be split. `reduce acc add .` states "accumulate per piece, then combine
  with `add`".

In pictures, this is what the two declarations allow.

```text
 split --- each piece touches only its own share
   s:  [ 0 1 2 3 | 4 5 6 7 | 8 9 10 11 ]
         piece A    piece B    piece C        ← all three may run at once without touching each other

 reduce acc add --- gather per piece, then combine
   piece A: acc_A = 0+1+2+3      = 6  ─┐
   piece B: acc_B = 4+5+6+7      = 22 ─┼─ add ─▶ acc = 66
   piece C: acc_C = 8+9+10+11    = 38 ─┘
```

`add` gives the same answer however it is grouped (it is associative), so any split matches the sequential result. Floating-point addition is
not, so it cannot be split with `reduce` (see "The combining operation must be associative" below).

When the processor confirms such a declaration, it reports `W-PAR-OK`, and that note contains something important: the VM still runs sequentially. That is *a
correct implementation* because a theorem is proven that, under the splittable conditions, the parallel result is bit-for-bit identical to the sequential result
(#chref("proofs-races")). Native code really does split into pieces and run them on several threads. The two back ends agreeing is a measurement of
that theorem.

== The three conditions the processor checks

#dtable(
  columns: 3,
  id: "par-conditions",
  caption: [Conditions for a loop that may be split],
  [*Condition*], [*When broken*], [*What goes wrong*],
  [Read only your own share], [`E-PAR-READ`], [Reading another's place makes old-or-new depend on who runs first],
  [Write only your own share], [`E-PAR-WRITE`], [Two steps writing the same place leave a value that depends on order],
  [Do not write places that live across steps], [`E-PAR-CARRY`], [Such places tie steps together; to gather, state it with `reduce`],
)

#demo("examples/ch27/par_read.low")

This loop subtracts the first element from every element. If the first piece changes `index s 0` first, the first element other pieces read has already changed.
Even run sequentially, the loop has the defect of subtracting 0 after the first step, and splitting makes that defect depend on ordering.

#demo("examples/ch27/par_carry.low")

It accumulated into `acc` without `reduce`. The diagnostic says exactly how to fix it.

#misconception[If answers sometimes differ when run in parallel, that is a performance tuning issue][
  A program whose answer depends on ordering is not slow but wrong. In Lowent the `parallel` clause does not change meaning. The answer run split and the answer
  run one by one must always be the same, and if that condition cannot be confirmed, translation refuses. "Fast but sometimes wrong" is not something this
  language sells.
]

== The combining operation must be associative

When gathering with `reduce`, the shape of the tree that combines pieces changes depending on how they were split. If the operation is associative, the shape
does not change the answer; if not, it does.

#demo("examples/ch27/par_assoc.low")

`sub` is not associative: `(1000 − 1) − 2` and `1000 − (1 − 2)` differ. Both propositions --- associative means the shape does not matter, non-associative means
the shape changes the result --- are proven in Coq, and the diagnostic cites the theorem's name. Floating-point addition also depends on order, so it is blocked
separately (`E-PAR-FLOAT`).

== Atomic operations

#idx("atomic operations")
Sometimes split pieces must update *one place* together, such as adding counts from each piece into a shared counter. With plain `add`, two threads read the same
value, add separately and write, and one addition is lost. An atomic operation is indivisible --- other flows cannot see its middle.

#demo("examples/ch27/counter.low")

- `atomic_add counter 0 1` atomically adds 1 at position 0 of the slice `counter`. A place is addressed by *slice and index*.
- `count_par` splits `data` and updates the shared counter. It counted ten bytes, so the result is 10.
- Atomic operations are the `atomic` effect and require receiving `cap atomic`. `main` receives `cap atomic` at the entry point.
- `view_array u64` views the 8 allocated bytes as a slice of one `u64` without copying (#chref("named-types")).

Declaring the atomic effect without the capability is rejected.

#demo("examples/ch27/nocap.low")

Atomic operations are not free. Used on a value only one flow touches, they only slow things down. So they are not the default; they are chosen by name, and
their cost is written in the head's effects and capabilities.

#qa[
  If there are atomic operations, why is there no lock?
][
  Lock state shared between flows is not built in this edition, and using it says `E-LOCK-NOTYET`. Instead the standard library has data structures built on
  atomic operations. `spsc` is a ring buffer through which one producer and one consumer pass values without locks, and its correctness was confirmed by borrowing
  a proof in a weak-memory model (#chrefs("lib-containers", "proofs-locks")).
]

== Memory orderings

#idx("memory ordering")
Appending `order <name>` to an atomic operation decides what other flows see and when.

#dtable(
  columns: 2,
  id: "par-order",
  caption: [Memory orderings],
  [*Name*], [*Meaning*],
  [`seq_cst`], [Every flow sees one single order. The strongest; the default when not written],
  [`acq_rel`], [For read-write operations, nothing leaks forward or backward],
  [`acquire`], [Work after this read does not leak ahead of it],
  [`release`], [Work before this write does not leak behind it],
  [`relaxed`], [Atomicity only, no ordering],
)

The strongest is the default because it is the easiest to reason about. It is proven that when every access is `seq_cst` you may think sequentially. Weakening is
a choice made in writing by someone who knows its value.

Combinations that mean nothing for an operation are rejected.

Using four of the orders with the operations they pair with looks like this.

#demo("examples/ch27/orders.low")

`atomic_store … order seq_cst` writes 5, `atomic_swap … order acq_rel`, which reads and writes, swaps in 7 and returns the old 5, `order release`
writes 15, and `order acquire` reads it back, so 15 is returned. A pairing that does not match is refused.

#demo("examples/ch27/order_bad.low")

What would a read release? C leaves such combinations undefined. A write cannot be `acquire`, and a fence (`atomic_fence`), which only sets order, cannot be
`relaxed`.

== Lanes --- computing several values at once

#idx("lanes")
Where `parallel` splits a loop across *flows*, `vec` holds *several values* as one value within a single flow and computes them at once
(SIMD). `vec u32 4` is a value with four lanes holding four `u32`s, and the lane count is part of the type. Whether the machine computes the
lanes at once or one by one, the answer is the same (canon 6.2.11).

#demo("examples/ch27/lanes.low")

- `load xs 0` reads four lanes starting at position 0 of the slice. `store ys 0 t` writes the other way.
- `splat 5` fills every lane with 5. The lane count comes from the type of the name it is stored in (`vec u32 4`).
- Comparing `vec`s, as in `gt v lim`, gives a *mask* `mask 4` holding true or false per lane. `select over lim v` picks `lim` in lanes where
  the mask is on and `v` where it is off. Choosing per lane without a branch (`if`) lets the machine do it in one instruction.
- `reduce_add`, `reduce_max`, `reduce_min` and `reduce_mul` gather the lanes into one. Pressing `[1,9,3,7]` down to 5 gives `[1,5,3,5]`, whose
  sum is 14.
- `reverse` reverses the order of the lanes, and `rotate r 1` rotates them by one. `[7,3,9,1]` rotated, `[3,9,1,7]`, was written to memory.
- `native_lanes u32` gives, *at translation time*, how many `u32` lanes this machine handles at once. Choosing that lane count only computes
  more at once; the answer is the same.

A mask can also read or write just some of the lanes.

#demo("examples/ch27/masked.low")

`store_masked out 0 v m` writes only at the places of the lanes that are on (9 and 7) and leaves the others untouched. `load_masked xs 0 m fallback`
reads only the lanes that are on and puts the default 100 in the others. This is the shape for handling the tail when fewer than
four slots remain at the end of a slice.

#dtable(
  columns: 3,
  id: "par-lane-ops",
  caption: [Builtin ops for lanes and arrays],
  [*Op*], [*What it does*], [*Note*],
  [`load` · `store` · `load_masked` · `store_masked`], [read and write between memory and lanes], [masked forms touch only lanes that are on],
  [`splat` · `select`], [one value into every lane · choose per lane by mask], [`splat` needs a type context (below)],
  [`reduce_add` · `reduce_max` · `reduce_min` · `reduce_mul`], [gather the lanes into one], [the result has the element type],
  [`reverse` · `rotate`], [reverse · rotate the lanes], [the count is a translation-time constant],
  [`native_lanes`], [this machine's lane count (translation time)], [does not change answers],
  [`sum_neumaier` · `sum_seq`], [add up a float slice seen through `view_array`], [not lane ops --- the name says how they add (compensated · front to back)],
  [`avg`], [rounding average per lane], [its value is fixed at `(a+b+1)>>1` — the sum is widened so a lane cannot overflow],
  [`prefetch xs i`], [pull a place about to be used into cache], [a performance hint that does not change meaning],
)

Watch two things. First, `splat` takes its lane count from the *declared type*, so it cannot be written inline in an expression.
Second, *adding lanes and adding a slice are different ops* --- lanes are `reduce_add`, a float slice is `sum_neumaier` or `sum_seq`. Until
2026-09-17 the latter were spelled `sum` and `sum_fast`, while the canon used those same names for "add all the lanes". One name meant two
things, so the names were split (canon 6.3.7.1).

#antipattern[Writing `splat` inline in an expression][
  #demo("examples/ch27/mistake_splatinline.low")

  `splat` fills every lane with one value, and only the declared type says *how many lanes there are*. Inside an expression there is nothing
  to say it, so the value is read as a scalar and the surrounding comparison stops matching its `mask` type. It is rejected with
  `E-VEC-SPLAT`; store it first under a name that writes the lane count down --- `var lim be vec u32 4 splat 5 .`, then `gt v lim`.
]

== Saying how a place is used --- `access`

The `access <name> <mode> .` clause writes in the head whether the op only *reads* an input place or only *writes* it. Callers and the scheduler
rely on that promise.

#demo("examples/ch27/access.low")

`shared_read` means "only reads". With no write there is no race, so several tasks may hold the same place together. `write_only` means "only
writes", so a buffer not yet filled may be passed. The tool *checks* both modes against the body. The remaining modes such as `sequential` are
kernel scheduling hints that constrain nothing yet, and writing one makes `W-NOT-YET` say so.

== Common mistakes

#antipattern[Pieces of a split loop incrementing a shared counter with ordinary arithmetic][
  #demo("examples/ch27/mistake_sharedwrite.low")

  Every piece reads `index counter 0`, adds 1 and writes it back. When two threads read the same value and each writes its sum, one
  increment is lost. That is a write to a place outside the piece's own share, so it is rejected with `E-PAR-WRITE`. If a shared place
  really must be updated together, take `cap atomic` and use `atomic_add counter 0 1` (this chapter's `counter.low`). Usually, though,
  gathering per-piece counts with `reduce` is faster.
]

#antipattern[Splitting a sum of floats][
  #demo("examples/ch27/mistake_floatreduce.low")

  Floating-point addition is not associative. How the pieces are split changes the rounding, so the answer would depend on the number of
  cores. Hence `E-PAR-FLOAT`. As the diagnostic suggests, use the sequential `sum_neumaier` (a compensated sum). Determinism is part of the meaning,
  not a performance option.
]

#antipattern[Starting a `reduce` accumulator at a value that is not the identity][
  #demo("examples/ch27/mistake_reduceinit.low")

  Run sequentially, `total [1,2,3,4,5,6]` is 100 + 21 = 121. Split, *each piece* starts `acc` at 100, and six pieces of native code gave 621
  --- the promise that a split answer equals the sequential one breaks there. So it is refused with `E-PAR-IDENTITY` (until 2026-09-16 it
  passed with `W-PAR-OK`). The starting value of a `reduce` must be the *identity* of the gathering operation --- 0 for `add`, `bit_or` and
  `bit_xor`, 1 for `mul`, 0 for `max` on an unsigned width. If there is a value to add, add it to the result outside the loop.
]

#antipattern[Writing the loop to split in a different shape][
  #demo("examples/ch27/mistake_noloop.low")

  The processor recognises only loops of the shape `while lt i (len s) . do … end` as candidates for splitting. `while lt (add i 1) (len s)`
  is not that shape, so this is `E-PAR-NOLOOP`. As the diagnostic says, the `parallel` clause is a *claim*, and with no loop to split,
  nothing is verified and only the claim remains. This loop also reads the neighbouring element `index s (add i 1)`; even with the shape
  fixed it would be rejected with `E-PAR-READ`. Write neighbour-reading computations (smoothing and the like) as a sequential loop that
  writes its results into another slice.
]

#antipattern[Declaring `write_only` and then reading][
  #demo("examples/ch27/mistake_access.low")

  A `write_only` place may not be filled yet, so reading it reads garbage. Callers trust the declaration and pass an empty buffer. So it is
  refused with `E-ACCESS-MODE`. The `W-EFFECT-OVER` that comes along is due to a defect where writes to a caller's buffer are counted as effects
  inconsistently (#chref("pipe")). If the op must read, drop the mode or take the place as an ordinary input.
]

#misconception[Only addition can be gathered with `reduce`][
  #demo("examples/ch27/max_gather.low")

  Any associative operation can be gathered. `max` is associative, and 0 is its identity for `u64`, so taking the largest value in each piece
  and combining them again with `max` gives the same answer. `min`, `mul` and the bit operations work on the same principle. What is rejected
  is an operation such as `sub`, where grouping changes the answer (`E-PAR-ASSOC`), and floating-point operations (`E-PAR-FLOAT`).
]

== This chapter's syntax at a glance

#dtable(
  columns: 3,
  id: "parallel-atomic-glance",
  caption: [Parallel and atomic syntax --- shape · meaning · why it looks this way],
  [*Shape*], [*Meaning*], [*Why*],
  [`parallel s split .` (op head)], [declares that `s` may be split and processed by many], [a claim that is checked, not trusted --- `W-PAR-OK` when it holds],
  [`while lt i (len s) . do … end`], [the shape of a splittable loop], [any other shape is `E-PAR-NOLOOP`],
  [reading and writing only `index s i`], [only its own share], [others' places: `E-PAR-READ` · `E-PAR-WRITE`],
  [`reduce acc add .`], [accumulate per piece, then combine with the operation], [start at the identity --- the operation must be associative (`E-PAR-ASSOC`)],
  [`atomic_add counter 0 1` · `atomic_load cells 0`], [atomically on a place named by slice and index], [`effects atomic` + `cap atomic`],
  [`… order seq_cst` · `acq_rel` · `acquire` · `release` · `relaxed`], [memory ordering --- `seq_cst` if unwritten], [the easiest to reason about is the default],
  [`order release` on a read, and so on], [rejected (`E-ATOMIC-ORDER`)], [meaningless combinations are not left undefined],
  [`view_array u64 bytes`], [see bytes as a `u64` slice without copying], [atomic cells live on an allocated window],
  [`var v be vec u32 4 load xs 0 .` · `reduce_add v`], [read four lanes as one value · gather lanes], [SIMD within one flow --- the lane count is part of the type],
  [`access data shared_read .` · `access out write_only .`], [a promise to only read · only write --- checked against the body], [read-only lets several tasks hold it · breaking it is `E-ACCESS-MODE`],
)

#recap[
  `parallel <slice> split .` declares that a loop may be split, and the processor checks that it reads and writes only its own share and does not write places
  that live across steps. Accumulation is stated with `reduce <place> <op> .`, and the operation must be associative. The split answer is bit-for-bit identical to
  the sequential one. When a shared place is updated together, receive `cap atomic` and use atomic operations, choosing a memory ordering with `order`; the default
  is `seq_cst`, and meaningless combinations are rejected.
]
