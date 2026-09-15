#import "../../book/lib.typ": *

= Proofs about ownership and borrowing

#chapter-toc()

#prereq(
  ([#chref("references"), Borrowing], [many readers or one writer]),
  ([#chref("ownership"), Ownership], [the strength of memory rules --- static, dynamic, proven]),
  ([#chref("proofs-why"), Why prove], [invariant preservation and exhaustive checking]),
)

#deepqa[
  #chref("references")'s `stale.low` wrote 5 to `n` while `r` held a read borrow of `n`. What rejected it, and what does that rule protect?
][
  It was rejected with `E-EXCL`. Borrows of one value must be many readers or one writer, and the rule protects *the reader's belief* --- that the value does not change while it is
  looking. This chapter covers whether programs the compiler "passes" under this rule really have no violations at run time.
]

#why[
  The borrowing rules are the foundation of this language's memory safety and absence of data races. But a rule existing and a rule being *sufficient* are different. If the rule has
  a hole, the compiler's "pass" means nothing. This chapter's theorems give that "pass" meaning. And in the course of proving, one real compiler defect was turned into a theorem. It is
  the story that best shows what proofs do, so it comes after numbers and bounds.
]

#organizer[
  You will learn the two models --- a static machine (a list of live borrows) and a dynamic machine (borrow stacks) --- and how a program is reduced to a sequence of three kinds of
  events. You will understand the central theorem "programs passing the static check have no borrowing violations at run time" and the two invariants its proof needed. You will also
  see the fixed-point theorem that loops are safe however many times they run, the story of widening the model to places and op boundaries, and the exhaustive checking of about ten
  million cases backing the gap between model and implementation.
]

#chapter-questions()

== Two machines and three events

Two machines look at the same rule. The *static* machine holds a list of live borrows at compile time and rejects attempts to create overlapping borrows. The *dynamic* machine keeps
a borrow *stack* per place at run time and stops when an invalidated borrow is used. This chapter's theorem is that the two machines do not disagree.

For the proof, a program is reduced to a sequence of three kinds of events. Making a small object keeping only the essence instead of the whole language is the first skill of proof.

#dtable(
  columns: 2,
  id: "pown-events",
  caption: [Three events],
  [*Event*], [*Meaning*],
  [`Create t x mut`], [Create a borrow of place `x` with tag `t`. `mut` says whether it can write],
  [`Use t wr`], [Access through borrow `t`. `wr` says whether it is a write],
  [`Own x wr`], [The owner accesses place `x` directly],
)

The dynamic machine is defined by five rules. Creating a borrow pushes it onto the stack (D1); *reading* through a borrow discards only the write borrows above it and keeps read
borrows (D2); *writing* through a borrow discards everything above (D3). An owner read invalidates all write borrows (D4), and an owner write empties the stack (D5). The difference
between D2 and D3 is the point --- many readers can coexist, but after a write the names above see stale values and must no longer be usable. And the owner touching the place again is
not itself an error; the error is caught when a dead borrow is *about to be used* afterwards.

== The central theorem

#mathbox[Theorem A --- agreement (`agreement` in `LowentEXCL.v`)][
  `forall evs, wf evs -> static_green evs = true -> dyn_clean evs = true.` For every well-formed event sequence, if the static check passes, the dynamic machine has no violation.
  Read by contraposition (`no_violation_in_green`): if a borrowing violation happened at run time, the static check certainly rejected that program.
]

The converse --- dynamically safe implies statically passing --- is not true and need not be. The static check is conservative and may reject programs that are actually safe. Being
wrong on the safe side is an inconvenience; being wrong on the dangerous side is a disaster. It is the same asymmetry as #chref("proofs-numbers")'s interval analysis.

The proof is invariant preservation, and it needed two invariants.

- *INV* --- borrows alive statically are also on the dynamic stack, with matching target place and write flag.
- *SINV* --- any two distinct borrows alive on the same place are both read borrows.

Why was SINV needed? Writing through a borrow (D3) discards everything above on the stack, and if something discarded is still alive statically, INV breaks. The rule "only one write
borrow" prevents overlap, so there is no other borrow alive on the same place as the writing borrow. Intuition says "because overlap is dangerous", but the proof gives a more exact
reason --- *without that rule the correspondence between the two machines collapses.* Loosen the rule and the proof points to exactly where it breaks.

== However many times it runs

The model of the theorem above had no loops. But the compiler looks at code once, and that code runs a million times.

#demo("examples/ch40/loop_stale.low")

A read borrow `r` created outside the loop is used inside it, while inside the same loop the owner writes to `x`. The first iteration looks fine, but that write kills `r`, and the
next iteration's `deref r` uses a dead borrow. It is the shape that blows up on the second iteration.

#demo("examples/ch40/loop_fresh.low")

Conversely, if the borrow *is born and ends inside* the loop, it is fresh every iteration, and nothing stale remains between iterations.

#mathbox[Theorem B --- loop agreement (`loop_agreement` in `LowentLoop.v`)][
  If, from the static state after setup code `pre`, running the loop body `body` once *returns to the same state* (a fixed point), then the loop has no borrowing violation however
  many times it runs (`forall k`). 0 times, once, 2#super[64] times. Something was learned along the way. There is *only one way* borrows break in loops --- a borrow created outside the
  loop used in the body while the body has owner access. One static rule rejects exactly that pair, and the theorem shows "that one is enough". How many rules are needed is known by
  argument, not by feel.
]

Nested loops are proven too (`LowentNest.v`). Writing the program as a tree and its unfolding as a *relation* rather than a function gave a theorem with no depth limit, no iteration
limit, and *different behaviour allowed per iteration*. A real program whose inner loop runs three times on the first outer iteration and zero times on the second fits as is. And the
old flat theorem is recovered as a corollary. If it could not be recovered, it would not be a generalisation but a different theorem.

== Finding an implementation defect while widening the model

The flat model saw places as numbers. The real language is more complex. The model was widened twice, and both times Theorem A was proven again.

*Places.* In the flat model `x.a` and `x.b` are both just `x`, so two write borrows of different fields come out as conflicting. The real rule passes them. A proof that cannot follow
the implementation is about a different language. So places were generalised to paths, and *equality* of places was replaced by *overlap* ("one is a prefix of the other"). The
structure of the theorem and invariants stayed the same. With a good abstraction, extension ends this locally.

*Op boundaries.* The event language had no calls. So the shape where an op returns a borrow received as an argument directly as a reference, *laundering* the borrow, was outside the
model. The implementation really hit it --- the static check did not recognise the returned reference as the same as the original borrow and passed it. After the defect was fixed,
the story was turned into a theorem.

#mathbox[The old rule was unsound (`LowentLaunder.v`)][
  `old_rule_is_unsound` --- in the laundering case the old static check passes while the dynamic machine reports a violation. `new_rule_catches_it` --- the fixed rule rejects that case.
  And `honest_laundering` --- honest returns still pass. Without the last theorem, "reject everything" would also be a correct answer.
]

Until then it was a *story*: "we ran into it". Stories are forgotten and erased by refactoring. Now it is a *theorem*. If anyone removes that check, the proof breaks. This is the most
valuable thing proofs do in this repository.

#qa[
  If the place model is proven, can `mut_ref (field s a)` and `mut_ref (field s b)` be used at the same time?
][
  The static check passes it. But this edition's intermediate representation cannot yet lower references pointing at non-local places (fields), so running it says it cannot with
  `E-IR-UNSUP` and `E-VM-UNSUP`. A proven rule and an implemented feature are different things. Moreover, this edition's `--check` prints that diagnostic and still ends with "check:
  ok"; being green while an error code is present is a defect.
]

== Between model and implementation

#idx("exhaustive checking")
The theorems are about two machines on paper. Whether those machines match the real compiler is backed by *bounded exhaustive model checking*. All 10,490,024 event sequences with at
most two places, four tags and length 7 are fed to the implementation's static check and dynamic machine to see whether the verdicts match the model. Violations: 0. As an opt-in,
1.3 billion sequences of length 9 were also run, with 0 violations. Widening the envelope elevenfold cost under a second. Before writing "too expensive to do", measure.

Branch merging in loops is outside the theorems, and the loop checker exhaustively confirms 90,376 shapes × 1 … 3 iterations. These are "exhaustively checked", not "proven".

== What is not proven

- *Loop termination* is not covered. The theorem is "safe if it runs", not "it ends". Infinite loops are legitimate programs, like a firmware main loop.
- *Tags are assumed fresh*. The implementation does not reuse tags, but that fact itself is outside the proof.
- *Memory itself was not modelled*. The event model only looks at "who has the right to access". The step from that system of rights to actual memory safety such as use-after-free is
  separate.
- *It is a theorem about the sequential fragment*. The extension to several flows is handled by the data race theorem in #chref("proofs-effects-concurrency").

#recap[
  Comparing a static machine (a borrow list) and a dynamic machine (borrow stacks) over sequences of three events proved Theorem A: programs passing the static check have no borrowing
  violations at run time. The proof needed the correspondence of the two machines (INV) and "overlapping borrows are all reads" (SINV). Loops are safe however many times they run if
  they reach a fixed point, and nested loops were generalised by writing unfolding as a relation. Widening the model to places and op boundaries turned the implementation's laundering
  defect into a theorem. About ten million cases of exhaustive checking back the gap between model and implementation.
]
