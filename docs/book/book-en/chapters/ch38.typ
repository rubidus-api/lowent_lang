#import "../../book/lib.typ": *

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

`mean2`, which computes the mean of two numbers, passes all three tests. But given 4294967295 and 1, `add a b` overflows `u32` and stops. Write a million more tests, and if the
author does not pick large numbers, the defect does not show.

This is the nature of testing. A test can show "it is wrong on *this* input (∃)" but not "it is wrong on *no* input (∀)". Checking a million natural numbers is not checking all
natural numbers. No amount of ∃ adds up to ∀. This gap is the one reason proofs are needed.

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

They are overlapped because the three catch different kinds of mistakes. In this repository cross-checks caught defects the proofs missed, and proofs caught defects cross-checks
missed. And this part does not mix three words. "Proven" means Coq checked every case, "exhaustively checked" means every case of a fixed size was run, and "sketch" means a person
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
  caption: [Groups of what is proven],
  [*Group*], [*In one sentence*], [*Chapter*],
  [Numbers and bounds], [No widening silently changes a value, and places where checks were removed are proven in range], [#chref("proofs-numbers")],
  [Ownership and borrowing], [Programs passing translation have no borrowing violations at run time], [#chref("proofs-ownership")],
  [Effects and concurrency], [Effect declarations cover actual effects, safe code has no data races, and parallel results are bit-for-bit identical to sequential], [#chref("proofs-effects-concurrency")],
  [Syntax and hashes], [Whichever closer closes it gives the same tree, and the same source gives the same bytes], [#chref("proofs-syntax-hash")],
)

== Three ways of proving

You do not need to read proof scripts. But knowing *how* something was proven gives a sense of how much to trust the theorem.

*Invariant preservation.* Show that "a property is kept every time the program takes a step". If it is true at the start (base) and a step carries truth to truth (preservation),
it is true however many steps are taken. It is the program version of mathematical induction. The theorems for borrowing rules and loops take this approach.

*Reduction to an order structure.* Turn the problem into one about an order and answer it with properties of the order. Making "`u8` fits safely into `u32`" into a partial order
turns "which type to pick when joining two types" into the well-known notion of a least upper bound. Numeric widening and the weak-memory model take this approach.

*Exhaustive checking.* Properties not yet proven for all sizes are run for every case within a fixed size. This is not proof, and this part says so.

#qa[
  How do proofs connect to compiler code? Does a theorem change the compiler?
][
  Theorems decide *what the compiler must reject*. That the condition for `uN ⊑ iM` in the widening table is `N < M` (strict), and that blocking *only one* shape --- a write
  borrow living across a loop meeting an owner write in the body --- is enough, came from theorems. The other direction exists too. Turning a defect the implementation hit
  ("laundering a borrow through a function") into a theorem means the proof breaks the moment anyone removes that check. Stories are forgotten, but theorems remain
  (#chref("proofs-ownership")).
]

#recap[
  Tests show that a wrong input *exists*, and proofs show that no input is wrong. Lowent overlaps proof, exhaustive checking and cross-checking, and does not mix "proven",
  "exhaustively checked" and "sketch". The 31 Coq files have no `Admitted` or `Axiom`. Proofs are built by invariant preservation, reduction to order structures and exhaustive
  checking, and they decide what the compiler must reject. Proofs are about the language's rules, not a guarantee that your program is correct.
]
