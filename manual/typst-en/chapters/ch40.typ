#import "../../typst-ko/lib.typ": *

= Proofs about numbers --- widening, narrowing, division

#chapter-toc()

#prereq(
  ([#chref("numbers"), Numbers], [widening is automatic, narrowing is written]),
  ([#chref("named-types"), Named types], [`range lo hi` is a contract that became the shape of a parameter]),
  ([#chref("proofs-math"), The mathematical toolkit], [the three conditions of a partial order, and the join]),
)

#deepqa[
  In #chref("numbers"), `u8` and `i16` could be mixed but `u32` and `i32` could not. What separated them?
][
  Whether a value-preserving widening exists. Every value of `u8` (0 … 255) fits in `i16`, but large values of `u32` do not fit in `i32` of the same width. This chapter defines
  that "value-preserving widening" as a partial order from #chref("proofs-math") and reads the machine proof that it really is safe.
]

#why[
  In C, `unsigned char small = 300;` silently becomes 44, and putting `-1` into an `unsigned int` gives 4294967295. Lowent allows only value-preserving widening automatically,
  and if losing is acceptable, makes you choose how to lose (`narrow_wrap`, `narrow_sat`, `narrow_try`). Saying that rule is right means a table of a few lines has no mistakes,
  and people get such tables wrong. So the definition was set up mathematically and the job of missing no case was given to a machine. These are the proofs that touch code most
  directly, so they come right after the tools.
]

#organizer[
  You will learn the four-line definition of the widening relation `⊑` and the theorem that it is a value-preserving partial order, along with properties of the join and
  narrowing. You will pick up exactly when division fails, the sign promise of `mod`, and the theorems about `range` that ground the removal of checks. You will also see how the
  three theorems of the type rules (`progress`, `preservation`, `values_fit`) put stopping into the model honestly.
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

```text
 u8 ──▶ u16 ──▶ u32 ──▶ u64
   ╲       ╲       ╲
    ▼       ▼       ▼
 i8 ──▶ i16 ──▶ i32 ──▶ i64

 → is ⊑ (fits without changing the value); following arrows is ⊑ too (transitivity).
 There is no same-width arrow down such as u8 → i8 or u16 → i16 (N < M),
 and no arrow up from the i row to the u row (nowhere to hold a negative).
```

#demo("examples/ch40/order_strict.low")

#demo("examples/ch40/order_ok.low")

What `NumericLattice.v` proves about this relation:

- *It preserves values* (`sub_preserves`). If `t ⊑ u` and `v` is in `t`'s range, it is in `u`'s range. This is why widening has no run-time check.
- *It is a partial order* (`sub_refl`, `sub_trans`, `sub_antisym`). If transitivity broke, the language would allow `u8 → u16` and `u16 → u32` but not `u8 → u32`. Managing the
  table by hand really does make such holes.
- *The join holds both and is one of the two* (`join_sound`, `join_is_an_operand`). When two values go into one operation, the type picked holds both values and does
  not invent a third type absent from the source (#chref("proofs-math")'s `u8 ⊔ u16 = u16`).
- *Narrowing has exactly one condition* (`narrow_ok_iff`). `narrow_try u8 300` never quietly succeeds giving 44.

#mathbox[The skeleton of the proof that widening preserves values][
  Split into four cases. If `uN ⊑ uM` (N ≤ M), both lower ends are 0 and the upper ends satisfy 2#super[N] − 1 ≤ 2#super[M] − 1 --- using only the fact that powers of 2 are
  monotonic. If `uN ⊑ iM` (N < M), then N ≤ M − 1, so 2#super[N] − 1 ≤ 2#super[M−1] − 1. Signed pairs are the same, and `iN ⊑ uM` is false by definition, so there is nothing to
  do. The key lemma is just "if a ≤ b then 2#super[a] ≤ 2#super[b]". The whole proof stands on high-school exponent laws. What is hard is not the argument but *missing not a single
  case*, which is why it is left to a machine.
]

== Division and remainder

Division is a place this language treats with unusual care.

#demo("examples/ch40/division.low")

Signed division `-7 / 2` truncates towards zero to −3. `-128 / -1` stops because `i8` has no 128. In C this one case is undefined behaviour. The sign of `mod`
follows *the divisor* --- `mod -7 3` is 2 and `mod 7 -3` is −2.

#dtable(
  columns: 2,
  id: "pnum-div",
  caption: [What is proven about division and remainder],
  [*Theorem*], [*Meaning*],
  [`div_unsigned_total`], [Unsigned division always succeeds unless dividing by zero],
  [`div_signed_failure_is_only_min_neg1`], [The only failing signed division is `MIN / −1`],
  [`mod_sign_follows_divisor`], [The sign of `mod` follows the divisor],
  [`mod_is_a_safe_index`], [`mod h (len s)` is always at least 0 and less than `len s`],
)

```text
 unsigned div a b     b = 0                 → stops
                      otherwise             → always a value
 signed div a b       b = 0                 → stops
                      a = MIN and b = −1    → stops (undefined behavior in C)
                      otherwise             → always a value (truncated toward 0)
 mod h n              when n > 0            → 0 ≤ result < n  (a safe index)
```

The last theorem has great practical value.

#demo("examples/ch40/modslot.low")

Hash tables always pick slots with `mod`, and because the result is proven in range, the index check disappears. What if `len s` is 0? It divides by zero and stops
first. So on any path that yields a value, `len s > 0` is guaranteed. The argument has no gap.

== Theorems that `range` provides

`range lo hi` written in a parameter's type position (#chref("named-types")) makes the widening story one layer more precise.

#dtable(
  columns: 2,
  id: "pnum-range",
  caption: [What is proven about ranges],
  [*Theorem*], [*Meaning*],
  [`rsub_preserves`], [Widening between ranges also preserves values],
  [`radd_sound`], [The sum of two ranges lies within the computed range],
  [`radd_no_check`], [If the result range fits in the type, no overflow check is needed],
  [`idx_no_check`], [If the index range lies within the length, no bounds check is needed],
  [`rdisj_no_value`], [Two disjoint ranges share no value (so one branch is dead)],
  [`derive_minimal`], [The derived range is the smallest (no wider than needed)],
)

`radd_no_check` and `idx_no_check` are what justify removing checks. The analysis that applies these two to real code is in #chref("proofs-bounds").

== Putting stopping into the model honestly

The type rules are proven one layer deeper (`LowentType.v`). Over numeric expressions with width and sign, it adds binding, branches, comparison, narrowing, checked
arithmetic and wrapping arithmetic, and proves three theorems.

- `progress` --- a typed closed expression *is a value, stops, or takes a step*. It never gets stuck.
- `preservation` --- taking a step does not change its type.
- `values_fit` --- when execution reaches a value, that value is *always within its type's width*.

The third is special in this language. If a value over 8 bits lands in a `u8` place, the emitted C silently does something else. So ranges were put inside the type
rules (attaching a type to a value requires range evidence) and proven preserved throughout execution.

And that the conclusion has *three* parts is the point. Checked arithmetic stops instead of producing a value on overflow. Put stopping into values and the theorem
lies; leave stopping out and the theorem is false. So it is "value · stop · step". Preventing stops at translation time is the normal path's job, done by contracts
and certificates (#chref("proofs-bounds")).

```text
 a closed, typed expression e
    ├─▶ a value v     values_fit: v always fits the width of its type
    ├─▶ it stops      checked arithmetic overflowed · division by zero
    └─▶ one step → e' preservation: e' has the same type (and again one of the three)
```

#qa[
  Is it worth proving separately that widening cannot fail?
][
  Yes (`widen_never_fails`). "Cannot fail" must be proven for widening sites to carry no run-time check. That is *why widening is free*. Conversely, `narrow_ok_iff`
  pins narrowing's success condition to "exactly within range", so `narrow_try u8 300` does not quietly give 44 but `none`. Failure is a value.
]

#misconception[Since `values_fit` is proved, an overflowing value can never reach a `u8` position in this edition's compiler][
  The theorem is about the *type rules*. Whether the compiler follows those rules in every place is a separate matter. One example found while
  writing this manual: when a `pipe`'s `map` produces `u64` and `collect into` stores it in a `u8` buffer, this edition's tool silently wrapped
  the value for a long time (it is now rejected with `E-TYPE-COLLECT`, #chref("pipe")). The gap between a proof that the rules are right and the fact that the implementation applies them everywhere
  shows up in places like this, and tests and the comparison of the two back ends fill it. Proofs decide what must be stopped; whether it was
  stopped is measured.
]

== What is not proven

- *Floating point is not in this lattice.* Only integers were handled. `f32 → f64` preserves values, but the reverse and mixed operations are separate rules not proven
  here. Rounding, NaN and −0 are not covered either.
- *Widths are fixed at 8, 16, 32 and 64.* Arbitrary-width integers (such as `bits 7`) exist in the implementation (#chref("named-types")) but lie outside this model, and `u128` exists in neither.
- *The theorems are about type rules.* Whether the compiler implements those rules exactly is confirmed by tests and back-end cross-checks (#chref("build-test")).

#recap[
  Widening `⊑` is a partial order defined in four lines, and the condition for `uN ⊑ iM` is the strict `N < M`. Widening preserves values and cannot fail, the join
  is one of its two operands, and narrowing succeeds exactly within range. The only failing signed division is `MIN / −1`, `mod` follows the divisor's sign and is
  always a safe index. The `range` theorems ground check removal, and the type rules put stopping into the model honestly as "value · stop · step". Floating point is
  outside the proofs.
]
