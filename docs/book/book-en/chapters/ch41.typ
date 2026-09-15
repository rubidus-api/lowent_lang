#import "../../book/lib.typ": *

= Proofs about effects and concurrency

#chapter-toc()

#prereq(
  ([#chref("effects"), Effects], [purity permits reordering, remembering and deleting]),
  ([#chref("parallel-atomic"), Parallel loops and atomic operations], [split answers equal sequential ones · memory orderings]),
  ([#chref("proofs-ownership"), Proofs about ownership and borrowing], [passing translation means no borrowing violations]),
)

#deepqa[
  In #chref("parallel-atomic"), why did the `W-PAR-OK` note say that the VM running sequentially is *a correct implementation*?
][
  Because a theorem is proven that when the splittable conditions hold, the parallel result is bit-for-bit identical to the sequential result. This chapter gathers that theorem
  along with the other proofs about effects and concurrency.
]

#why[
  Concurrency is known as the place where proofs are most expensive. But what this repository learned was that *reading the proposition exactly reduces the tools needed*. "Safe
  code has no data races" turned out to be a set-theoretic claim about discipline, not a claim about a memory model, and the one place heavy concurrency logic was really needed
  was the lock. This chapter starts from the soundness of the effect system, then looks in turn at the layers where discipline prevents races (levels 1 and 2) and the layer that
  must face the memory model head on (level 3).
]

#organizer[
  You will learn the effect soundness theorem ("effects that actually happen are within the declaration"), why it is not circular, and the legitimacy of optimisations relying on
  effects. You will pick up the theorems that level 1 parallelism and level 2 actors have no data races by discipline, that parallel results are bit-for-bit identical to
  sequential ones, and the role of associativity. You will also see the theorem that with the default memory ordering `seq_cst` you may think sequentially, the monotonicity that
  weaker orderings add behaviours, and proofs about locks, rwlocks, deadlock and starvation, including *borrowed* proofs.
]

#chapter-questions()

== Effect declarations cover actual effects

#demo("examples/ch41/recursive.low")

`stars` is recursive, calling itself, and declares `effects io`. At a call site the compiler uses the callee's *declaration* rather than walking its *body* again. At first
glance this looks circular --- a declaration trusts a declaration.

#mathbox[Effect soundness (`effect_sound` in `LowentEffect.v`)][
  `prog_ok P = true -> lookup P f = Some (d, b) -> performs P (ECall f) a -> In a d.` In a program passing the check, when op `f` is called, every effect that actually happens is
  within what `f` declared. Call depth does not matter. It is not circular because the induction is over *executions*, not *programs*. When an effect happens, it happens at some
  finite call depth, and induction on that depth closes. So neither recursion nor mutual recursion needs separate treatment.
]

The legitimacy of optimisations stands on this theorem (`LowentOpt.v`). In a model with values, state and output streams, reordering, common subexpression elimination,
memoisation and dead code elimination were all four proven legitimate. The key is one line --- *a value depends only on the places it reads* (`value_depends_only_on_reads`). And
counterexamples showed the premises are not decoration. Reordering effectful pieces changes a value from 14 to 7, and deleting output shrinks the output. What remains are call
boundaries and the moment of stopping.

== Discipline prevents races

A data race is two flows touching the same place at the same time with at least one writing. The usual way to handle races is to learn a memory model, but this language's claim
is different --- *level 1 and 2 code never needs to look at the memory model*, because races are ruled out by discipline. This claim is proven in pure Coq (`LowentDRF.v`).

- *Level 1 --- split loops* (`l1_no_race`). If two tasks are Bernstein independent (one's write set does not overlap the other's read or write set), they race at no place. And *the
  condition is necessary* (`l1_condition_is_necessary`) --- actually building a pair that breaks it produces a race.
- *Level 2 --- actors* (`l2_accesses_are_separated_by_a_message`). If different actors access the same place, there is always a message handing over that place's ownership between
  the two accesses. A message is an edge fixing before and after, and two accesses with a fixed order are by definition not a race.

The connection to #chref("proofs-ownership") is the point. Enforcing "only one name writes" within one flow becomes exactly "two tasks do not overlap". One safety rule pays off in
two places.

#misconception[Proving concurrency always needs heavy tools like separation logic][
  This repository planned so at first too. Looking again, "discipline prevents races" was a set-theoretic claim. Among the proof files, only the lock uses heavy concurrency logic
  (Iris); the other concurrency theorems are pure Coq. "It is concurrency, so separation logic" was a reflex.
]

== Parallel equals sequential, bit for bit

No races is not the end. Is the result the same as sequential? Three theorems in `LowentPar.v` answer.

- *DET-1* (`det1_par_eq_seq`, `det1_bit_identical`). If tasks are pairwise independent and each looks only at its own read set, the result of parallel execution equals sequential
  execution. A counterexample where overlap breaks determinism is proven too (`overlap_is_nondeterministic`).
- *DET-2*. Swapping the order of two non-conflicting tasks gives the same result. A scheduler need only respect dependency edges.
- *DET-3* (`assoc_shape_free`, `nonassoc_shape_matters`). If the gathering operation is associative, the shape of the reduction tree does not change the result; if not, it does.

The three conditions the processor checked in #chref("parallel-atomic") (`E-PAR-READ`, `E-PAR-WRITE`, `E-PAR-CARRY`) and `E-PAR-ASSOC` are exactly the premises of these theorems.
The theorems do not verify the implementation. They *fix the conditions the implementation must keep*. Conditions invented afterwards loosen to fit the implementation, so this order
is better.

== With the default, you may think sequentially

For atomic operations (level 3) there is no discipline. Two flows really do hammer the same place, and CPUs and compilers reorder instructions. This language exposes memory orderings
as `relaxed` \< `acquire`, `release` \< `acq_rel` \< `seq_cst` and makes `seq_cst` the default. This design rests on two propositions --- with the default you may reason with
sequential consistency, and weaker orderings really do break it. Both must be proven for the decision to be justified.

#demo("examples/ch41/handoff.low")

The shape above is why weak orderings exist. Write data first (`relaxed`), write a flag with `release`, and when another flow reads the flag with `acquire`, the earlier data write is
guaranteed visible.

#dtable(
  columns: 2,
  id: "peff-rc11",
  caption: [Theorems of the weak memory (RC11) model],
  [*Theorem*], [*In plain words*],
  [`sb_sc_impossible`], [If every access is `seq_cst`, store buffering's "both read 0" outcome is impossible],
  [`sb_relaxed_is_consistent`], [With `relaxed` that outcome satisfies every axiom --- it can really happen],
  [`mp_relacq_forbids_stale`], [Writing a flag with `release` and reading with `acquire` cannot see stale data],
  [`sb_relaxed_has_a_race`], [That bad execution contains a race --- so levels 1 and 2 cannot produce it],
  [`all_sc_is_sc`], [*Every* consistent execution where all accesses are `seq_cst` is sequentially consistent],
  [`consistent_monotone`], [The same execution with only weaker orderings is also consistent --- weaker orderings add behaviours],
)

Turn the monotonicity theorem around and it becomes a practical sentence --- *writing a stronger ordering is always the safe direction.* That is the ground for making `seq_cst` the
default. The literature's seven standard counterexamples were exhaustively checked in those tests.

== Locks, and borrowed proofs

A lock itself cannot get safety from discipline. A spinlock is a device where two threads really do hammer the same word at the same time. Only here was Iris used (`LowentLock.v`).
It is proven that a CAS spinlock gives mutual exclusion, that acquiring receives both a token and the resource, and that releasing requires returning both. For rwlocks the
exclusivity of the write lock was proven directly, and fractional ownership on the read side was *borrowed* from Iris's verified `rw_spin_lock` library.

Deadlock freedom (`no_deadlock`) is the theorem that if every thread takes locks only in ascending order, some thread can always progress. Look at the thread wanting the largest
lock --- if it is blocked, the holder of that lock wants something larger still, contradicting maximality. But *the tool does not enforce this discipline*. No starvation
(`no_starvation`) is a bounded theorem that in a round-robin cooperative scheduler a ready task runs within the number ahead of it + 1 steps, and it assumes yielding.

Message passing under weak memory and the correctness of the SPSC ring buffer borrowed existing proofs in weak memory logics (iRC11, gpfsl). The standard library's `spsc` is that
algorithm. MPSC, MPMC and seqlocks have no proof to borrow, so they were left out (#chref("lib-containers")). Borrowed proofs enter the trusted base. There is one reason not to rebuild
them --- keep two copies of the same thing and they diverge.

#qa[
  If there is a proven lock, why is using `lock` in this edition `E-LOCK-NOTYET`?
][
  What was proven is a CAS spinlock written in Iris's language, and that the real runtime's lock matches it is not yet verified. And the shared lock state type has not been built
  yet. This is a place where proof comes first and implementation later. Instead of accepting what does not exist and building it later, it says it does not exist
  (#chref("tasks-channels")).
]

== What is not proven

- The rule by which `task_group` absorbs `concurrent`, and effect closure, are outside the effect model. There is no effect-row polymorphism.
- In effect-based optimisations, *call boundaries* and *the moment of stopping* are outside.
- Level 1 parallelism is implemented, but the theorems do not verify the implementation. Back-end cross-checks back it empirically.
- Among code stating weak orderings, only SPSC has a proof. The rest is subject to audit.
- Deadlock freedom for locks relies on a discipline people keep. Priorities and blocking are outside the starvation theorem.

#recap[
  Effect soundness is induction over executions and so not circular, and on it reordering, common subexpression elimination, memoisation and dead code elimination are legitimate. At
  level 1 (Bernstein independence) and level 2 (actors' ownership messages) data races are absent by discipline, parallel results are bit-for-bit identical to sequential ones, and
  gathering operations must be associative. If every access is `seq_cst`, every execution is sequentially consistent, and weaker orderings add behaviours. Locks were confirmed with
  Iris, and read locks and SPSC with borrowed proofs.
]
