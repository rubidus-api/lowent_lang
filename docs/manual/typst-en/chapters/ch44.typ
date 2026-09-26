#import "../../typst-ko/lib.typ": *

= Proofs about effects --- writing "what it can do" into the type

#chapter-toc()

#prereq(
  ([#chref("effects"), Effects], [effects spread along calls, and purity permits reordering, remembering and deleting]),
  ([#chref("capabilities"), Capabilities], [effects write what kind of work, capabilities write who permitted it]),
  ([#chref("proofs-math"), The mathematical toolkit], [lattices and join]),
)

#deepqa[
  In #chref("effects"), what separated `fn` from `proc`, and what did the promise of purity permit the processor to do?
][
  `fn` leaves no trace outside and `proc` leaves as much as its written effects. A pure op gives the same answer for the same arguments, so the processor may
  reorder calls, remember results, or delete unused calls. This chapter covers the proof that the promise *really holds* --- and why that proof is not circular.
]

#why[
  #chrefs("proofs-numbers", "proofs-loops") were about "how memory is touched". This chapter is about "what it can do". `effects none` is a strong promise --- it
  reads no globals, allocates nothing, writes nothing to the screen, waits for nothing. If the optimiser trusts that promise and deletes a call that actually
  wrote a file, observable behaviour disappears. The effect system does not give memory safety. Instead it gives a different kind of safety --- legitimate
  optimisation, auditability, rejection on small machines. That safety comes from trusting declarations, so a theorem that declarations cover reality is needed.
]

#organizer[
  You will learn that effects are sets of atoms forming a lattice under subset order, and that propagation and gating are two operations of one lattice. You will
  confirm with examples the four rules the processor enforces (propagation · purity · a set of atoms · stopping is not an effect), and pick up attenuation and local
  audit, which capabilities give as the partner of effects. You will also see why the effect soundness theorem is not circular, and the proof, with
  counterexamples, that four optimisations relying on effects are legitimate.
]

#chapter-questions()

== Effects are sets of atoms

#idx("effect lattice")
An effect is *a set of atoms*. It can hold several atoms, as in `effects io alloc .`. Then there is a natural order --- `ε₁ ⊑ ε₂` is `ε₁ ⊆ ε₂`, "`ε₁` does less
than `ε₂`". The subset relation forms a lattice, the join is union, and `none` (the empty set) is the bottom.

#dtable(
  columns: 3,
  id: "peff-lattice",
  caption: [Two jobs done by one lattice],
  [*What*], [*Lattice operation*], [*Meaning*],
  [Propagation], [`⊔` (union)], [Gathering the effects of everything I call gives my effect],
  [Gating], [`⊆` (subset)], [Doing less than declared is fine],
)

It is *the same shape* as #chref("proofs-numbers")'s type lattice. The same mathematics answers a different problem. And the judgement on small machines becomes
one `⊆`. Code that allocates must not translate for a machine without a heap (#chref("hardware")), and with effect declarations it is enough to ask "is this op's
effect within the set this tier supports".

```text
            {io, alloc}
             ▲       ▲
            {io}     {alloc}
             ▲       ▲
              none

 propagation  if f calls g (effects io) and h (effects alloc),
              effects(f) ⊇ {io} ⊔ {alloc} = {io, alloc}      short of it: E-EFFECT
 gating       if the set a heapless board supports has no alloc,
              {io, alloc} ⊄ that set                         it does not translate for that board
```

A *hierarchical* lattice splitting `io` into `{read, write}` is possible too. This language uses a *flat* one for now. The precision a hierarchy gives is already
available from capabilities, and a flat lattice keeps the `⊆` test simple so the cost is visible. Express precision in two places and the two begin to diverge.

== Four rules the processor enforces

*Rule 1 --- propagation.* If `f` calls `g`, then `effects(f) ⊇ effects(g)`. Otherwise it is `E-EFFECT` (`E-EFFECT-CALC` for a `fn`). It stops an op marked
`effects none` from secretly writing a file (#chref("effects")).

*Rule 2 --- purity.* A `fn` writing to the caller's buffer is `E-EFFECT-PURITY`. `fn` computes values and `proc` does work. If the criterion is *the shape of a
word*, holes appear. At one time an actor's handlers were counted as pure because they were not `proc`, so a handler mutating actor state could declare
`effects none` and nobody objected. The same offence was caught on one side and not the other.

*Rule 3 --- an `effects` clause is a set of atoms.*

#demo("examples/ch44/effect_dup.low")

#demo("examples/ch44/effect_typo.low")

Writing the same atom twice is rejected. A duplicate in a set means nothing, and the reader has to decide whether it is noise. A nonexistent atom is caught as a
typo --- as the diagnostic says, a typo passing silently would *declare the op pure*. The rule looks obvious, but there was a time the implementation read only
*the first word* of the clause. Then `effects io alloc` registered only `io`, and atoms from the second on were as good as absent. A defect in the code reading
declarations disables the whole declaration system.

*Rule 4 --- stopping is not an effect.* This is the most thought-provoking part of the chapter.

#demo("examples/ch44/trap_not_effect.low")

`index s i` stops when out of range. So should `pick` carry the `panic` effect? *No.* `panic` counts only an explicit `panic "…"`. The possibility of stopping is
what *contracts* speak of, not effects. The danger of `index` *can be removed* with `requires lt i (len s)`, and the processor really does remove that check
(#chref("proofs-bounds")). An effect mark is never removed. Marking something removable as an effect makes the mark permanent, nearly every op ends up with
`panic`, and the mark loses meaning.

```text
 what ends the op              where it is written         can it disappear
 panic "…"  (intended)         effects panic               no — it always stays
 index out of range (breach)   requires lt i (len s)       the check goes once proven
```

#misconception[An op that can stop is not pure][
  Effects say "what this op does" and contracts say "when it is safe". Because the two were not mixed, the `panic` mark still carries information. An op marked
  `panic` can end the program *on purpose*. An out-of-range index is not intent but a contract violation, and contracts handle that place.
]

== Capabilities --- the partner of effects

If effects say "what kind of work", capabilities say "where the *right* to do it came from". Writing to the screen requires receiving `cap io` as an argument; it
cannot be pulled out globally from anywhere. This object-capability model gives two things.

- *Attenuation.* A received right can be narrowed before passing it on. The precision a flat effect lattice lacks is obtained here.
- *Local audit.* Looking at an op's signature shows *everything* it can touch. That there are no hidden global passages is the point.

#dtable(
  columns: 2,
  id: "peff-gives",
  caption: [Safety the effect system gives],
  [*What it gives*], [*How*],
  [Legitimate optimisation], [With `effects none`, reordering, common subexpression elimination and deletion do not change observable behaviour],
  [Auditability], [Code doing `io` can be found from declarations],
  [Tier and profile gating], [Ops allocating on small machines are stopped as translation errors],
  [No surprises], [What is written as pure really is pure],
)

== Effect soundness --- not circular

#demo("examples/ch44/recursive.low")

`stars` is recursive, calling itself, and declares `effects io`. At a call site the processor uses the callee's *declaration* rather than walking its *body* again.
At first glance this looks circular --- a declaration trusts a declaration.

#mathbox[Effect soundness (`effect_sound` in `LowentEffect.v`)][
  `prog_ok P = true -> lookup P f = Some (d, b) -> performs P (ECall f) a -> In a d.` In a program passing the check, when op `f` is called, every effect that
  actually happens is within what `f` declared. Call depth does not matter. It is not circular because the induction is over *executions*, not *programs*. When an
  effect happens, it happens at some finite call depth, and induction on that depth closes. So recursion, mutual recursion and non-terminating recursion need no
  separate treatment. The proof file includes an example where a self-calling op passes the check, `io` actually happens, and `alloc` *cannot* happen.
]

== Optimisations relying on effects are legitimate

The legitimacy of optimisations stands on effect soundness. At the effect layer, pure pieces may be reordered (`pure_calls_commute`) or deleted
(`dropping_pure_is_legal`) with the same effects happening. The proof raised to the value layer is `LowentOpt.v`. In a model with values, state and an output
stream, reordering, common subexpression elimination, memoisation and dead code elimination were all four proven legitimate.

The key is one line --- *a value depends only on the places it reads* (`value_depends_only_on_reads`). And counterexamples showed the premises are not decoration.
Reordering effectful pieces *changes* a value from 14 to 7, and deleting a piece that prints shrinks the output. One theorem, one counterexample --- without the
counterexample, the theorem would be true even if the condition were overly strict.

#qa[
  Why does using a reserved word as a parameter name give a name diagnostic rather than an effect diagnostic?
][
  At one time it was the other way round. Naming a parameter `raw`, the raw-pointer word, silently contaminated effect inference and produced "declared pure but
  performs an effect" (`E-EFFECT-CALC`). The real reason was "a reserved word was used as a name", but the diagnostic pointed at purity. A diagnostic that sends
  the programmer to the wrong place is the worst kind. So those words were handed to `E-NAME-BUILTIN`, leaving one diagnostic for one offence. Proofs decide what
  is rejected, but people meet why --- if the latter is wrong, the former loses half its value.
]

== What is not proven

- *That the implementation matches the model* is an empirical correspondence backed by tests and back-end cross-checks (#chref("proofs-limits")).
- The rule by which `task_group` *absorbs* `concurrent` is not a `⊆` discipline but discharging a requirement, so it is outside the model.
- In effect-based optimisation, *call boundaries* (raising the purity judgement to op boundaries) and *the moment of stopping* are outside.
- *The precision limit of a flat lattice.* Refinements like `io ⊑ {read, write}` were not done, and capability attenuation stands in for them, with no argument
  that the two give the same precision.
- *No effect-row polymorphism.* An op "inheriting the caller's effects as they are" cannot be written, so higher-order ops must declare the widest effects.
- *Some atoms are vocabulary only.* `page_fault`, `blocking`, `cancel`, `detach` and others can be declared but have no operation producing them yet, so
  propagation of those atoms is thinly tested in practice.

#recap[
  Effects are sets of atoms forming a lattice under subset order; propagation is union and gating is a subset test. The processor enforces four rules --- propagation
  · purity · a set of atoms (rejecting duplicates and typos) · stopping is not an effect --- and capabilities, as the partner of effects, give attenuation and local
  audit. Effect soundness is induction over executions, so it is not circular even with recursion, and on top of it reordering, common subexpression elimination,
  memoisation and dead code elimination were proven legitimate along with counterexamples.
]
