#import "../../typst-ko/lib.typ": *

= `budget` --- handle bit budgets and generation wraparound <mod-budget>

#modhead(file: "lib/budget.low", layer: [L1 --- pure computation], caps: [none])

Decides how many bits each of three pieces gets when a generational handle is packed into *one word*, and checks *at compile time* that the budget fits. It is the place for
writing a handle's widths in source and having the tool enforce them when you build your own store --- something that hands out and takes back slots, like
#modref("pool")[`pool`] or #modref("shard")[`shard`].

```text
h = (generation << (shard_bits + slot_bits)) | (shard << slot_bits) | slot
      which life                     which shard                which slot
```

```lowent
let ho option u64 budget.pack 32 8 24 7 1 0 .
guard is_some ho else return 1 .
let h u64 some_value ho .
let slot u64 budget.slot_of 32 h .
```

#aside[What this module guards][
  *A budget that does not fit does not compile.* `budget.pack 40 16 16 …` sums to 72, more than a word --- all constants, so it is `E-CONTRACT-IMPOSSIBLE` at the call site.
  *A value that does not fit its piece gives `none`* --- truncating silently would make two different handles equal. *When a generation wraps, the slot retires* ---
  `next_gen` answers `none` and does not revive it. Going back to 0 would revive every old handle for that slot, exactly what generational handles exist to prevent.
]

#dtable(
  columns: 4,
  id: "mod-budget-defaults",
  caption: [Recommended defaults and their grounds],
  [*Machine*], [*slot · shard · gen*], [*Capacity*], [*Grounds*],
  [64-bit], [32 · 8 · 24], [about 4.3 billion slots · 256 shards · about 17 million lives], [the widest widely used real id (SQLite pages) is 32 bits],
  [32-bit], [16 · 4 · 12], [65,536 slots · 16 shards · 4,096 lives], [lwIP counts lengths in 16 bits],
)

These are only defaults --- when a store writes its own numbers, those win.

#dtable(
  columns: 3,
  id: "mod-budget-ops",
  caption: [Ops of `budget`],
  [*op*], [*Shape*], [*On failure*],
  [`plan` · `default64` · `default32`], [Budget struct · recommended defaults], [---],
  [`pow2`], [`n u64 → u64` (contract `n < 63`)], [contract violation stops on entry],
  [`slots` · `shards` · `lives`], [`p plan → option u64`], [`none` if a width is 63 or more],
  [`pack`], [three budgets + three values → `option u64`], [`none` if a value exceeds its piece · compile error if the budget overflows],
  [`slot_of` · `shard_of` · `gen_of`], [budget + `h` → `u64`], [contract violation stops on entry],
  [`retired`], [`gen_bits, gen → bool`], [---],
  [`next_gen`], [`gen_bits, gen → option u64`], [`none` if the slot is retired],
)

#antipattern[Simply incrementing the generation][
  `let g u64 add gen 1 .` gives no error, but past the width it equals a handle from another life. Use `next_gen` and accept `none` --- it means that slot is finished.
]

#antipattern[Counting a 63-bit piece][
  `budget.pow2 63` is rejected by the entry contract. `shl 1 63` overflows signed 64 bits into a negative --- rejecting beats silently returning a wrong number.
]

*Cautions.* Give *the same budget* when unpacking --- if `pack` and `slot_of` see different numbers the answer is silently wrong. Write the budget once as three constants and
use only those. Why the op is `pow2`, not `cap` --- `cap` is a syntactic position (`input k cap clock .`), and as an op name it could not stand in one unit with `clock`.
Two-word handles were not built.
