# <a id="mod-sortgen"></a>`sortgen` — generic sorting (the type brings the comparison)

Source

`lib/sortgen.low`

Layer

L0 — pure computation

Capabilities

none

Sorts even when elements are not `u64`. What is smaller **the type says for itself**. Used to order records by some field — for plain `u64` elements [`sortlib`](sec68.md#mod-sortlib) is simpler and faster. Three things go together — ① declare that the type satisfies `ordered`, ② write that type’s `less`, ③ call with the type as an argument.

```lowent
use sortgen .

def struct keyed do
  satisfies sortgen.ordered .
  k u64 .
end .

fn keyed.less input a keyed . input b keyed . output bool . do
  return lt field a. k . field b. k . . .
end .

proc sorted3 input s mut slice keyed . output u64 . effects none . do
  guard ge len s. . 3 . else return 90 . .
  sortgen.sort_by keyed. s. .
  let f0 keyed idx s. 0 . .
  let f1 keyed idx s. 1 . .
  let f2 keyed idx s. 2 . .
  return add mul 100 field f0. k . . add mul 10 field f1. k . . field f2. k . . . .
end .
```

**Why the comparison is not passed as a function value.** The common answer (C’s `qsort`, C++ comparators) passes a comparison function as a value. This language does not. It has no first-class functions, and even with them there would be an **indirect call** — free-looking at the call site, while the cost of going through a function pointer hides. This language’s answer is **comptime type parameters + trait bounds** (chapter 22, chapter 23). Each call creates an op specialised for that type, and the comparison is embedded as a direct call. Zero indirect calls, zero space for a comparator.

| **op** | **Shape** | **Failure** |
|---|---|---|
| `ordered` | trait — `less (self, self) → bool` | unsatisfied = `E-BOUND-UNSAT` (compile time) |
| `sort_by` | `(comptime t, mut slice t) → void`, `requires ordered t` — insertion sort (stable) | none |
| `sort_fast` | same shape — quicksort, for large arrays | none |
| `lower_by` | `(comptime t, slice t, key t) → u64` | none — the insertion point if absent (may be `len s. .`) |
| `find_by` | `(comptime t, slice t, key t) → option u64` | `none` if absent |

*Table 50.1 — Ops of `sortgen`*

If `less a b` is true, a comes before b. The relation must be a **strict weak order** — in particular `less a a` must be false. Without `requires ordered t` the body would call `less` without anyone asking whether it exists. With the bound, calling with a type lacking it is rejected at compile time.

**The story of `sort_fast`.** At first a generic quicksort was not built on the belief that partitioning needs a **copy** of the pivot. Wrong — hold the pivot by **position** (index) and re-read it at each comparison. During the partition loop the pivot stays at `hi − 1`, and all moving is done by `swap`.

> **Counter-example. Forgetting `satisfies` or passing `u64` directly**
>
> > Calling `sort_by` with a struct lacking `satisfies sortgen.ordered .` or with `u64` is `E-BOUND-UNSAT`. `u64` has nowhere to attach a method, so it cannot satisfy a trait — wrap it in a one-field struct (layout stays 8 bytes), or use `sortlib.sort`.

> **Counter-example. `less` returning true for equal values**
>
> > `return le field a. key . field b. key . . .` compiles, but with two or more equal values the loop may never end. The comparison must be `lt`.

**Cautions.** Descending order and multiple keys are written in `less` — adding a mode argument is entropy itself. One copy of code is produced per type (the price of monomorphisation, in exchange for no indirect calls). **Reading gives a view** — `let a t idx s j` is a window onto that position, not a copy, so elements are swapped with `swap` (two `set`s would overwrite themselves). Take care if you write a sort yourself — after one swap the same names `a` and `b` see different values. Decide once per step. Comparing twice makes the second comparison see the already swapped values, and an element moves down one place and stops — a partial sort that, with three elements and a single key, happens to give the right answer and hides the defect (it really did hide, until a multi-key example revealed it). It is separate from `sortlib` because bringing in generic templates would have excluded every file using `sortlib` from some comparison checks.

---

[← Prev](sec68.md) · [Contents](README.md) · [Next →](sec70.md)
