# <a id="mod-vecs"></a>`vecs` — growable byte vectors with caller-held buffers

Source

`lib/vec.low`

Layer

L1 — allocator via `using`

Capabilities

none — an allocator is handed in

A byte vector that, when short of room, **takes a larger place and moves**. Used to gather byte strings of unknown final size. What is missing is reallocation, not growth — take a larger place and move the old contents, which can be written in Lowent. So it is a library, not a builtin, and the growth policy (doubling) belongs to the library too.

|  | **`vecs`** | **[`vecgen`](sec106.md#mod-vecgen)** |
|---|---|---|
| Storage | **held by the caller** — buffers are passed to ops | owned by the container |
| Growth | the new place is **returned as a value** (the caller rebinds) | swapped inside |
| Allocator | handed over with `using` on each call | held as a field |

*Table 50.1 — `vecs` and `vecgen` — ownership is the difference*

`vecs` is for when you want to hold buffer lifetimes **directly** inside a region. A vector lives as **a pair of two values**: `vec_u8` (a single length `len`) and `mut slice u8` (storage). Capacity is not stored separately — `len buf` is the capacity. Push maintains the invariant `v.len ≤ len buf`.

| **op** | **Shape** | **Failure** |
|---|---|---|
| `vec_u8` | struct — `len u64` | — |
| `next_cap` | `fn (cur u64) → u64` — 8 from 0, otherwise double | none |
| `push_byte` | `proc (comptime a, using al a, v mut vec_u8, buf mut slice u8, x u8) → option (mut slice u8)`, `effects state via a`, `requires allocs.byte_allocator a` | `none` on OOM — `v` and `buf` unchanged |

*Table 50.2 — Ops of `vecs`*

With room, `push_byte` writes in place and returns `some buf`; when full it takes double with `send al reserve (next_cap (len buf))`, copies the old contents and returns `some <new buffer>`. The allocator is handed over with a `using` clause, not a positional argument — the type `a` is filled from that source and monomorphised, so passing it costs nothing (chapter 35). No hidden allocation, so code without an allocator cannot call this op.

```lowent
module vecgrow .
use vecs .
use allocs .

proc main input al cap allocator . input out cap io . output u8 . effects io alloc state . do
  let memopt option mut slice u8 . be alloc_bytes al capacity 256 .
  guard is_some memopt . else return 1 .
  let mem mut slice u8 . be some_value memopt .
  var bump allocs.bump_bytes be spawn actor allocs.bump_bytes . .
  var c u64 be send bump init mem .
  let b0 option mut slice u8 . be send bump reserve 2 .
  guard is_some b0 . else return 2 .
  var buf mut slice u8 . be some_value b0 .
  var v vecs.vec_u8 be make vecs.vec_u8 do len 0 . end
  var i u64 be 0 .
  while lt i 10 . do
    let r option mut slice u8 . using bump be vecs.push_byte v buf (narrow u8 (add 65 i)) .
    guard is_some r . else return 3 .
    set buf (some_value r) .
    set i (add i 1) .
  end
  let m u64 be write_out out 1 (subslice buf 0 (field v len)) .
  return narrow u8 (field v len) .
end
```

Starting at 2 bytes and pushing 10, it grows three times, 2 → 4 → 8 → 16, and prints `ABCDEFGHIJ`.

> **Counter-example. Not rebinding to the returned buffer**
>
> > Forgetting `set buf (some_value r) .` compiles and runs, but the result is silently wrong. After growth, passing the old `buf` keeps `v.len` counting against the new buffer while writes go to the old (small) one. **The return value is the next buffer** — the most dangerous mistake in this module.

> **Counter-example. Ignoring OOM · calling from a pure fn**
>
> > Unwrapping `some_value r` unchecked stops with `E-VM-NONE` when the allocator runs dry. Calling from an `effects none` `fn` is `E-EFFECT-CALC` — `push_byte` is a proc with `effects state`. Passing a source that does not satisfy the trait (such as `u64`) is `E-BOUND-UNSAT`.

**Cautions.** Growth is copying — one push is O(len) worst case, but doubling gives amortised O(1). If the size is known ahead, starting that large is cheaper. **Old buffers are not reclaimed** — growing to n on a bump consumes about 2n of backing, and old buffers are swept away together when the `region` ends (chapter 18). Header and buffer are a pair — nothing prevents mixing those of different vectors. There is no element type generalisation, pop, insert, remove or shrinking.

---

[← Prev](sec72.md) · [Contents](README.md) · [Next →](sec74.md)
