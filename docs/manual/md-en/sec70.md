# <a id="mod-searchlib"></a>`searchlib` — binary search on sorted slices

Source

`lib/search.low`

Layer

L0 — pure computation

Capabilities

none

Finds values in a **sorted** slice by binary search. Probe the middle, decide whether the target is left or right, and search only the remaining half — about twenty steps even for a million elements. In exchange, elements must already be in line to choose a direction. It is the partner of [`sortlib`](sec68.md#mod-sortlib), and together they make **sorted sets and maps** — unlike a hash map, order is kept, so range queries and “the next larger value” work. The file is `search.low`, but the module name is `searchlib`.

```lowent
use searchlib .
use sortlib .

sortlib.sort s .
let i option u64 searchlib.bsearch s 42 .
```

> **Sortedness is not checked**
>
> > The premise that `s` is ascending is **the caller’s job**. If broken, answers are wrong but nothing stops — no out-of-range access happens, so there is nothing to stop for; instead **a silently wrong answer** comes out. “`none` for a value that is definitely there” almost always means it was not sorted.

| **op** | **Shape** | **Answer** |
|---|---|---|
| `bsearch` | `(s slice u64, target u64) → option u64` | index of `target`, or `none`. With duplicates, **one** of them (which one is unspecified) |
| `lower_bound` | `(s slice u64, target u64) → u64` | first index at or above `target`. `len s` if all are smaller |

*Table 50.1 — Ops of `searchlib` — all `fn` · `effects none`*

How to choose — “is it there, and where” is `bsearch`; “where would it go” or “where do values at or above this start” is `lower_bound`. `lower_bound` is both **insertion point** and **start of a range** — `[lower_bound s a, lower_bound s b)` is the index range of elements with `a ≤ x < b`. It is not an `option` because the question has no “not found” (the end index is an answer too). There is no upper_bound, insert or delete — one lower bound gives membership, insertion point and range queries.

The implementation runs on the half-open range `[lo, hi)` with midpoint `lo + (hi − lo)/2` — `(lo + hi)/2` can overflow at large indices (the classic defect).

```lowent
fn count_in input s slice u64 . input a u64 . input b u64 . output u64 . do
  let i u64 searchlib.lower_bound s a .
  let j u64 searchlib.lower_bound s b .
  guard lt i j else return 0 .
  return sub j i .
end
```

> **Counter-example. Not separating `bsearch`’s `none`**
>
> > `return some_value r .` compiles, and stops with `E-VM-NONE` the moment nothing is found. Testing only with present values never triggers it — seeing an `option`, add `guard is_some` (chapter 11).

> **Counter-example. Giving meaning to `bsearch`’s index among duplicates**
>
> > In `[5,5,5]`, `bsearch s 5` may yield 0, 1 or 2. If you need “the position of the first 5”, use `lower_bound s 5`.

> **Counter-example. Indexing directly with `lower_bound`’s result**
>
> > If not found, `i` is `len s`, a position that does not exist — `idx s i` stops with `E-VM-BOUNDS`. It only fires when the value exceeds the table’s maximum, so it tends to hit with real data. Put `guard lt i (len s)` first.

**Cautions.** It cannot be used as is on descending data (there is no place to swap in a comparator). **Sort once, search many times** — calling `sort` before each search loses the whole gain. After sorting, changing one element with `set` requires sorting again. Keep the order when subtracting two lower bounds — `sub` underflows. As pure ops, the same slice can be searched from many places at once (while nobody writes). Other element types are `lower_by` and `find_by` of [`sortgen`](sec69.md#mod-sortgen).

---

[← Prev](sec69.md) · [Contents](README.md) · [Next →](sec71.md)
