#import "../../typst-ko/lib.typ": *

= `strmap` --- string-keyed hash map <mod-strmap>

#modhead(file: "lib/strmap.low", layer: [L0 --- pure computation (the caller's backing)], caps: [none])

A hash map storing and finding `u64` values by byte string (string) keys --- word counts, symbol tables, configuration maps. It widens #modref("hashmap")[`hashmap`] to
variable-length byte string keys. Key bytes are *copied* into an arena on insertion, so they are not tied to the original string's lifetime.

```lowent
let stored bool strmap.put slots keys "apple" 1 .
let v option u64 strmap.lookup slots keys "apple" .
```

*The caller prepares two buffers.* `slots` must start all zeros.

- `slots` (`mut slice u64`) --- the slot table. `slots[0]` is *the arena cursor* (how many key bytes are used); real slots start at index 1, one slot = three u64 `[keyoff, keylen, value]`. N slots = `(len slots − 1) / 3`.
- `keys` (`mut slice u8`) --- the key byte arena. Bytes of many keys laid end to end in one buffer (a bump that only moves the cursor forward).

`keylen` has three meanings --- `0` = empty, `MAX_U64` = tombstone, anything else = the length of a live key. A real key length cannot be MAX, so it serves as the tombstone
value without reserving anything. The hash is FNV-1a 64 with linear probing.

#dtable(
  columns: 3,
  id: "mod-strmap-ops",
  caption: [Ops of `strmap` --- all `effects none`],
  [*op*], [*Shape*], [*Failure*],
  [`put`], [`proc (slots mut slice u64, keys mut slice u8, k slice u8, v u64) → bool`], [full · arena short · empty key → `false`],
  [`lookup`], [`proc (slots slice u64, keys slice u8, k slice u8) → option u64`], [absent · empty key → `none`],
  [`del`], [`proc (slots mut slice u64, keys slice u8, k slice u8) → bool`], [absent · empty key → `false`],
  [`size`], [`fn (slots slice u64) → u64`], [`len slots < 4` → 0],
  [`occupied_at`], [`fn (slots, slot u64) → bool`], [out of range → `false`],
  [`keylen_at` · `keyoff_at` · `val_at`], [`fn (slots, slot u64) → u64`], [out of range → 0],
  [`rehash`], [`proc (ns mut slice u64, na mut slice u8, os slice u64, oa slice u8) → bool`], [`false` midway if `ns` or `na` is too small],
)

- `put` updates only the value for the same key (length and bytes equal) --- no new slot, no arena use. New keys are copied into the arena. The first tombstone met while
  probing is reused.
- `del` overwrites keylen with a tombstone and zeroes the value. *The old key bytes stay in the arena* --- reclamation happens when `rehash` moves to a new arena (compaction).
- Read key bytes with `subslice keys off (add off klen)`. That is a view into the arena, valid only while the arena lives.

#antipattern[An empty string key][
  `strmap.put slots keys "" 7` is always `false`. `keylen 0` marks an empty slot, so an empty key cannot exist in the encoding. `lookup ""` is always `none` too.
]

#antipattern[Traversing by `keylen > 0` after deletion][
  A tombstone's keylen is MAX, so `gt keylen 0` is true --- deleted items creep into totals. Filter with `occupied_at` in any map that has ever used `del`.
]

#antipattern[Touching `slots[0]` directly][
  Tampering with the arena cursor makes the next `put` copy key bytes to the wrong place. Nothing stops; it is silently wrong. Initialise as "all zeros", then modify only
  through ops.
]

*Cautions.* The arena is a bump --- repeatedly inserting and deleting different keys can make `put` return `false` for lack of arena even with free slots. One `false` from
`put` does not distinguish slot shortage from arena shortage. Estimate sizes as `3N + 1` u64 for N slots and, for the arena, the sum of live key lengths plus dead bytes. A
real use reads a file, splits words with #modref("strings")[`strings`] and counts frequencies (#chref("lib-containers")).
