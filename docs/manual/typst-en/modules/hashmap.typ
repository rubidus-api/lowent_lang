#import "../../typst-ko/lib.typ": *

= `hashmap` --- `u64` → `u64` hash map <mod-hashmap>

#modhead(file: "lib/hashmap.low", layer: [L0 --- pure computation (the caller's backing)], caps: [none])

A hash map storing and finding `u64` values by `u64` keys. Used to look things up quickly by number (id → count, handle → state). String keys are #modref("strmap")[`strmap`];
the container that chooses element type and allocator is #modref("mapgen")[`mapgen`] (#chref("lib-containers")).

```lowent
let stored bool hashmap.put b 7 42 .
let v option u64 hashmap.lookup b 7 .
```

#aside[Two things to know][
  ① The map makes no memory of its own --- the caller provides storage `b` and passes it every time. ② A new map *must start all zeros*. Break either and answers go wrong
  without errors --- a slice full of garbage reads as "a map that already holds things", so keys never inserted are found, or a key just inserted comes back `none`.
]

*Open addressing.* No linked list per key; if a slot is taken, insertion moves to the next slot of the same table (linear probing) --- one buffer suffices, matching the
caller-owned discipline. The backing is one `mut slice u64` laid out as `[key0+1, val0, key1+1, val1, …]` (N slots = `len / 2`). Key slots store `k+1`, so `0` marks an empty
slot and *key 0 can be stored*. Deletion overwrites the key slot with a *tombstone*, `MAX_U64` --- turning it back into an empty slot would break the trail of keys pushed
further along by collisions, making later keys unfindable. `lookup` skips tombstones, `put` reuses them, traversal does not count them. The hash is multiplicative (golden
ratio constant).

#dtable(
  columns: 3,
  id: "mod-hashmap-ops",
  caption: [Ops of `hashmap` --- all `effects none`],
  [*op*], [*Shape*], [*Failure*],
  [`put`], [`proc (b mut slice u64, k u64, v u64) → bool`], [full · reserved key → `false`],
  [`lookup`], [`proc (b slice u64, k u64) → option u64`], [absent · reserved key → `none`],
  [`del`], [`proc (b mut slice u64, k u64) → bool`], [absent · reserved key → `false`],
  [`size`], [`fn (b slice u64) → u64`], [slot count (not the item count)],
  [`occupied_at`], [`fn (b slice u64, slot u64) → bool`], [out of range → `false`],
  [`key_at` · `val_at`], [`fn (b slice u64, slot u64) → u64`], [meaningful only when occupied],
  [`rehash`], [`proc (nb mut slice u64, ob slice u64) → bool`], [`false` midway if `nb` is too small],
)

- `put` updates in place for an existing key. It remembers the first tombstone met while probing and inserts there once the key is confirmed absent further on --- tombstone
  reuse keeps clusters from growing.
- *It does not grow by itself.* When full, `put` returns `false` (no stop). To grow, the caller prepares a larger zeroed backing and calls `rehash nb ob`. Only live items move,
  so tombstones disappear too (compaction). The old backing is only read, so it stays intact on failure.
- *Two keys cannot be stored.* `MAX_U64` and `MAX_U64 − 1` are reserved by the encoding --- the price of getting deletion without a separate status byte.
- Traversal scans `0 .. size` and handles only `occupied_at` slots. Order is hash order --- for sorted enumeration, extract the keys and sort them with
  #modref("sortlib")[`sortlib`].

```lowent
proc hm_sum input b mut slice u64 . output u64 . effects none . do
  var i u64 0 .
  while lt i (len b) do
    set (idx b i) 0 .
    set i (add i 1) .
  end
  guard ge (div (len b) 2) 4 else return 0 .
  guard eq (hashmap.put b 10 100) true else return 0 .
  guard eq (hashmap.put b 20 222) true else return 0 .
  guard eq (hashmap.del b 10) true else return 0 .
  var slot u64 0 .
  var total u64 0 .
  while lt slot (hashmap.size b) do
    if hashmap.occupied_at b slot do
      set total (add total (hashmap.val_at b slot)) .
    end
    set slot (add slot 1) .
  end
  return total .
end
```

#antipattern[Reading slots without `occupied_at`][
  `key_at` and `val_at` of empty or tombstone slots are meaningless (an empty slot's `key_at` goes as far as `0 − 1`), so totals go wrong without errors. Always filter traversal
  with `occupied_at`.
]

#antipattern[Ignoring `put`'s `false`][
  A full map answers `false` instead of stopping, so the line passes silently; later `lookup` returns `none` and an unchecked `some_value` stops with `E-VM-NONE`. The stop is
  far from the cause --- `guard eq (hashmap.put …) true` should be habit.
]

*Cautions.* Why `lookup`, not `get`, and `size`, not `capacity` --- `get` is the payload enum destructuring word and `capacity` is syntax in `alloc_bytes … capacity n`, so ops
cannot have those names. Repeated insert and delete pile up tombstones and lengthen probes --- move to a same-size backing with `rehash` to compact. With an odd backing length
the last u64 is unused.
