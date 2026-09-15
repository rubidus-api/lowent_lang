#import "../../typst-ko/lib.typ": *

= `mapgen` --- generic hash map `table k v` <mod-mapgen>

#modhead(file: "lib/mapgen.low", layer: [L1 --- storage (carries an allocator)], caps: [none --- `open` is handed a bump allocator])

A table finding values by key, with key and value types *of your choosing*. Each op call writes the key type and value type in that order (for the same reason as
#modref("vecgen")[`vecgen`]). #modref("hashmap")[`hashmap`] is fixed to `u64 → u64`, keeps storage with the caller and makes you call `rehash` yourself. `mapgen` has open
types, one value owns storage and allocator, and it rehashes itself when full.

```lowent
let mo option (mapgen.table u32 u64) . using bump be mapgen.open u32 u64 4 .
guard is_some mo . else return 1 .
var m mapgen.table u32 u64 . be some_value mo .
guard mapgen.insert u32 u64 m 7 900 . else return 2 .
let x option u64 . be mapgen.lookup u32 u64 m 7 .
guard mapgen.erase u32 u64 m 7 . else return 3 .
```

*Design.* ⓪ Why `table`, not `map` --- `map` is a builtin op. When one word means two things, how sentences group becomes unstable. Generics had been slipping past that check;
closing the hole caught this module first. ① *No key value is reserved for empty* --- a separate status array (`flags`: 0 empty · 1 occupied · 2 tombstone). The common
shortcut "key 0 = empty" pretends key 0 is stored and silently gives wrong answers. ② *Tombstones remain* --- turning a deleted slot back to empty makes keys placed beyond it
unfindable forever. ③ *Load factor ≤ 0.5* --- before passing half, slots double and everything is rehashed. ④ The hash is for distribution, not cryptography (one round of
multiply-xorshift). ⑤ *Traversal is by slot, not iterator* --- a hash table has no natural "next element", so instead of an iterator object carrying state, `next_used` gives
the next occupied slot. There is no object to invalidate.

#dtable(
  columns: 3,
  id: "mod-mapgen-ops",
  caption: [Ops of `mapgen` --- the first two arguments are key type and value type],
  [*op*], [*effects*], [*What it does*],
  [`table`], [---], [struct --- `al` · `keys` · `vals` · `flags` (bytes) · `n` (pairs held)],
  [`open k v want` (`using al`)], [state], [Opens an empty map (ready for `want` pairs; slots are `(want+1)*2` rounded up to a power of two, at least 8)],
  [`insert k v m key val`], [state], [Inserts (overwrites if present). Rehashes first if the load factor would be exceeded. `false` = no room],
  [`lookup k v m key` · `has k v m key`], [none], [Value → `option v` · is it present],
  [`erase k v m key`], [none], [Deletes (leaving a tombstone). `true` if it was present],
  [`count_of k v m` · `slots_of k v m`], [none], [Pairs held (tombstones not counted) · slot count],
  [`next_used k v m from`], [none], [First occupied slot from `from` → `option u64`],
  [`key_at k v m at` · `val_at k v m at`], [none], [Key · value at that slot. `none` if not occupied],
)

The key array is `slots × size_of k` bytes and the value array `slots × size_of v` bytes --- opening `table u8 u16` for 3 pairs takes 32 bytes with 8 slots; the same count as
`table u32 u64` takes 104. The arithmetic differs per instance.

```lowent
var at u64 be 0 .
var total u64 be 0 .
var going bool be true .
while going . do
  let nx option u64 . be mapgen.next_used u32 u64 m at .
  guard is_some nx . else do
    set going false .
    continue .
  end
  let s u64 be some_value nx .
  let v option u64 . be mapgen.val_at u32 u64 m s .
  guard is_some v . else return none .
  set total (add total (some_value v)) .
  set at (add s 1) .
end
```

The frequency-count idiom is `lookup` → 1 if absent, +1 if present → `insert`. With value type `u8` and only 1 stored, it is a set.

#antipattern[Inserting while traversing][
  If `insert` rehashes, slot numbers change meaning and entries are skipped or seen twice. Collect what to insert and insert after traversal. `erase` is safe during traversal
  (it only leaves tombstones).
]

#antipattern[Not advancing `at` by one][
  `set at (some_value nx)` finds the same slot forever. The next start is *previous slot + 1*.
]

#antipattern[Non-integer keys · swapping type order][
  `table f32 u64` is unusable because `widen u64` produces meaningless bits --- byte string keys are #modref("strmap")[`strmap`]. Opening `table u32 u64` and calling with
  `u64 u32` calls a different instance (the type checker catches it).
]

*Cautions.* Keys and values are integer scalars. `none` from `lookup` is not an error but the common normal case "that key is absent". `erase` decreases `count_of`, but slots do
not shrink. Order is not promised (hash order, changing on rehash). Growth rehashes everything, costing O(slots) at that moment, and on a bump three old arrays remain ---
giving a proper `want` is the practical gain. A map lives within its allocator's lifetime.
