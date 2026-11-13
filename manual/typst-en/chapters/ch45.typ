#import "../../typst-ko/lib.typ": *

= Proofs about races and parallelism --- discipline instead of a memory model

#chapter-toc()

#prereq(
  ([#chref("actors"), Actors], [state is locked inside the actor and ownership moves by message]),
  ([#chref("parallel-atomic"), Parallel loops and atomic operations], [the three conditions for a splittable loop, and `reduce`]),
  ([#chref("proofs-ownership"), Proofs about ownership and borrowing], [within one flow, only one name writes]),
)

#deepqa[
  In #chref("parallel-atomic"), why did the `W-PAR-OK` note say that the VM running sequentially is *a correct implementation*?
][
  Because a theorem is proven that when the splittable conditions hold, the parallel result is bit-for-bit identical to the sequential one. This chapter covers
  that theorem and the one that must stand before it --- "there are no data races". They are different conclusions from the same premise.
]

#why[
  When two flows touch the same place at the same time and one of them writes, it is a data race. In C and C++ it is undefined behaviour. The usual way to deal
  with races is to learn a memory model such as memory orderings, which is hard. This language claims something else --- *code in split loops (level 1) and
  actors (level 2) never needs to look at a memory model.* Races are ruled out by discipline. And a guarantee that answers are the same every time, even without
  races, is needed separately. This chapter is the story of establishing both claims in pure Coq without heavy concurrency logic. Reading the proposition exactly
  reduces the tools needed.
]

#organizer[
  You will learn the three-line set condition called Bernstein independence and the definition of a race. You will pick up the pair of theorems that at level 1
  independence means no races and the condition is necessary, and the theorem that at level 2 a message always lies between accesses by different actors. You
  will also see why having no races differs from being deterministic, DET-1 (parallel results are bit-for-bit identical to sequential), DET-2 (freedom for the
  scheduler), DET-3 (associativity frees the shape of the reduction tree), and the counterexample for each.
]

#chapter-questions()

== The Bernstein condition --- an answer from 1966

#idx("Bernstein condition")
Let `R` and `W` be the *set of places read* and *set of places written* by a task. Two tasks are *independent* in three lines.

```text
W₁ ∩ W₂ = ∅      the two do not write the same place
W₁ ∩ R₂ = ∅      one does not read what the other writes
R₁ ∩ W₂ = ∅      nor the reverse
```

Three lines saying intersections are empty are all there is. `R₁ ∩ R₂` is not in the condition --- *reading* the same place together is fine. A race is defined as
"two tasks touch the same place and at least one writes". The usual definition adds "the two accesses are not ordered", but at level 1 that holds automatically.
Between tasks after flows split and before they rejoin, there are no ordering edges at all.

The processor's diagnostic quotes this condition directly.

#demo("examples/ch45/par_overlap.low")

Every piece writes the first element, so `wr ∩ wr = ∅` broke, and reading another piece's element broke `rd ∩ wr = ∅` too. #chref("parallel-atomic")'s three
conditions --- read only your own share, write only your own share, do not write places living across iterations --- are the Bernstein condition.

== Independence means no races, and the condition is necessary

#mathbox[Level 1 --- split loops (`LowentDRF.v`)][
  `l1_no_race : forall t1 t2 l, indep t1 t2 -> ~ races t1 t2 l.` If two tasks are Bernstein independent, they race at *no place*. The proof is five lines.
  Assuming a race means "one writes", and the independence condition denies the other's access to that place in all three cases. Contradiction.

  `l1_condition_is_necessary : ~ indep writer reader /\ races writer reader 0.` Actually build a pair breaking the condition --- one writes place 0 and the other
  reads place 0 --- and a race *exists*.
]

The second theorem is the mark of an honest proof. Proving only "satisfying the condition is safe" leaves the theorem true even if the condition is overly
strict --- a condition that lets nothing pass is also safe. So *a counterexample is proven alongside*: loosen the condition and it really breaks. One theorem,
one counterexample --- a method this part uses in many places.

Without races, the well-known DRF-SC theorem (Adve–Hill) applies and the program behaves with sequential consistency. That is, you need not imagine orders
jumbled up.

== Between actors there is always a message

#mathbox[Level 2 --- actors (`l2_accesses_are_separated_by_a_message`)][
  If in an execution trace *different* actors access the same place `l`, then *between* the two accesses there is always a message handing over ownership of
  that place.
]

The chain of argument runs like this. A place is owned by exactly one actor. Only the owner accesses it. Ownership moves only by message. But the two accesses
come from different actors. Therefore an ownership transfer happened between them. A message is an edge fixing before and after, and two accesses with a fixed
order are *by definition not a race*. The most common mistake with actors --- sending a reference by message and continuing to use it on the sending side --- is
stopped because it is an ownership transfer and the sender loses that place (#chref("actors")'s `handoff`).

The connection to #chref("proofs-ownership") is the point. Enforcing "only one name writes" within one flow becomes exactly "two tasks do not overlap". One safety
rule pays off in two places.

== Containment --- where heavy tools are needed

```text
level 1 (split loops)          ─┐
level 2 (actor isolation)       ─┤→ no races by discipline → DRF-SC → think sequentially
level 3 (atomics · locks)       ─┘→ races really exist     → a memory model is needed
```

#misconception[Proving concurrency always needs heavy tools like separation logic][
  The first plan was to leave "safe code has no races" as a sketch and do the machine proof with heavy concurrency logic. Looking again, that proposition was not a
  claim about memory models. "Discipline prevents races" is a *set-theoretic* claim. Heavy tools are really needed only at level 3, and among the proof files only
  the lock uses Iris (#chref("proofs-locks")). "It is concurrency, so separation logic" was a reflex.
]

== No races is not the same as deterministic

Running twice on the same input can give different answers. Even without races. If several cores add floating-point numbers in parts and combine them, the last
bits change when the *order* of combining changes. Each touched only its own share, so there is no race. Yet the answers differ.

This language's claim is strong. *The result of level 1 parallelism is bit-for-bit identical to the sequential result.* No approximation, no tolerance. The conditions
establishing that claim were proven to be exactly three (`LowentPar.v`). Determinism is not a matter of races but of the Bernstein condition. The same condition
gives a different conclusion.

#dtable(
  columns: 3,
  id: "prace-det",
  caption: [Three conditions for deterministic parallelism --- with and without],
  [*Condition*], [*With it*], [*Without it*],
  [DET-1 split without overlap], [Parallel = sequential (`det1_par_eq_seq`)], [Order changes the result (`overlap_is_nondeterministic`)],
  [DET-2 keep dependency edges], [The remaining order is free (`det2_schedule_free`)], [--- not using the freedom only loses performance],
  [DET-3 associative combining], [Tree shape does not change the result (`assoc_shape_free`)], [Same leaves, different results (`nonassoc_shape_matters`)],
)

#mathbox[DET-1 --- parallel equals sequential][
  If tasks are pairwise independent and each is *local* (decides values only from its own read set), the value at *every place* `i` after parallel execution equals
  sequential execution. The corollary's name is the claim itself --- `det1_bit_identical`. The fork in the proof is instructive. If the first task writes place `i`,
  no one else writes `i`, so the answer is the first task's. If the first task does not write `i`, the one writer among the rest gives the answer, and since that
  task's *reads* do not overlap the first task's writes, reading the original state or the state the first task touched gives *the same value*. The second case says
  why "local" is needed.
]

The counterexample is two tasks writing 1 and 2 to the same place. Depending on order, the answer is 2 or 1.

DET-2 is the theorem that swapping two non-conflicting tasks gives the same result. The proof is *induction on adjacent swaps*. If adjacent pairs can be swapped,
repeating that reaches "any rearrangement that keeps the conflict order" --- the same idea as a sorting algorithm. It gives the scheduler maximal freedom without
losing determinism, and freedom is performance.

DET-3's counterexample is subtraction. `(5 − 3) − 1 = 1` while `5 − (3 − 1) = 3`. If the operation is not associative the language must fix the tree shape, and if
the scheduler changes the tree with the number of cores, the result depends on the schedule. The proof did not import IEEE floating point as axioms; it showed "if
non-associative, the shape matters" generally, using subtraction as the representative non-associative operation. That is why `reduce acc sub` is rejected with
`E-PAR-ASSOC` and floating-point addition with `E-PAR-FLOAT` (#chref("parallel-atomic")).

#qa[
  Isn't "roughly the same answer" good enough?
][
  Letting it pass because the values are almost the same *makes bit comparison unusable for regression tests.* Losing reproducibility loses a debugging tool.
  Parallel sums giving different answers per core count are the most common cause of irreproducibility in scientific computing. In this language there is no
  "roughly" --- if the three conditions cannot be confirmed, translation refuses.
]

== What is not proven

- *The DRF-SC theorem itself is cited.* "Without races it behaves with sequential consistency" is a well-known theorem; what was proven here is that our discipline
  satisfies its *premise* (no races).
- *The theorems do not verify the implementation.* Level 1 parallelism is implemented, but what the theorems pin down are *the conditions the implementation must
  keep*. Whether it keeps them is backed empirically by back-end cross-checks.
- *The model assumes static read and write sets.* With pointers those sets are hard to know in advance; in practice the type system must guarantee them. "Local" is
  an assumption, not a check.
- *The state model is a "place → value" function.* Real memory with alignment, aliasing and partial writes was not modelled. Floating point itself (rounding, NaN,
  −0) was not covered either.

#recap[
  Bernstein independence is three empty intersections; at level 1 independence means no races and the condition is necessary, both proven with a counterexample.
  At level 2 an ownership message always lies between accesses by different actors, so races are absent by definition. Heavy tools are needed only at level 3.
  Having no races differs from being deterministic, and three conditions --- splitting without overlap · keeping dependency edges · associative combining --- make
  parallel results bit-for-bit identical to sequential ones. Each condition has a counterexample showing what breaks without it.
]
