#import "../../typst-ko/lib.typ": *

= Proofs about weak memory --- with the default, you may think sequentially

#chapter-toc()

#prereq(
  ([#chref("parallel-atomic"), Parallel loops and atomic operations], [the five memory orderings `order` and the default `seq_cst`]),
  ([#chref("proofs-math"), The mathematical toolkit], [partial orders --- orders that allow incomparable pairs]),
  ([#chref("proofs-races"), Proofs about races and parallelism], [levels 1 and 2 remove races by discipline]),
)

#deepqa[
  Where was a memory model needed in #chref("proofs-races")'s containment picture?
][
  At level 3 --- atomic operations and locks. There two flows really do hammer the same place, so discipline cannot remove races. This chapter looks head on at
  the weak memory model that decides "what can be seen, and when" there.
]

#why[
  CPUs and compilers reorder instructions. With one flow it goes unnoticed, but when several flows look at the same memory it shows --- others do not see things
  in the order I wrote them. Lowent exposes five strengths for atomic operations (`relaxed` \< `acquire`, `release` \< `acq_rel` \< `seq_cst`) and makes the
  strongest, `seq_cst`, the default. The weak ones must be written to be obtained, and that code is subject to audit. This decision rests on two propositions:
  that with the default you may think sequentially, and that the weak ones *really* break that. Prove only one and the decision is not justified. Without the
  first, "default `seq_cst`" is only comfort; without the second, "write it and audit it" is an exaggeration.
]

#organizer[
  You will learn what sequential consistency is and the standard counterexample that breaks it (store buffering), and the RC11 model, which views an execution as a
  graph with several kinds of edges and judges that an execution with a cycle does not exist. You will pick up the pair of theorems that the counterexample is
  impossible with the default and possible with `relaxed`, exactly which axiom forbids it, and the theorem that `release` and `acquire` really synchronise message
  passing. You will also see the general theorem over all executions, monotonicity (weaker orderings add behaviours), and the model defect that proof found.
]

#chapter-questions()

== Sequential consistency and its counterexample

#idx("sequential consistency")
*Sequential consistency* (SC) means "all accesses of all flows *can be lined up in one row*, with the order within each flow kept in that row". It is the picture
people naturally imagine. Real hardware does *not* behave this way.

```text
initially: x = 0, y = 0

flow 1:  x ← 1        flow 2:  y ← 1
         r1 ← y                r2 ← x

bad outcome:  r1 = 0  and  r2 = 0
```

It is the standard counterexample called *store buffering*. Why is it bad? It cannot be lined up. `r1 = 0` puts flow 1's read before flow 2's write, and `r2 = 0`
puts flow 2's read before flow 1's write. But within each flow the write comes before the read. Contradiction. Yet *this outcome happens on real x86.*

== An execution is a graph

#idx("RC11")
The reference model is RC11 (Lahav et al., PLDI 2017). It views an execution as a graph. Events are vertices, and there are several kinds of edges.

#dtable(
  columns: 2,
  id: "pwm-edges",
  caption: [RC11 edges],
  [*Edge*], [*Meaning*],
  [`hb` (happens-before)], [The order is guaranteed],
  [`rf` (reads-from)], [This read read that write's value],
  [`mo` (modification order)], [The global order of writes to one place],
  [`fr` (from-read)], [This read comes *before* that write --- it read an older value],
)

The judgement is one sentence. *If a cycle appears on some axis, the execution does not exist.* That is all the mathematics this chapter needs --- does a directed
graph have a cycle. If you have learned topological sorting, you know the idea. RC11 is hard not because of the idea but because there are many kinds of edges.

== Impossible with the default, possible with `relaxed`

#mathbox[E1 --- `sb_sc_impossible`][
  `consistent (SB SC) rf_00 mo_SB = false.` If every access is `seq_cst`, the outcome "both read 0" violates the consistency axioms --- no such execution exists.
  See the cycle directly: `Wx1 —hb→ Ry0 —fr→ Wy1 —hb→ Rx0 —fr→ Wx1`. To read `fr`: `Ry0` read the initial value of `y`, which `Wy1` overwrote, so `Ry0` comes
  before `Wy1`. The four edges close a loop, so this execution cannot exist.
]

#mathbox[E2 --- `sb_relaxed_is_consistent`][
  `consistent (SB Rlx) rf_00 mo_SB = true.` With `relaxed`, that bad outcome *satisfies every axiom* --- it can really happen. The sequential consistency axiom
  speaks only of `seq_cst` events, so with only `relaxed` it is vacuously true. That is both the value and the danger of `relaxed`. Removing constraints makes it
  fast, but what was removed is exactly the safety.
]

E1 turns the sentence "default `seq_cst`" from comfort into a *theorem*. Code using only `seq_cst` may be imagined lined up in a row.

== Exactly that axiom forbids it

#mathbox[E3 --- which axis does the work][
  `coherence_alone_does_not_forbid_sb` --- even with `seq_cst`, the coherence axis alone does not forbid store buffering. `it_is_the_sc_axiom_that_forbids_it` ---
  what forbids it is the sequential consistency axis.
]

#realcase[The machine taught us the model was wrong][
  The first model written *let* store buffering pass even with `seq_cst`. Axioms were missing. The prover exposed that, and the axioms were added. The two theorems
  of E3 pin the lesson down. With which axis does the work written down, the theorem breaks when someone later deletes the axiom. The real value of a proof tool lies
  here more than in obtaining theorems.
]

== Message passing --- the weak ones exist for this shape

E1 to E3 said "weak is dangerous". Then why do the weak ones exist? Exactly for this shape.

#demo("examples/ch46/handoff.low")

Write the data first (`relaxed`), then write the flag with `release`. The other flow reads the flag with `acquire`, and if it is 1, reads the data. The bad outcome
is a stale value: "read flag 1, yet read data 0".

#mathbox[E4 --- message passing][
  `mp_relacq_forbids_stale` --- with `release` and `acquire`, seeing the stale value is *impossible*. `mp_relaxed_allows_stale` --- with both `relaxed`, it is
  *possible*. `mp_sync_is_what_forbids_it` --- what makes the difference is the synchronisation edge `sw`, standing *between* the `release` write and the `acquire`
  read. The argument: `sw` creates an `hb` from the data write to the data read. But the data read read the initial value and the data write is later by `mo`, so an
  `fr` in the opposite direction stands. A cycle forms on the same place, so the coherence axis forbids it.
]

E4 pairs with E1. E1 says "the default is safe", E4 says "writing it synchronises". Both sides of the design decision are on the machine.

#mathbox[E5 --- `sb_relaxed_has_a_race`][
  An execution yielding store buffering's `(0, 0)` *contains a race*. But as #chref("proofs-races") showed, levels 1 and 2 cannot produce such executions. So level 1
  and 2 code need not read this chapter --- weak memory is a level 3 problem only, and containment holds.
]

== Over all executions

Most of the theorems above end with `vm_compute. reflexivity.` Because `consistent` was defined as a decision function returning true or false, computing that
function on a particular execution finishes the proof. It is short, and when the model changes it re-checks automatically. The price is that it is a result about
*particular executions*. The standard counterexamples are exactly what the design decision hangs on, so it suffices to justify it --- but it did not stop there.

#mathbox[If every access is `seq_cst`, it is sequentially consistent (`all_sc_is_sc` in `LowentRC11SC.v`)][
  `all_sc E = true -> consistent E rf mo = true -> sc_com_acyclic E rf mo = true.` If every access is `seq_cst` and the execution is RC11-consistent, then
  `po ∪ rf ∪ mo ∪ fr` has no cycle --- for *all executions · all rf · all mo*. In axiomatic memory models this is the standard definition of "this execution is
  sequentially consistent". The engine of the proof is one line --- *a subrelation inherits acyclicity.*
]

And it went a step further than "no cycle". Instead of arguing abstractly that "acyclic means a total order exists", it *built topological sorting as a function* and
proved its result is a genuine linear extension (`topo_linearises`, `sc_witness_orders_everything`). All accesses were laid out in one row, and that row keeps all of
program order and communication order. For the good store buffering execution the row `[0; 1; 2; 4; 3; 5]` actually comes out.

== Weaker orderings add behaviours

#mathbox[Monotonicity (`consistent_monotone` in `LowentRC11Mono.v`)][
  The same execution with only memory orderings weakened is also consistent --- for all executions · all rf · all mo · all assignments. That is, *writing weaker
  orderings adds possible behaviours (never removes them).* At the edge level too, weakening removes synchronisation edges and adds none (`sw_weaken`). With
  `sc_is_the_strongest` and `rlx_is_the_weakest`, the strength order itself is a theorem.
]

Turn it around and it becomes a practical sentence --- *writing a stronger memory ordering is always the safe direction.* That sentence is the ground for making
`seq_cst` the default, and it is a theorem, not a belief.

#realcase[A defect in the order found by the monotonicity proof][
  The old strength order placed `acq_rel` *above* `seq_cst`, because it counted only synchronisation edges and ignored the sequential consistency axiom. Yet store
  buffering is consistent under `acq_rel` and inconsistent under `seq_cst`. Under that order monotonicity was *false* (`old_order_is_not_a_strength_order` computes
  it). Why bounded exhaustive computation missed it was clear too --- those skeletons never touched the sequential consistency axis. The order was fixed, making
  `seq_cst` the unique top.
]

== Seven tests from the literature

Using only hand-picked executions cannot catch missing axioms (the failure story of E3). So the named tests from the literature were fed in (`LowentRC11Sweep.v`).

#dtable(
  columns: 3,
  id: "pwm-litmus",
  caption: [Verdicts on seven litmus tests],
  [*Test*], [*`seq_cst`*], [*`relaxed`*],
  [SB · LB · MP · 2+2W · IRIW], [Forbidden], [Allowed],
  [CoRR · CoWR], [Forbidden], [*Forbidden*],
)

The last line is the lesson. *"`relaxed` = no guarantee at all" is false.* The sequential consistency axis forbids store buffering and the coherence axis forbids
CoRR and CoWR, and the latter is always there regardless of memory ordering.

#qa[
  What happens to an atomic operation with no `order` written?
][
  It is `seq_cst`. Code with no marks is the safest. Making `relaxed` the default would make code with nothing written dangerous, gaining danger *without asking for
  it*. Danger should be obtained by writing it. And combinations meaningless for an operation --- `release` on a read, `acquire` on a write --- are rejected at
  translation (#chref("parallel-atomic")).
]

#misconception[When performance matters, just write every atomic operation as `relaxed`][
  As E2 shows, `relaxed` *really permits* the outcome where both read 0. Removing constraints can make it faster, but what is removed is
  exactly the guarantee that you may reason sequentially. In this edition the only code with explicit weak ordering that has a proof is
  `spsc` (#chref("proofs-locks")); everything else is subject to audit. Write with the default `seq_cst`, measure, then pick the one place
  that must be weakened and write down the argument for that place.
]

== What is not proven

- *A program logic verifying a particular lock-free algorithm under `relaxed`* is not in this chapter. The metatheorems within the model (sequential consistency ·
  monotonicity · order extension) are proven, and the only program verified with a program logic is the SPSC ring buffer, with a borrowed proof
  (#chref("proofs-locks")).
- *"If acyclic, topological sorting always succeeds"* is not there yet. For now the order is actually obtained and known correct, and failure shows as `None` --- it
  never silently gives a wrong order.
- *The model is RC11 transcribed by hand.* Whether it matches the paper's definitions was checked by people reading, and it was wrong once (E3).
- *The tests are a sample.* No test uses fences (this model has no fence events), and each skeleton has one rf and mo --- the bad outcome.
- *Whether the compiler keeps this model is separate.* Memory orderings are emitted one-to-one as C11 atomics, so the rest is the C compiler's job (the trusted base).
  That mapping and "atomic operations are not reordered" are confirmed by tests.

#recap[
  Sequential consistency means all accesses can be lined up in one row, and store buffering is its standard counterexample. RC11 views an execution as a graph with
  four kinds of edges and judges that an execution with a cycle does not exist. If every access is `seq_cst` the counterexample is impossible, with `relaxed` it is
  possible, and what forbids it is the sequential consistency axiom. `release` and `acquire` really synchronise message passing, and such bad executions contain races,
  so they have nothing to do with levels 1 and 2. For all executions `seq_cst` is sequentially consistent, and weaker orderings add behaviours, so writing stronger is
  always the safe direction.
]
