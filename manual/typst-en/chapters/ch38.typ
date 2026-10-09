#import "../../typst-ko/lib.typ": *

= Why prove

#chapter-toc()

#prereq(
  ([#chref("build-test"), Building and testing], [tests, back-end cross-checks, contract cross-checks]),
  ([#chref("contracts"), Contracts], [enforced contracts become facts that remove checks]),
)

#deepqa[
  In #chref("build-test"), what did checks like the back-end cross-check show about defects, and what could they *not* show?
][
  They show that defects *exist* but not that they *do not*, because two implementations giving the same answer are not thereby both right. And absence, it said, is the
  job of proofs. Part X covers those proofs.
]

#why[
  Part X covers how far the claims this language makes about itself are true. "This is proven" came up several times in earlier chapters --- widening, the index safety of
  `mod`, the borrowing rules, the determinism of parallelism. This part honestly gathers what each of those claims means, what model it is about, and where it ends. Its first
  chapter sets out why proofs are needed, and how three layers --- proof, exhaustive checking and cross-checking --- overlap.
]

#organizer[
  You will confirm with an example the distinction that tests show "exists (∃)" and proofs show "none (∀)". You will learn what each of the three layers --- proof, exhaustive
  checking, cross-checking --- catches and misses, and the three strategies this repository's proofs use (invariant preservation, reduction to an order structure, exhaustive
  checking). You will also understand the measured scale of what is proven, and the relation that proofs decide what the compiler must reject.
]

#chapter-questions()

== Tests show existence, proofs show absence

#demo("examples/ch38/average.low")

`mean2`, which computes the mean of two numbers, passes all three tests. But given 4294967295 and 1, `add a. b. .` overflows `u32` and stops. Write a million more tests, and if the
author does not pick large numbers, the defect does not show.

This is the nature of testing. A test can show "it is wrong on *this* input (∃)" but not "it is wrong on *no* input (∀)". Checking a million natural numbers is not checking all
natural numbers. No amount of ∃ adds up to ∀. This gap is the one reason proofs are needed.

```text
 inputs of mean2: two u32 values = about 1.8 × 10^19 pairs

 tests          (10,20) (0,0) (10^6,3·10^6)  three points tried → all right
 counterexample (4294967295, 1)              a point never tried → overflows, stops
 proof          ─── every pair ───           speaks about all of them at once
```

In Lowent this defect is at least *not silent*. In C the overflowing sum would wrap and give a wrong mean. Here it stops (#chref("numbers")). And the fixed version is written in a
shape that cannot overflow.

#demo("examples/ch38/average_fixed.low")

Adding half the difference of the two numbers to the smaller cannot overflow. `--ir` says it removed one of three checks. The remaining two stay even though they really cannot
overflow, because the interval analysis does not know the relation between `min` and `max`. The analysis is wrong only *in the safe direction* --- when it cannot prove, it keeps the
check.

== Overlapping three layers

#dtable(
  columns: 4,
  id: "why-layers",
  caption: [Three layers of verification],
  [*Layer*], [*What it does*], [*How strong*], [*What it misses*],
  [Proof], [Every case, by logic], [No counterexample can exist within the model], [Powerless if the model is wrong],
  [Exhaustive checking], [Every case of a fixed size, actually run], [Certain within that size], [Cannot see larger inputs],
  [Cross-checking], [Two implementations on the same inputs], [Catches disagreements], [Misses when both are wrong the same way],
)

They are overlapped because the three catch different kinds of mistakes. Drawn as who checks what against what:

```text
 rules of the spec ── modelled ──▶ Coq model ── proof ──▶ every case in the model
                                     ▲
                                     │  exhaustive checking: model vs implementation, small sizes
                                     ▼
 compiler ─┬─ run on the VM ───┐
           └─ emitted as C ────┴─▶ cross-check: different answers = compiler defect
```

Proofs look at the model, cross-checks look at the implementation, and exhaustive checking ties the two together. In this repository cross-checks caught defects the proofs
missed, and proofs caught defects cross-checks missed. And this part does not mix three words. "Proven" means Coq checked every case, "exhaustively checked" means every case of a fixed size was run, and "sketch" means a person
argued it but no machine checked.

#misconception[A program written in a proven language is correct][
  Proofs are about *the language's rules*, not *your program*. The theorem that the borrowing rules are sound says only "programs passing translation have no borrowing
  violations"; it does not say the program gives the answer you want. `mean2` above has no borrowing violations and its overflow is not silent, but it was still a wrong design.
  Whether a program is correct is answered by contracts, tests and review.
]

== What is proven

The proof files are all in `docs/proofs/coq/`. This is the scale counted for this edition.

#dtable(
  columns: 2,
  id: "why-ledger",
  caption: [Scale of machine-checked proofs],
  [*Item*], [*Count*],
  [Coq files], [31 (including 1 checker extraction file)],
  [Theorem · Corollary · Lemma], [179 · 15 · 250],
  [Example], [33],
  [`Qed`], [481],
  [`Admitted` · `Axiom`], [0],
  [Lines of proof script], [10,350],
  [Checkers], [Rocq 9.2 (all) · Coq 8.20.1 (all but the two weak-memory files)],
)

In Coq, `Admitted` means "assume this is true and move on", and `Axiom` means "this is assumed". If even one such place exists, every theorem in the file relies on that assumption.
Both are 0. What is proven and what is not yet is written in `docs/proofs/LEDGER.md`.

Grouped, it looks like this.

#dtable(
  columns: 3,
  id: "why-groups",
  caption: [Groups of what is proven, and the chapters of this part],
  [*Group*], [*In one sentence*], [*Chapter*],
  [Tools], [The six pieces of mathematics later chapters use --- partial orders, lattices, fixed points, induction, abstract interpretation], [#chref("proofs-math")],
  [Numbers], [No widening silently changes a value, and the cases where division fails are known exactly], [#chref("proofs-numbers")],
  [Bounds], [Places where checks were removed are proven to be in range], [#chref("proofs-bounds")],
  [Ownership and borrowing], [Programs passing translation have no borrowing violations at run time], [#chref("proofs-ownership")],
  [Loops], [A loop that reaches a fixed point has no borrowing violations however many times it runs], [#chref("proofs-loops")],
  [Effects], [Effect declarations cover the effects that actually happen, and optimisations are legitimate on top of that], [#chref("proofs-effects")],
  [Races and parallelism], [Safe code has no data races, and parallel results are bit-for-bit identical to sequential], [#chref("proofs-races")],
  [Weak memory], [With the default memory ordering you may think sequentially, and weaker orderings add behaviours], [#chref("proofs-weak-memory")],
  [Locks], [A CAS spinlock gives mutual exclusion, and ascending lock order does not deadlock], [#chref("proofs-locks")],
  [Syntax], [One statement is a one-statement block, and whichever closer closes it gives the same tree], [#chref("proofs-syntax")],
  [Hashes], [Comments do not change hashes, and equal encodings mean equal interfaces], [#chref("proofs-hash")],
)

The last chapter (#chref("proofs-limits")) gathers what all of these rely on and what they do not cover.

== Three ways of proving

You do not need to read proof scripts. But knowing *how* something was proven gives a sense of how much to trust the theorem.

*Invariant preservation.* Show that "a property is kept every time the program takes a step". If it is true at the start (base) and a step carries truth to truth (preservation),
it is true however many steps are taken. It is the program version of mathematical induction. The theorems for borrowing rules and loops take this approach.

*Reduction to an order structure.* Turn the problem into one about an order and answer it with properties of the order. Making "`u8` fits safely into `u32`" into a partial order
turns "which type to pick when joining two types" into the well-known notion of a least upper bound. Numeric widening and the weak-memory model take this approach.

*Exhaustive checking.* Properties not yet proven for all sizes are run for every case within a fixed size. This is not proof, and this part says so.

== Promises this part keeps

This part keeps three promises.

- *It does not mix words.* "Proven", "exhaustively checked" and "sketch" are different strengths. "This language is proven" is often really "a
  few short cases were run".
- *Every theorem comes with the defect it stops.* Theorems whose defect could not be named were left out. A theorem with no value to the reader
  has no reason to be here.
- *This part is not normative.* The language is defined by the specification's clause canon. If this part disagrees with it, this part is wrong.

And the ideas this language rests on, reduced to six lines, are these. All six are either backed by mathematics or written down as not backed
--- that distinction is the purpose of this part.

+ *Make wrong programs impossible to write.* Making them inexpressible is cheaper than catching them (#chref("proofs-ownership")).
+ *Answer "I don't know" when you don't know.* Failure is a value --- `option` or `result` --- not a hidden exception.
+ *Costs must be visible.* Allocation, side effects and copies are all written in the source (#chref("proofs-effects")).
+ *When in doubt, keep the check.* Checks are removed only when there is a proof (#chref("proofs-bounds")).
+ *Two implementations must give the same answer.* If the VM and native code differ, it is a compiler defect (#chref("build-test")).
+ *Write down what was not done.* Assumptions, limits and what is unproven are recorded (#chref("proofs-limits")).

#qa[
  How do proofs connect to compiler code? Does a theorem change the compiler?
][
  Theorems decide *what the compiler must reject*. That the condition for `uN ⊑ iM` in the widening table is `N < M` (strict), and that blocking *only one* shape --- a write
  borrow living across a loop meeting an owner write in the body --- is enough, came from theorems. The other direction exists too. Turning a defect the implementation hit
  ("laundering a borrow through a function") into a theorem means the proof breaks the moment anyone removes that check. Stories are forgotten, but theorems remain
  (#chref("proofs-ownership")).
]

```text
 theorem ────────────────▶ decides which shapes the compiler must reject
                            e.g. the condition for uN ⊑ iM is N < M (strict)
 a defect the compiler hit ▶ turned into a theorem
                            e.g. laundering a borrow through a function
                            remove that check and the proof breaks
```

#recap[
  Tests show that a wrong input *exists*, and proofs show that no input is wrong. Lowent overlaps proof, exhaustive checking and cross-checking, and does not mix "proven",
  "exhaustively checked" and "sketch". The 31 Coq files have no `Admitted` or `Axiom`. Proofs are built by invariant preservation, reduction to order structures and exhaustive
  checking, and they decide what the compiler must reject. Proofs are about the language's rules, not a guarantee that your program is correct.
]
