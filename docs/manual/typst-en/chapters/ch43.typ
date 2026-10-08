#import "../../typst-ko/lib.typ": *

= Loops and fixed points --- proving "however many times"

#chapter-toc()

#prereq(
  ([#chref("control"), Flow], [`while` repeats its body while the condition holds]),
  ([#chref("proofs-math"), The mathematical toolkit], [a fixed point is a point that applying once more does not change]),
  ([#chref("proofs-ownership"), Proofs about ownership and borrowing], [Theorem A is about a straight sequence of events]),
)

#deepqa[
  What was the first line of "What is not proven" for Theorem A in #chref("proofs-ownership")?
][
  That the model is a straight sequence of events, with no branches or loops. The real compiler looks at code *once*, and that code may run *a million
  times*. One check must guarantee a million runs. This chapter is how, and the answer is one word --- fixed point.
]

#why[
  Why loops are dangerous is clear. Defects that are fine when run briefly but break when run long are the hardest to find, because tests run briefly. And
  inside a loop the same name points to a different thing each round --- the borrow in round 1 and the borrow in round 2 are the same `r` in the source but
  different borrows at run time. This chapter solves both difficulties with fixed points and fresh tokens, and shows there is *exactly one* shape in which
  borrows break in loops. It is a case of knowing how many rules are needed by argument rather than by feel.
]

#organizer[
  You will confirm with examples the shape where a borrow living across a loop breaks on the second round, and the safe shape where a borrow is born and ends
  within a round. You will learn Theorem B --- if the static state returns after one round, it is safe however many times it runs --- the method of issuing a
  new token each round to remove the naming problem, and the invariant FRESH added to the proof. You will also see the generalisation to nested loops that
  run differently each round by writing unfolding as a relation, and why termination is not covered.
]

#chapter-questions()

== It breaks on the second round

#demo("examples/ch43/loop_stale.low")

A read borrow `r` created outside the loop is used inside it, while inside the same loop the owner writes to `x`. Round 1 looks fine. But that write kills
`r`, and round 2's `deref r` uses a dead borrow. It is fine when run once and blows up the second time. The processor rejects it at translation.

#demo("examples/ch43/loop_fresh.low")

Conversely, if the borrow *is born and ends inside* the loop, it is fresh every round, and nothing stale remains between rounds. The common advice "create it
inside the loop and finish it inside the loop" has a proof attached.

== At a fixed point, any number of times

#idx("fixed point")
Here the function `f` is "running the loop body once", and the value `x` is *the static checker's state* (the list of live borrows). If one run of the body
leaves the static state unchanged, that is a fixed point. Then induction holds.

```text
Running 0 times is safe.                                         base --- nothing was done
Running once safely from state L returns the state to L.         preservation
─────────────────────────────────────────────
Therefore it is safe however many times it runs.                 ∀k
```

It is like dominoes needing equal spacing. If the state changed every round, there would be no argument for "the next piece". Because the state returns, the
same argument can be reused endlessly.

#mathbox[Theorem B --- loop agreement (`loop_agreement` in `LowentLoop.v`)][
  `forall pre body, (exists lpre, srun [] pre = Some lpre /\ srun lpre body = Some lpre) -> forall k, dyn_clean pre body k = true.` If, from the static state
  after setup code `pre`, running the loop body `body` once *returns to the same state*, the loop has no borrowing violation however many times it runs.
  `forall k` is the whole theorem --- 0 times, once, 2#super[64] times. `k = 0` is included. It looks trivial, but leave it out and the induction fails.
]

Some code does not reach a fixed point. If a borrow is added each round, never dies, and escapes the loop, the static state grows. Such code does not satisfy
the theorem's premise, and the processor rejects it. When a borrow is born and dies in the body, the state returns --- that is ordinary code.

== Only one dangerous shape

Something was learned while proving. There is *only one way* borrows break in loops.

```text
A borrow T created in pre is used in body, while body also contains owner access own(x).
  → own(x) in round i kills T.
  → use(T) in round i+1 is a violation.
```

One static rule rejects exactly that pair --- a borrow living across the loop × owner access in the body --- and the proof of Theorem B shows "that one is
enough". There is no other danger. This is the proof's *practical output*. Too few rules leak defects and too many block normal code, and the argument decided
how many is right.

== Fresh tokens --- one move that removes the naming problem

The real headache in loops is *name clashes*. The `r` of round 1 and the `r` of round 2 are the same name in the source but different borrows at run time. Handling
that usually means building a "tag renaming" device.

This proof built no such device. Instead the model *issues a new token every time it runs*. A static tag `t` points through an environment to "the current token",
and that token gets a new number each round. Then last round's token is pointed to by no one and becomes invalid by itself. This is no trick --- *the
implementation really issues fresh tags.* Because the model followed the implementation, the proof got shorter. If a proof is hard, look at the model again.
Half the difficulty comes from a wrong representation.

The proof adds one invariant to #chref("proofs-ownership")'s INV and SINV.

- *FRESH* --- every issued token is smaller than `next`.

Without it a new token could receive the same number as an old one, and a dead borrow would *come back to life*. It is the scariest kind of hole in a proof ---
wrong on the dangerous side, not the safe side.

== Nested loops --- from function to relation

#demo("examples/ch43/nested.low")

The inner loop runs 0, 1, 2 and 3 times on successive outer rounds. The borrow `mut_ref x` is born and ends in the inner body, so it is safe in every round.

The flat Theorem B imposes `unroll body k` --- "every round runs the same". Real programs do not. The nested loop proof (`LowentNest.v`) represents the program
as a tree (`NLeaf`, `NSeq`, `NLoop`) and writes unfolding *as a relation, not a function*.

#mathbox[Nested loops (`nested_agreement`)][
  A nested loop program passing the static check has no borrowing violation *under any unfolding*. There is no depth limit, no iteration limit, and *rounds may
  run differently*. Written as a relation, "the inner loop runs three times in round 1 and zero times in round 2" is simply another unfolding of the same tree.
  And the old flat theorem is recovered as a corollary (`flat_loop_recovered`). If it could not be recovered, it would not be a generalisation but a different
  theorem.
]

Loops whose first round changes the state fit too. Peeling once as `b ; loop b` leaves a fixed point afterwards (`peeled_loop_is_safe`).

#qa[
  How does the compiler find the fixed point? Does the theorem guarantee that too?
][
  The theorem *assumes* the fixed point at the entry. The compiler *reaches* it by merging the states of branches and rounds (with the lattice's join and monotone
  iteration), but that process is outside the model. The gap is backed by bounded exhaustive checking --- the loop checker runs all 90,376 shapes for rounds 1 … 3
  and checks that model and implementation agree. This is "exhaustively checked", not "proven".
]

#misconception[If following the loop body once shows no problem, any number of rounds is fine][
  `loop_stale.low` is the counterexample: round 1 is fine, and round 2 uses a dead borrow. "Following it once" is enough only when the static
  state after one pass *equals the state before*, that is, at a fixed point. So the checker applies the body repeatedly until the state stops
  changing, not just once, and this chapter's theorem guarantees that stopping there is correct. The same holds when a person reads code:
  ask "what is alive when the second round begins?"
]

== What is not proven

- *Merging `if` branches is not in Theorem B.* The model is two straight sequences `(pre, body)`. The correctness of merges is backed by the exhaustive checking
  above.
- *The process of reaching the fixed point* is outside the model. The theorem speaks of after it is reached.
- *Whether loops terminate is not covered.* The theorem is "safe if it runs", not "it ends". Infinite loops, like a firmware main loop, are legitimate programs in
  this language. That is why tests the tool generates for itself carry a step budget (#chref("proofs-bounds")) --- termination cannot be guaranteed, so the tool
  must know when to stop.

#recap[
  A borrow living across a loop meeting owner access in the body breaks on the second round, and the processor rejects that one pair. A borrow born and ending in
  the body is fresh every round. Theorem B --- if the static state returns after one round (a fixed point), there is no borrowing violation however many times it
  runs --- is induction on `k`, and a model issuing new tokens each round with the invariant FRESH removes the naming problem. Nested loops were covered, including
  rounds that run differently, by writing unfolding as a relation. Merges and reaching the fixed point are backed by exhaustive checking, and termination is not
  covered.
]
