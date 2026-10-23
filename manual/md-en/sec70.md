# <a id="mod-collection"></a>`collection` — map, filter and fold over a slice

Source

`lib/collection.low`

Layer

L0 — pure computation

Capabilities

none

Does the same work on each element of a slice and puts the results into another slice (`map`), keeps only the elements that pass a test (`filter`), or folds them into one value (`fold`). **The element type brings the work** — the element is wrapped in a struct of one field, and that struct satisfies the traits `mappable`, `keepable` and `foldable`. It is the same way [`sortgen`](sec69.md#mod-sortgen) finds its comparison in the element type. **Usually this module is not needed** — copying, keeping and folding are written with `pipe` (chapter 24), which passes a run-time value with `with`, changes the element type, and wraps nothing. This module is for generic code where the element type has to carry the work.

```lowent
use collection .

struct bump do
  satisfies collection.mappable .
  satisfies collection.foldable .
  v u64 .
end .

fn bump.apply input x bump . output bump . do
  return lit bump do v add field x. v . 1 . . end . .
end .

fn bump.combine input acc bump . input x bump . output bump . do
  return lit bump do v add field acc. v . field x. v . . . end . .
end .

proc bumped_sum input s slice bump . input out mut slice bump . output u64 . effects none . do
  let n u64 collection.map bump. s. out. . .
  let total bump collection.fold bump. lit bump do v 0 . end . out. . .
  return add n. field total. v . . .
end .
```

| **op** | **shape** | **failure** |
|---|---|---|
| `mappable` · `keepable` · `foldable` | trait — `apply (self) → self` · `keep (self) → bool` · `combine (self, self) → self` | unsatisfied = `E-BOUND-UNSAT` (compile time) |
| `map` | `(comptime t, slice t, mut slice t) → u64` — `dst[i]` gets the `apply` of `src[i]` | a short `dst` stops the run as it is written |
| `filter` | `(comptime t, slice t, mut slice t) → u64` — elements whose `keep` is true, from the front | the same |
| `fold` | `(comptime t, start t, slice t) → t` — the accumulator first, then the element | none — empty gives `start` |

*Table 50.1 — Ops of `collection`*

`map` and `filter` give **the number of elements written** (a `u64`, the same as `collect into`). The caller gives the storage — this module does not allocate.

**Why these are not core ops.** Until 2026-10-16 the three were core ops. A core op’s name cannot be an author’s name, so no local or op could be called `map`, which is also why [`mapgen`](sec117.md#mod-mapgen) chose the name `table`. Moving them into a library freed the names.

**Why not pass an op as a value.** This language has no way to pass an op as a value. Even if it had one, an indirect call would hide its cost at the call site. Each call builds an op for that type (monomorphisation), and `apply` is fixed in as a direct call. The price is that **one element type has one `apply`** — moving the same bytes in two ways takes two wrapper types. To see a byte slice as a slice of wrappers, use `view_array <wrapper> <slice>` (one field, the same layout).

> **Counter-example. Passing a `u64` slice as it is**
>
> > `collection.map u64. s. out. .` is `E-BOUND-UNSAT`. `u64` has no place to attach a method, so it cannot satisfy a trait — wrap it in a struct of one field.

**Not built** — a `map` that changes the element type (`t` to `u`), and a transformation that holds state (a factor `k`, say). Both need a trait that takes a type parameter, or an op taken as a translation-time argument — that syntax is left as the next piece of work.

---

[← Prev](sec69.md) · [Contents](README.md) · [Next →](sec71.md)
