#import "../../typst-ko/lib.typ": *

= Proofs about bounds --- intervals, relations, row-major addresses

#chapter-toc()

#prereq(
  ([#chref("slices"), Sequences], [writing the length condition as a contract removes index checks]),
  ([#chref("contracts"), Contracts], [enforced contracts become facts that remove checks]),
  ([#chref("proofs-math"), The mathematical toolkit], [abstract interpretation must err wide, and loop analysis stops at a fixed point]),
  ([#chref("proofs-numbers"), Proofs about numbers], [`radd_no_check` · `idx_no_check`]),
)

#deepqa[
  What were the two theorems that #chref("proofs-numbers") called the justification for removing checks, and what did each presuppose?
][
  `radd_no_check` says no overflow check is needed if the result interval fits the declared type, and `idx_no_check` says no bounds check is needed if the
  index is known to be at least 0 and less than the length. Both presuppose that *the fact is already known*. This chapter covers how the compiler obtains
  those facts --- and what stops it from wrongly believing it has.
]

#why[
  This language stops when integer arithmetic overflows and when an index goes out of range. So it would seem every operation carries a check and runs slowly.
  In practice most checks vanish at translation time, because not overflowing and being in range are proven. C does not check, so it is fast but reads other
  people's memory when out of range --- half of all security incidents come from there. Lowent takes a third path: check, but remove the check on the spot when
  it is proven unnecessary. The claim "write honestly and it gets faster" rests entirely on this chapter's analysis, so we look at where it can go wrong and
  what backs it.
]

#organizer[
  You will confirm how interval analysis computes value ranges and that its soundness points one way. You will read, through `--emit-proof` output, how
  one-slot relations carry facts between variables that intervals cannot, and the four stages at which bounds checks vanish (direct · length stored in a local
  · capacity from a contract · row-major address). You will also see when the row-major proof collapses, the device by which the VM accuses the compiler when
  the analysis is wrong, the certificates left for each removed check and their checker, and the checks that remain even with contracts.
]

#chapter-questions()

== Computing with intervals

#idx("interval analysis")
The compiler computes with intervals `[lo, hi]` instead of single values to decide overflow and bounds.

```text
a ∈ [0, 100],  b ∈ [0, 100]
a + b ∈ [0, 200]        lo+lo, hi+hi
a − b ∈ [−100, 100]     lo−hi, hi−lo   subtraction flips --- a common place for mistakes
a × b ∈ [0, 10000]      with mixed signs, min and max of four products
```

#demo("examples/ch41/sum_contract.low")

The contract bounds both inputs to 100 or less, so the sum does not exceed 200 and does not overflow `u16`. Every check disappears. With the same shape but
unbounded inputs, the checks remain.

#demo("examples/ch41/product_bare.low")

`1000 × 1000` does not overflow, but the analysis does not know how large `a` and `b` can get. So it keeps the check and confirms no overflow only when actual
values arrive.

The theorems all point one way. *"Places said to be safe really are safe."* The converse --- safe but not said to be --- is not guaranteed. That is only a
performance loss.

```text
analysis goes wide    →  checks remain     →  slow (safe)
analysis goes narrow  →  checks disappear  →  wrong memory access (dangerous)
```

So every time a rule is added, the question is the same --- "can this rule be narrower than reality?"

== Relations --- what intervals cannot do

Intervals look at variables separately. Knowing `i ∈ [0, 100]` and `n ∈ [0, 100]` does not tell whether `i < n`. Yet that is exactly the fact array safety needs.
So *one-slot relations* sit beside the intervals.

```text
lenlt[i] = s     local i is less than len(local s)
lerel[i] = n     local i is less than local n
```

These facts come from branch conditions. Inside the body of `while lt i n`, `i < n` is true, and on the branch where the condition is false, `i ≥ n` is true. One
slot is used instead of heavy relational domains (octagons, polyhedra) because of the shape of real code. Code walking an array is almost always shaped
`while lt i (len s)`, and capturing the shape of real code exactly is worth more than sophisticated theory.

The rules for facts have three parts.

+ *Obtain.* Plant the fact on the branch where the condition is true.
+ *Carry.* Follow assignments --- in `var x be j`, if `j < cap` then `x < cap`. At merges, keep a fact only if both paths agree. What holds on one side only is
  not a fact.
+ *Kill.* When a related variable changes, kill the fact in both directions. When a slice is rebound, every fact about its length dies.

The third is the core of safety. Keeping a fact alive too long is exactly wrong check removal, so killing is always the more aggressive side.

== Four stages

Here are the shapes in which bounds checks vanish, in order of increasing strength. `--emit-proof` lists each check removed by proof on one line --- op name,
instruction position, operation, rule used, and the ranges the rule used.

#demo("examples/ch41/bound_stages.low")

- *1 · Direct.* In `direct` the loop condition looks at `len s` directly. `index` was removed by `R-IDX-LENLT`.
- *2 · Length stored in a local.* `stored` puts the length in a local with `let n be len s`. It is a very common idiom. At one time the fact "this value is
  `len s`" was a property of a stack value that vanished the moment it was stored in a local, and this loop's check stayed. Since the fact is now carried by
  locals too, it is removed just like stage 1. This was a performance fix, not a safety fix --- the check was there, so it was safe all along.
- *3 · Capacity from a contract.* In `capacity` the array size and loop bound are different variables. `j < cap` (loop condition) and `len s ≥ cap`
  (contract), so `j < len s`.
- *4 · Row-major address.* `grid` uses a *computed* index, `index a (add (mul i n) k)`. Neither intervals nor one-slot relations handle products, yet it was
  removed by `R-IDX-ROWMAJOR`. The product and sum of the address were proven along with it, by `R-MUL-CAP` and `R-ROW-CAP`.

The additions incrementing `i` and `j` vanished too, via `R-ADD-LENLT` and `R-ADD-LEREL`, thanks to the same relations. If `i < n` then `i + 1 ≤ n`, so it does
not overflow.

== The row-major proof

#mathbox[Proof that a row-major address is in range][
  There are three premises --- the contract `len a ≥ p·q`, the outer loop condition `i < p`, and the inner loop condition `k < q`. The address `i·q + k` is
  computed with stopping multiplication and stopping addition. Then i ≤ p − 1, so i·q ≤ (p − 1)·q = p·q − q, and k ≤ q − 1, so i·q + k ≤ p·q − q + q − 1 = p·q − 1.
  Therefore address ≤ p·q − 1 < p·q ≤ len a. Only two inequalities were added.
]

High-school mathematics, but drop any premise and it collapses.

#dtable(
  columns: 2,
  id: "bounds-rowmajor",
  caption: [When the row-major rule holds],
  [*What was removed*], [*Index check*],
  [Nothing], [Disappears],
  [The contract `len a ≥ n·n`], [Remains],
  [`i` bound by a different variable (`while lt i m`)], [Remains],
  [Address computed with `wrap_mul`], [Remains],
)

The last line is the subtlest.

#demo("examples/ch41/grid_wrap.low")

Same formula, but there is no `index` line. `wrap_mul` silently wraps on overflow. A wrapped value can get smaller, so the step i·q ≤ (p − 1)·q breaks. In this
example `n ≤ 1000` so it never actually wraps, but the rule holds only "for stopping multiplication". *If the operation does not stop, it is not a fact.*

Then what if the contract's product `p·q` itself overflows? The contract's product is a stopping product, and contract checks are never removed, so if the body
was reached it did not overflow. Where bounds checks were removed, the contract check is the only barrier. That is why contract checks are removed by neither
modes nor optimisation.

In the development repository's measurements, this rule cut the bounds checks of a matrix multiplication benchmark from 6 to 1 (the remaining one is
`index c 0`, where `n ≥ 1` is missing from the contract), and those of an LRU benchmark whose contract states capacity from 10 to 4.

#misconception[Fewer checks mean proportionally faster][
  The number of checks is not a proxy for cost. A check that never fires and is always branch-predicted correctly is practically free, and one benchmark cut
  checks from 7 to 0 with unchanged time. Checks are expensive where they block auto-vectorisation, as in matrix multiplication. So this part does not count
  fewer checks as an achievement. And arithmetic checks counted by `--ir` are recorded as proven while the C back end still emits the stopping calls --- that
  number is *what is proven*, not *what disappeared*.
]

== Trust proofs, but back trust with checks

Theorems say the rules are right, but whether the compiler applied them exactly must be confirmed separately. There are two devices.

*Analysis self-accusation.* The VM does not remove checks. It runs the places the compiler marked "may be removed", and if a value is actually out of range, it
accuses the compiler right there with `E-VM-ANALYSIS: … the interval/relational analysis is UNSOUND (this is a compiler bug)`. The native build has no checks and
is fast, the VM has checks and reports when the mark is false, and the back-end cross-check runs both on the same inputs. This device has actually worked. While
adding the row-major rule, `requires ge (len a) (mul n n)` was believed to be a checked contract, but the processor could not read a contract of that shape, so
there was really no check, and exactly this diagnostic appeared. *Trust only what is checked.*

#idx("certificate recheck")
*Certificate recheck.* The numbers at the end of each `--emit-proof` line are the grounds. For every removed check the compiler leaves arithmetic grounds (a
Farkas certificate), and checkers written separately from the compiler recompute that arithmetic. The `certified` count on the first line is how many passed.
One checker is *extracted* into OCaml from the Coq code that proved the rule's soundness (`LowentCert.v`, `LowentCertExtract.v`). Nobody confirms "did I
transcribe this arithmetic correctly" for a hand-ported checker, but the extracted one is the very function the theorem talks about. Still, this only makes the
rule's *arithmetic* trustworthy; "does that fact really hold at that place" is outside the checker's jurisdiction.

#realcase[A tool that did not stop][
  While making interval analysis examples, an op with a loop running `n` times finished `--check` at once but `--ir` ran past 60 seconds without ending. `--ir`
  derives boundary values from the contract and actually runs the op, and with `n = 2^64 − 1` that loop would not finish within a human lifetime. That run was
  given a step budget, and cases hitting the budget are *counted as skipped*. Not passing them silently is the point. The moment a tool says it checked what it
  did not, it becomes a lie. The `--run` users invoke has no budget --- only tests the tool makes for itself may be cut off.
]

#qa[
  Why does a check remain for an index like `lru_idx`, where two values merge?
][
  At a merge a fact survives only if both paths agree. If `lru_idx` arrives as `0` on one side and `j` on the other, the fact `j < cap` belongs to one path and
  dies at the merge. Even if `0 < cap` is known separately, one-slot relations do not gather and carry "both are less than cap". This analysis would rather err by
  keeping a check than by keeping a fact alive too long.
]

== What is not proven

Some checks do not go away even with contracts. These are the places measured.

#dtable(
  columns: 2,
  id: "bounds-remain",
  caption: [Checks that remain, and why],
  [*Remaining place*], [*Why it cannot be removed*],
  [`index c 0` in matrix multiplication], [`n ≥ 1` is missing from the contract (adding it closes it)],
  [`index keys lru_idx` in LRU], [It merges `0` and `j`, so the relational fact dies at the merge],
  [`index s i` in the sieve (`while lt (mul i i) n`)], [`i·i < n ⟹ i < n` is another nonlinear shape, rare in real code, so no rule was made],
  [Partition indices in sorting], [There is nowhere to carry `hi ≤ len s` non-strictly (relations are strict only for now)],
)

- *Widening discards information.* It goes wide to reach a fixed point quickly, so checks remain even where safe (#chref("proofs-math")'s `count_up`). A loss,
  not a danger.
- *Facts about array contents have no place in this domain.* "Every element is less than 10" is closed by no `requires`. It is the tool's limit, not the
  program's defect, and since the check stays the answer is still right.
- *Floating-point intervals are not handled.*
- *The correspondence between analysis rules and theorems is empirical.* Self-accusation, certificate rechecks and back-end cross-checks back it
  (#chref("proofs-limits")).

#recap[
  Interval analysis computes with ranges instead of values; erring wide is only slow, erring narrow is dangerous. One-slot relations obtain facts like
  `i < len s` from branch conditions, carry them, and kill them aggressively when related variables change. Bounds checks vanish in four stages --- direct ·
  length stored in a local · capacity from a contract · row-major address --- and `--emit-proof` lists each rule on one line. The row-major proof holds only for
  stopping multiplication, and contract checks are never removed. The VM's self-accusation and a certificate checker extracted from Coq back trust, and checks
  remaining because of merges, nonlinearity or array contents are written down as remaining.
]
