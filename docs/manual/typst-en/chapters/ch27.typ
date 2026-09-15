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

#demo("examples/ch27/order_bad.low")

What would a read release? C leaves such combinations undefined. A write cannot be `acquire`, and a fence (`atomic_fence`), which only sets order, cannot be
`relaxed`.

#recap[
  `parallel <slice> split .` declares that a loop may be split, and the processor checks that it reads and writes only its own share and does not write places
  that live across steps. Accumulation is stated with `reduce <place> <op> .`, and the operation must be associative. The split answer is bit-for-bit identical to
  the sequential one. When a shared place is updated together, receive `cap atomic` and use atomic operations, choosing a memory ordering with `order`; the default
  is `seq_cst`, and meaningless combinations are rejected.
]
