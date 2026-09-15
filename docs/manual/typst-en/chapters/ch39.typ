#import "../../typst-ko/lib.typ": *

= The mathematical toolkit

#chapter-toc()

#prereq(
  ([#chref("proofs-why"), Why prove], [tests show existence (∃), proofs show absence (∀)]),
  ([#chref("numbers"), Numbers], [width and sign fix a type, and widening happens automatically only when it keeps the value]),
  ([#chref("control"), Flow], [`while` repeats its body while the condition holds]),
)

#deepqa[
  What were the three ways of proving in #chref("proofs-why")?
][
  Invariant preservation (a step carries truth to truth), reduction to an order structure (turn the problem into properties of an order) and
  exhaustive checking (run every case of a fixed size --- not a proof). The first two need mathematical vocabulary. This chapter gathers that
  vocabulary from scratch.
]

#why[
  The rest of Part X uses the words partial order, lattice, fixed point, induction and abstract interpretation without explanation. They look hard,
  but their meaning is mostly high-school mathematics --- sets, inequalities, induction falling like dominoes. This chapter attaches the six tools
  to real Lowent programs once each. The symbols learned here are used unchanged in every later chapter. It is not a chapter that proves anything
  but one that prepares you to read.
]

#organizer[
  You will learn sets and relations, partial orders that allow incomparable pairs together with their three conditions, and the join and lattices
  used when combining two values. You will see that monotone functions and fixed points make loop analysis terminate, that induction and invariants
  show "however many steps", and abstract interpretation, which computes with ranges instead of values, along with the direction of its soundness.
  You will also see a table of symbols and how the six tools interlock in later chapters.
]

#chapter-questions()

== Six tools

#dtable(
  columns: 3,
  id: "math-tools",
  caption: [The mathematics later chapters use],
  [*Tool*], [*In one line*], [*Where it is used*],
  [Sets and relations], [What exists, and what pairs with what], [Everywhere],
  [Partial orders], [Comparison of size that allows incomparable pairs], [#chrefs("proofs-numbers", "proofs-weak-memory")],
  [Lattices and join], [The smallest thing holding both when combining two], [#chrefs("proofs-numbers", "proofs-effects")],
  [Monotone functions and fixed points], [The point that no longer changes --- loop analysis stops there], [#chrefs("proofs-bounds", "proofs-loops")],
  [Induction and invariants], [With a first piece and a rule for passing it on, every piece falls], [#chrefs("proofs-ownership", "proofs-loops")],
  [Abstract interpretation], [Compute with ranges of values instead of values], [#chref("proofs-bounds")],
)

== Sets and relations

A *set* is a collection --- `{0, 1, 2}`. A *relation* is a list of pairs. The relation "less than" is a collection of ordered pairs such as
`{(0,1), (0,2), (1,2)}`. In talk about programs, a relation "widens safely" is placed over the set of types `{u8, u16, u32, u64, i8, …}`. That
relation is written `⊑` and read "the left fits into the right without changing value". It resembles `≤`, but it speaks not of numeric size but of
*whether it can be held*.

== Partial orders --- incomparable pairs

#idx("partial order")
Numbers are always comparable. Of 3 and 5, one is larger. Types are not. `u8 ⊑ u16 ⊑ u32 ⊑ u64` and `i8 ⊑ i16 ⊑ i32 ⊑ i64`, but neither `u32` nor
`i32` holds all of the other. `u32` holds up to four billion and `i32` holds negatives. The structure that honestly captures the fact that neither
can be called larger is a partial order.

#dtable(
  columns: 3,
  id: "math-poset",
  caption: [The three conditions of a partial order],
  [*Condition*], [*Meaning*], [*Example*],
  [Reflexivity], [Always fits into itself], [`u8 ⊑ u8`],
  [Transitivity], [Stepping stones connect], [`u8 ⊑ u16` and `u16 ⊑ u32` give `u8 ⊑ u32`],
  [Antisymmetry], [Fitting both ways means the same], [`A ⊑ B` and `B ⊑ A` give `A = B`],
)

These three must be *proven* for "safe widening" to mean anything. If transitivity broke, the language would allow `u8 → u16` and `u16 → u32` but
block `u8 → u32`. Users could never find out why. The three are proven for the real types in #chref("proofs-numbers").

== Lattices and join --- what to pick when combining

#idx("join")
When two values go into one operation, the processor must compute in *a type holding both*. There are several candidates, but the right one is the
smallest. That "least common upper bound" is called the *join* and written `⊔`. A partial order where any two elements always have a join is a
*lattice*.

#demo("examples/ch39/wider.low")

`u8 ⊔ u16 = u16`. So even though the output type is `u32`, the addition happens in `u16`, and `255 + 65535` overflows `u16` and stops. The join is
decided by the *operands*, not by the place that receives the result.

With mixed signs it defies intuition. `u8 ⊔ i8` is not `i8` but `i16` --- holding both 255 from `u8` and −128 from `i8` needs a wider type. People
get this wrong. Lowent does not pick this widening for you; it makes you write it.

#demo("examples/ch39/sign_join.low")

The "strictly wider signed type" in the diagnostic is exactly `u8 ⊔ i8 = i16`. Widening both to `i16` explicitly is accepted.

== Monotone functions and fixed points --- why analysis stops

#idx("fixed point")
A *monotone function* is one whose output does not get smaller as its input gets larger. A *fixed point* is a point where `f(x) = x` --- applying
once more changes nothing.

These two ideas do the work when the processor analyses a loop. The analyser does not know a variable's range on entering the loop. So it starts
narrow and widens repeatedly.

```text
after round 1: i ∈ [0, 0]
after round 2: i ∈ [0, 1]
after round 3: i ∈ [0, 2]      … this way it never ends
```

So it uses *widening*. After seeing it widen a few times, it decides "this will keep growing" and pushes to the top at once. Combining `i ∈ [0, ∞)`
with the loop condition `i < 10` gives `i ∈ [0, 9]`, and one more round still gives `[0, 9]`. *It has reached a fixed point* and the analysis ends.

#demo("examples/ch39/count_up.low")

There is one proof line. `add i 1` adds within `i ∈ [0, 9]`, so it is proven not to exceed `u8` (`R-ARITH-RANGE 0 9`). `add s i` is not proven,
because widening threw away the upper end of `s`. In reality `s` never exceeds 45, but the analysis does not know that. So the check stays --- slow,
but never wrong.

- That a fixed point always exists is the guarantee that the analysis *stops*.
- Widening goes *wide*. Going larger than reality only fails to remove checks; it never gives wrong answers. This direction is the heart of abstract
  interpretation.

== Induction and invariants --- dominoes

#idx("invariant")
Mathematical induction goes like this. `P(0)` is true, and if `P(n)` is true then `P(n+1)` is true. Then `P(n)` is true for every `n`. In programs,
`n` becomes "steps executed", and `P` is called an *invariant* --- a property true throughout execution.

#mathbox[The shape of the ownership and borrowing theorem][
  At the start the borrow state is clean (`P(0)`). Executing one event that passed the static check keeps a clean state clean (`P(n) → P(n+1)`).
  Therefore however many steps run, there is no borrowing violation (`∀n, P(n)`). The beauty of this argument is that *you need not know how many
  steps*. So the conclusion holds without knowing how many times a loop runs (#chrefs("proofs-ownership", "proofs-loops")).
]

== Abstract interpretation --- ranges instead of values

#idx("abstract interpretation")
To learn something about values without running the program, you must handle *sets of values* instead of single values. Handling sets as they are
is too large, so a simple approximation is used. The typical one is the interval `[lo, hi]`.

```text
if x ∈ [0, 5] and y ∈ [10, 20]
x + y ∈ [10, 25]      add the ends
x × y ∈ [0, 100]      with mixed signs, take min and max of four products
```

There is one rule. *The result of an abstract computation must include every value actually possible.* If `x` is always 3 but the analysis answers
`[0, 5]`, it loses something but is safe (it cannot remove a check). If `x` can be 7 but the analysis answers `[0, 5]`, the moment a check is removed
on that answer an out-of-range access happens. So every rule of the analysis leans "wide".

#misconception[The more precise the analysis, the better][
  *Direction* comes before precision. An analysis wrong on the wide side is only slow, but one wrong on the narrow side reads wrong memory. Each time a
  rule raising precision is added, the question is not "is it more precise" but "can it now be narrower than reality". That is why this language keeps a
  light analysis that errs wide instead of a heavy one, and keeps checks where it falls short.
]

== Table of symbols

#dtable(
  columns: 3,
  id: "math-symbols",
  caption: [Symbols in this part],
  [*Symbol*], [*Read as*], [*Meaning*],
  [`A ⊑ B`], [A fits safely into B], [The safe widening relation],
  [`A ⊔ B`], [The join of A and B], [The smallest thing holding both],
  [`[lo, hi]`], [Interval], [Values from lo to hi inclusive],
  [`∀x, P(x)`], [For all x, P], [No exceptions],
  [`∃x, P(x)`], [There is an x with P], [At least one],
  [`P → Q`], [If P then Q], [Implication],
  [`⊤`], [Unknown], [Any value possible],
  [`A ∗ B`], [A and B, on separate pieces], [The separating conjunction (#chref("proofs-locks"))],
)

== How the tools interlock

```text
partial order ⊑           defines "fits safely"                 (numbers)
   │
   ├─ join ⊔              picks the type when combining         (numbers · effects)
   │
monotone functions        loop analysis climbs over them        (bounds)
on a lattice
   │
fixed point               where analysis stops                  (bounds · loops)
   │
soundness of abstract     erring "wide" keeps conclusions safe  (bounds)
interpretation
   │
invariants and induction  show it holds throughout execution    (ownership · loops · races)
```

It reads bottom to top too. Induction keeps invariants, those invariants come from the soundness of abstract interpretation, abstract interpretation
runs on a lattice, and a lattice stands on a partial order. Without the tools design becomes guesswork --- a join table written by hand mixes in lines
like `u8 ⊔ i8 = i8` and turns 255 into −1, loop analysis without widening never ends, and a borrow check without invariants becomes a pile of "allowed
here, not there" rules.

#qa[
  What was proven in this chapter?
][
  Nothing. This chapter introduced vocabulary. That reflexivity, transitivity, antisymmetry, soundness of the join and existence of fixed points hold
  *for Lowent's actual definitions* is the content from #chref("proofs-numbers") on. And the soundness of abstract interpretation must be checked rule by
  rule --- "why is this one wide". If even one rule is narrow, it leaks right there. That is why #chref("proofs-bounds") examines the rules one by one.
]

#recap[
  A partial order allows incomparable pairs and keeps reflexivity, transitivity and antisymmetry. The join is the smallest thing holding both and is
  decided by the operands (`u8 ⊔ u16 = u16`, `u8 ⊔ i8 = i16`). Loop analysis reaches a fixed point by widening and stops, and widening goes wide, keeping
  checks but never being wrong. Induction and invariants show "however many steps", and abstract interpretation computes with ranges instead of values,
  always erring on the side of including reality.
]
