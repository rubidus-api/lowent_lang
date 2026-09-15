#import "../../book/lib.typ": *

= Proofs about numbers and bounds

#chapter-toc()

#prereq(
  ([#chref("numbers"), Numbers], [widening is automatic, narrowing is written]),
  ([#chref("slices"), Sequences], [contracts remove bounds checks]),
  ([#chref("proofs-why"), Why prove], [the three layers of proof, exhaustive checking and cross-checking]),
)

#deepqa[
  In #chref("numbers"), `u8` and `i16` could be mixed but `u32` and `i32` could not. What separated them?
][
  Whether a value-preserving widening exists. Every value of `u8` (0 … 255) fits in `i16`, but large values of `u32` do not fit in `i32` of the same width. This chapter defines
  that "value-preserving widening" mathematically and shows why the argument by which contracts remove checks is correct.
]

#why[
  This language's performance claim --- "write honestly and it gets faster" --- rests on removing checks. Removing checks is dangerous. Removed by feel, it is not faster but
  wrong. So the conditions for safe removal must be proven, and whether the compiler applied them exactly must be confirmed separately. These are the proofs of Part X that touch
  code most directly, so they come first.
]

#organizer[
  You will learn the four-line definition of the widening relation `⊑`, the theorem that it is a partial order, and the properties of the join that picks types at merge points.
  You will pick up why the interval analysis is designed to be wrong "wide", and its soundness theorems. You will also see the one-slot relational domain holding relations
  between variables, the nonlinear bound proof for row-major indexing, the theorem that `mod` is always a safe index, and the certificates left for each removed check along with
  a checker extracted from Coq.
]

#chapter-questions()

== Widening is a partial order

`t ⊑ u` reads "any value of type `t` fits into type `u` without changing". The definition is four lines.

#dtable(
  columns: 3,
  id: "pnum-sub",
  caption: [The widening relation `⊑`],
  [*Left → right*], [*Condition*], [*Why*],
  [`uN` → `uM`], [N ≤ M], [Between unsigned types only width must be larger],
  [`iN` → `iM`], [N ≤ M], [The same between signed types],
  [`uN` → `iM`], [*N < M*], [`u8` (255) does not fit in `i8` (127). Width must be *strictly* larger],
  [`iN` → `uM`], [none], [There is nowhere to put negatives],
)

The `<` in the third line is the point. Written as `≤`, `u8 ⊑ i8` would be allowed and 255 would become −1. This one character decides value safety, and people get such things
wrong.

#demo("examples/ch39/order_strict.low")

#demo("examples/ch39/order_ok.low")

What `NumericLattice.v` proves about this relation:

- *It preserves values* (`sub_preserves`). If `t ⊑ u` and `v` is in `t`'s range, it is in `u`'s range. This is why widening has no run-time check.
- *It is a partial order* (`sub_refl`, `sub_trans`, `sub_antisym`). If transitivity broke, the language would allow `u8 → u16` and `u16 → u32` but not `u8 → u32`. Managing the
  table by hand really does make such holes.
- *The join holds both and is one of the two* (`join_sound`, `join_is_an_operand`). When two branches give different types, the type the compiler picks holds both values and does
  not invent a third type absent from the source.
- *Narrowing has exactly one condition* (`narrow_ok_iff`). `narrow_try u8 300` never quietly succeeds giving 44.

#mathbox[The skeleton of the proof that widening preserves values][
  Split into four cases. If `uN ⊑ uM` (N ≤ M), both lower ends are 0 and the upper ends satisfy 2#super[N] − 1 ≤ 2#super[M] − 1 --- using only the fact that powers of 2 are
  monotonic. If `uN ⊑ iM` (N < M), then N ≤ M − 1, so 2#super[N] − 1 ≤ 2#super[M−1] − 1. Signed pairs are the same, and `iN ⊑ uM` is false by definition, so there is nothing to
  do. The key lemma is just "if a ≤ b then 2#super[a] ≤ 2#super[b]". The whole proof stands on high-school exponent laws. What is hard is not the argument but *missing not a single
  case*, which is why it is left to a machine.
]

The type rules are proven one layer deeper (`LowentType.v`). For numeric expressions with width and sign, a typed expression is a value, stops, or takes a step (`progress`); a step
does not change its type (`preservation`); and when execution reaches a value, that value is always within its type's width (`values_fit`). The third is special in this language,
because if a value over 8 bits lands in a `u8` place, the emitted C silently does something else.

== The interval analysis is wrong wide

#idx("interval analysis")
The compiler computes with intervals `[lo, hi]` instead of single values to decide overflow and bounds. If `a ∈ [0, 100]` and `b ∈ [0, 100]`, then `a + b ∈ [0, 200]`. Subtraction
flips (`lo − hi`, `hi − lo`), and multiplication with mixed signs takes the minimum and maximum of four products.

#demo("examples/ch39/sum_contract.low")

The contract bounds both inputs to 100 or less, so the sum does not exceed 200 and does not overflow `u16`. Every check disappears. With the same shape but unbounded inputs, the
checks remain.

#demo("examples/ch39/product_bare.low")

There are two grounding theorems. `radd_no_check` --- if the result interval is within the declared type's range, the operation does not overflow. `idx_no_check` --- if an index is
known to be at least 0 and less than the length, `index` is safe.

The theorems all point one way. *"Places said to be safe really are safe."* If the analysis is wide, checks remain and it is merely slow; if narrow, checks disappear and wrong
memory is read. So every time a rule is added, the question is the same --- "can this rule be narrower than reality?"

== Relations --- what intervals cannot do

Intervals look at variables separately. Knowing `i ∈ [0, 100]` and `n ∈ [0, 100]` does not tell whether `i < n`. Yet that is exactly the fact array safety needs. So a one-slot
relation sits beside the intervals --- "local `i` is less than `len s`", "local `i` is less than local `n`". These facts come from branch conditions. Inside the body of
`while lt i n`, `i < n` is true.

One slot is used instead of heavy relational domains (octagons, polyhedra) because of the shape of real code. Code walking an array is almost always shaped `while lt i (len s)`.
And the places where facts *die* are the core of safety. When a related variable changes, the fact is killed in both directions, and at merges it survives only if both paths agree.
Keeping a fact alive too long is exactly wrong check removal, so killing is always the more aggressive side.

#mathbox[The bound proof for row-major indexing][
  Matrix multiplication uses computed indices like `index a (add (mul i n) k)`. Neither intervals nor one-slot relations handle products, but this one shape has an exact proof. There
  are three premises --- the contract `len a ≥ p·q`, the outer loop condition `i < p`, and the inner loop condition `k < q`. Then i ≤ p − 1, so i·q ≤ p·q − q, and k ≤ q − 1, so
  i·q + k ≤ p·q − 1 < p·q ≤ len a. Only two inequalities were added. But drop any premise and it collapses. It also collapses if the address is computed with `wrap_mul` --- a wrapped
  product can get smaller, breaking the step i·q ≤ (p − 1)·q. The same formula, but if the product is not a stopping one, it is not a fact. What if the contract's product p·q itself
  overflows? The contract's product is a stopping product and contract checks are never removed, so if the body is reached, it did not overflow.
]

In the development repository's measurements this rule cut the bounds checks of a matrix multiplication benchmark from 6 to 1 (the remaining one is where `n ≥ 1` is missing from the
contract), and an LRU benchmark whose contract states capacity from 10 to 4. But the number of checks is not a proxy for cost. A check that never fires and is always branch-predicted
correctly is practically free, and one benchmark cut checks from 7 to 0 with unchanged time. Checks are expensive where they block auto-vectorisation.

== `mod` is always a safe index

#demo("examples/ch39/modslot.low")

`mod_is_a_safe_index` --- `mod h (len s)` is always at least 0 and less than `len s`. If `len s` is 0 it divides by zero and stops first, so on any path that yields a value,
`len s > 0` is guaranteed. Hash tables use this shape all the time, so it is valuable. For remainders and division it is also proven that unsigned division always succeeds when not
dividing by zero, that the only overflowing signed division is `MIN / −1`, and that the sign of `mod` follows the divisor.

== Trust proofs, but back trust with checks

Theorems say the rules are right, but whether the compiler applied them exactly must be confirmed separately. There are two devices.

*Analysis self-accusation.* The VM does not remove checks. It runs the places the compiler marked "may be removed", and if a value is actually out of range, it accuses the compiler
right there with `E-VM-ANALYSIS: … the interval/relational analysis is UNSOUND (this is a compiler bug)`. This device has actually worked. While adding the row-major rule,
`requires ge (len a) (mul n n)` was mistaken for a checked contract, but the processor could not read a contract of that shape, so there was really no check, and exactly this
diagnostic appeared.

#idx("certificate recheck")
*Certificate recheck.* For every removed check the compiler leaves arithmetic grounds (a Farkas certificate), and checkers written separately from the compiler recompute that
arithmetic. One checker is *extracted* into OCaml from the Coq code that proved the rule's soundness (`LowentCert.v`, `LowentCertExtract.v`). Nobody confirms "did I transcribe this
arithmetic correctly" for a hand-ported checker, but the extracted one is the very function the theorem talks about. Still, this only makes the rule's *arithmetic* trustworthy;
"does that fact really hold at that place" is outside the checker's jurisdiction.

#qa[
  If `--ir` says "4 / 4 removed", have four checks disappeared from the C code too?
][
  Bounds checks and narrowing disappear. For arithmetic checks, the proof is recorded in the intermediate representation, but the C back end still emits the stopping calls. Emitting
  them costs around 1%, and it is a choice not to keep integer semantics in two places. `--ir` writes this difference itself --- the number is *what is proven*, not *what disappeared
  from the code*, so do not predict performance from it.
]

== What is not proven

- *Floating point is not in this lattice.* Only integers were handled. Rounding, NaN and −0 were not covered, and the interval analysis does not support floating point.
- *Widening discards information.* It deliberately goes wide to reach a loop's fixed point quickly, so checks may remain even at safe places. A loss, not a danger.
- *The only nonlinear shape is row-major.* Other nonlinear shapes such as `while lt (mul i i) n` have no rule, so checks remain.
- *The theorems are about type rules and arithmetic rules.* Whether the compiler implemented those rules exactly is confirmed by tests, back-end cross-checks, self-accusation and
  certificate rechecks.

#recap[
  Widening `⊑` is a partial order defined in four lines, and the condition for `uN ⊑ iM` is the strict `N < M`. Widening preserves values and the join is one of its two operands. The
  interval analysis is designed to be wrong wide, and the one-slot relational domain and the row-major nonlinear proof remove bounds checks in real code. `mod` is always a safe index.
  The VM's self-accusation and a certificate checker extracted from Coq back the gap between proof and implementation. Floating point is outside the proofs.
]
