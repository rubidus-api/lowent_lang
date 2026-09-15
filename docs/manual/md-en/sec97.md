# <a id="mod-pool"></a>`pool` — generational-handle block pool

Source

`lib/pool.low`

Layer

L1 — the caller’s storage

Capabilities

none

A pool lending and taking back fixed-size blocks. Handles carry a **generation number** so stale handles are recognised. Used when objects are created and deleted repeatedly and you want to stop the defect of reusing something already deleted (use-after-free). “Take 4 KiB at a time as needed, free a particular block when you choose” has a release time that is not lexical, so neither `region` nor a bump will do — so instead of static checking, generational handles (chapter 18, chapter 35).

```lowent
newtype pa u8 .
let po option (pool.block_pool pa) . be pool.init pa mem gens 4096 .
guard is_some po . else return 1 .
var p pool.block_pool pa . be some_value po .
let h option (pool.handle pa) . be pool.take pa p .
```

> **What it prevents and what it does not**
>
> > **Prevents ①** — **accessing a returned block through an old handle**. The generation differs, so it is rejected (as a runtime value). **Prevents ②** — **mixing pools**. Handles and pools carry a **brand** as a type, so putting `handle pa` into `block_pool pb` is the compile error `E-TYPE-INSTANCE`. And `init` seals `mem` and `gens`, so ops no longer take them — the path to passing the wrong array vanished from the surface. A brand is a type, not a value, so handles do not grow. **Does not prevent** — one brand per declaration. Calling an op that takes a brand as a comptime argument and runs `init` twice makes one brand cover two pools. Keeping one brand per pool is your job. These are not generational handles built into the language — this is an ordinary library, and the safety comes from its discipline.

**Principle.** Each block has a generation number, copied into the handle on take. Releasing bumps the block’s generation, and from that moment old handles no longer match and become invalid automatically. **Fixed size** makes release just pushing onto a free list, with zero fragmentation. Generations live in a parallel array `gens` (SoA), and the check cost falls only on code passing through the gate `bytes`. **Free list links live inside the blocks’ own bytes** — a released block has a bumped generation and nobody can reach it, so using its first 8 bytes as the ledger is free. The pool is a struct, not an actor (actor state cannot hold slices).

| **op** | **Shape** | **Failure** |
|---|---|---|
| `handle b` · `block_pool b` | structs — a handle has `blk` · `len` · `gen`; a pool has sealed `mem` · `gens` and cursors | — |
| `init` | `comptime b, mem mut slice u8, g mut slice u64, bs u64 → option (block_pool b)` | `none` if `bs < 8` |
| `blocks` | `fn (comptime b, p) → u64` — `min(len mem / bs, len g)` | none |
| `take` | `(comptime b, p mut block_pool b) → option (handle b)` | `none` if no blocks remain |
| `release` | `(comptime b, p mut, h handle b) → bool` | `false` for stale or out-of-range handles (double release included) |
| `bytes` | `(comptime b, p, h) → option mut slice u8` — the only gate to the bytes | `none` if stale or out of range |
| `alive` | `(comptime b, p, h) → bool` | none (false is the answer) |
| `used` · `outstanding` | `→ u64` — blocks ever handed out sequentially · blocks out right now | none |

*Table 50.1 — Ops of `pool` — the first argument is the brand, all `effects none`*

`take` **gives back released blocks first** (the free list is LIFO) — repeated take and release never dries the pool. A reused block’s handle is a new handle with a bumped generation. Why the brand is written every time — this language has no inferred type parameters, and that one word is the contract “this handle belongs to that pool”.

**One remaining window** — releasing while **holding** a slice borrowed through `bytes` leaves that slice usable. A `borrow <name> be <expr> do … end` block closes the window lexically — touch bytes only inside the borrow, and release outside it (chapter 12).

```lowent
newtype demo_brand u8 .

proc demo input mem mut slice u8 . . input gens mut slice u64 . . output u64 . effects none .
do
  let po option (pool.block_pool demo_brand) . be pool.init demo_brand mem gens 16 .
  guard is_some po . else return 89 .
  var p pool.block_pool demo_brand . be some_value po .
  let h option (pool.handle demo_brand) . be pool.take demo_brand p .
  guard is_some h . else return 91 .
  let hh pool.handle demo_brand . be some_value h .
  let b option mut slice u8 . . be pool.bytes demo_brand p hh .
  guard is_some b . else return 92 .
  let bv mut slice u8 . be some_value b .
  var total u64 be 0 .
  borrow v be bv do
    set (index v 8) 3 .
    set (index v 9) 4 .
    set total (add (narrow u64 (index v 8)) (narrow u64 (index v 9))) .
  end
  let rel bool be pool.release demo_brand p hh .
  guard eq rel true . else return 94 .
  let dead option mut slice u8 . . be pool.bytes demo_brand p hh .
  guard eq (is_some dead) false . else return 95 .
  return total .
end
```

> **Counter-example. Reaching through a released handle · double release**
>
> > After release `bytes` is `none`, and an unchecked `some_value` stops with `E-VM-NONE` on that line. A second `release` just returns `false` silently, so without looking at the return value a “believed released but not” defect hides. Receive it with `guard eq rel true .`.

> **Counter-example. Releasing with values that must survive in the first 8 bytes**
>
> > Release overwrites that place with a free list link. It is safe because the generation is bumped and nobody can reach it, but the expectation “it stays in memory after release” is wrong for the first 8 bytes. Put data from byte 8 on.

> **Counter-example. Sending a borrowed name out of the block**
>
> > `borrow v be bv do set out v . end` is the compile error `E-BORROW-ESCAPE` — a borrow ends at the end of its block.

**Cautions.** Handles are values and can be copied, but releasing through any copy stales them all. `outstanding` not growing no matter how often you take and release is proof the free list is alive (`used` grows only in the first few rounds). `outstanding` walks the free list, O(free blocks) — do not call it on every hot-path pass. One handle = one block. Sequential delivery is assumed. Why `outstanding`, not `live` — the processor could not tell common words used as local variables from op heads. Designing a handle’s bit widths is helped by [`budget`](sec99.md#mod-budget).

---

[← Prev](sec96.md) · [Contents](README.md) · [Next →](sec98.md)
