#import "../../typst-ko/lib.typ": *

= `pagecache` --- page ids and pinning cursors <mod-pagecache>

#modhead(file: "lib/pagecache.low", layer: [L1 --- the caller's storage], caps: [none])

Separates *a long-lived name* (page id) from *short-lived access* (a pin) in a cache holding pages. What is stored outside is always the id; a pin is held only while touching
bytes. SQLite's page cache has this shape. Pages move (eviction, relocation). When they move, the only thing to fix should be the id → slot table; a cache that requires
hunting down scattered pointers is unusable.

```lowent
newtype db u8 .
var c be (owned pagecache.cache db) pagecache.open db 16 .
var p be (pagecache.pin db) pagecache.acquire db c 42 .
rem evict cannot be called in between --- it does not compile
var c2 be (owned pagecache.cache db) pagecache.release db p .
var c3 be (owned pagecache.cache db) pagecache.evict db c2 .
```

#aside[Evicting while a pin lives is a compile error][
  `acquire` *swallows* the cache token. So while a pin lives there is nothing in hand to pass to `evict` or `reset` --- `E-OWN-MOVED`. What prevents it is the language, not
  this module. `owned` already enforces "there is one token, and handing it over lets it go" (#chref("ownership")). Brands seal a store, tokens divide access units
  (#modref("shard")[`shard`]), and here the same rule makes a new protocol for free a third time.
]

#dtable(
  columns: 3,
  id: "mod-pagecache-limits",
  caption: [Limits and order --- where the numbers came from],
  [*What*], [*Decided*], [*Grounds*],
  [Default pin limit], [`machine.cache_line / 8` --- x86_64 and arm64 8 · mips_be 4 · cortex_m 1], [PostgreSQL sized its pin array at 8 entries, "64 bytes, about the size of a cache line"],
  [Release order], [LIFO --- a stacked pin swallows the previous one], [SQLite cursors hold pages as a stack],
)

The reason was written instead of the number. Hard-coding 8 is wrong on machines with a different cache line --- so it divides `machine.cache_line` (#chref("hardware")).

#dtable(
  columns: 3,
  id: "mod-pagecache-ops",
  caption: [Ops of `pagecache`],
  [*op*], [*What it does*], [*On failure*],
  [`cache` · `pin` · `pin2`], [Cache token · pin · stacked pin types], [---],
  [`open`], [Makes a cache token], [---],
  [`acquire`], [Pins a page --- swallows the cache token], [---],
  [`acquire_more`], [Stacks a pin --- swallows the previous pin (depth +1)], [---],
  [`release` · `release_more`], [Releases --- gives the token back], [---],
  [`evict` · `reset`], [Evict · clear. Requires the cache token], [compile error while pinned],
  [`slot_of`], [Current slot of an id], [`none` for an empty cache],
  [`pin_limit` · `within_limit`], [This machine's limit and the check], [---],
  [`epoch_of` · `depth_of` · `depth2_of`], [Observation], [---],
)

#antipattern[Releasing stacked pins from the inside][
  Pin `a`, stack `b` with `acquire_more db a 9`, then call `release db a` --- a compile error, because `a` is inside `b`. There is no way to break the order. That is LIFO.
]

#antipattern[Storing a slot for later][
  Holding a slot from `slot_of` and using it after an eviction gives no error, only a wrong slot. What is stored outside must be the id --- slots are obtained afresh each
  time. That is why this module separates ids from slots.
]

*Cautions.* Store neither slots nor pins --- both belong to this moment. The limit is a convention, not enforcement (depth is built only up to `pin2`). Reading pages in and
its failure, and eviction policies like LRU, are not here --- policy sits on top of this protocol.
