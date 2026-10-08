#import "../../typst-ko/lib.typ": *

= Storage and handles — `pool`, `shard`, `budget`, `wire`

#chapter-toc()

#prereq(
  ([#chref("structs-enums"), Groupings], [linked by number, an old number sees the wrong node]),
  ([#chref("named-types"), Named types], [a `newtype` is a new type with the same representation]),
  ([#chref("ownership"), Ownership], [an `owned` value moves when handed over]),
  ([#chref("fixed-memory"), Allocators and fixed memory], [a bump takes back only its last piece]),
)

#deepqa[
  In #chref("structs-enums"), what inconvenience arose when linking a tree by numbers, and what did it say catches it?
][
  If a node is deleted and its number reused, whoever held the old number sees the wrong node. It said the standard library's `pool` catches this at run time with
  *generational handles* that carry a generation count alongside the number. This chapter covers that `pool` and other modules handling storage.
]

#why[
  Bump allocators and regions are tools for "give it all back at once". They do not fit data made and deleted endlessly, like game objects, linked lists and cache
  pages. Using pointers for such data brings use-after-free, and using numbers brings number reuse. The modules in this chapter stop the same problem at different
  layers --- run-time generation comparison, type brands, ownership tokens, translation-time bit budgets. Not a single new word was added. All are combinations of rules
  learned in earlier chapters.
]

#organizer[
  You will learn how `pool` lends fixed-size blocks through generational handles and recognises old handles after release. You will see how `newtype` brands stop the
  mistake of mixing different pools at translation. You will also see `shard` dividing storage into non-overlapping pieces with ownership tokens, `budget` checking a
  handle's bit budget at translation time, and `wire` handling bit fields with a single mask.
]

#chapter-questions()

== Generational handles

#demo("examples/ch35/blocks.low")

- `pool.init objects mem gens 16` divides the bytes `mem` handed over by the caller into 16-byte blocks and keeps a generation count per block in `gens`. The pool seals
  `mem` and `gens` inside, so later ops do not take them separately.
#idx("generational handle")
- `pool.take` lends one block and gives a *handle*. The handle holds the block number and the generation count at that moment.
- `pool.release` takes the block back and increments that block's generation count.
- An old handle after release has a different generation, so `alive` is false, and a second `release` is rejected too.

The result 3 means "alive before release (1), and the first release succeeded (2)", and that after release it was neither alive (4) nor did a second release succeed (8).
Use-after-free and double free come out *as values* at run time.

#qa[
  Generation comparison is a run-time check. Can't translation stop it?
][
  Handles are values that can be copied, stored and written to files, so translation cannot know every handle that has gone stale. So this place is a dynamic check
  (in #chref("ownership")'s strength table, "dangling references of generational handles --- dynamic"). Instead, the mistake translation can stop --- mixing pools --- is
  stopped at translation, as below. What translation stops and what run time stops is written at the top of the module document.
]

== Brands --- no mixing pools

#idx("brand")
Pools and handles carry a *brand* as a translation-time type parameter. One `def newtype objects u8 .` declaration is one store.

#demo("examples/ch35/mixed.low")

Trying to return a handle borrowed from the `sounds` pool to the `meshes` pool is rejected, because the two instances are different concrete types. A brand is a type,
not a value, so the handle's fields do not grow. The cost is zero. Opening a store twice with the same brand is rejected too (`E-BRAND-REUSED`) --- then one name would
point at two, and the confusion the brand was meant to prevent would return.

== Non-overlapping pieces --- `shard`

`shard` divides one block of storage into non-overlapping pieces and gives a *token* per piece. A token is the right "this range is mine" and is an `owned` value.

#demo("examples/ch35/split.low")

`shard.split_at grid r 4` *consumes* the whole token `r` and splits it in two. Asking the width with `r` afterwards is `E-OWN-MOVED`. What stops this mistake is not the
`shard` module but *the language*. The token is `owned`, so it leaves your hands the moment it is handed over. The idea of an access unit is not a new rule but the
consequence of an existing one. Whether the pieces ride several flows or run sequentially, the fact that they do not overlap is itself the value.

The same shape is in `pagecache`. Pinning a page (`acquire`) consumes the cache token, so while the pin lives there is no token in hand to pass to eviction (`evict`). The
defect of evicting a pinned page is stopped at translation.

== Bit budgets --- `budget`

To put a generational handle in one word you must decide "how many bits go to slot number, shard number and generation count". `budget` writes those numbers in the source
and has the tool keep them.

#demo("examples/ch35/budget_bad.low")

`budget.pack 40 16 16 …` sums to 72 bits, which does not fit in a 64-bit word. All arguments are constants, so the contract violation is decided at translation
(#chref("contracts")). As the diagnostic's story says, a program with a bit budget that did not fit was once shipped in runnable form. This module is the first place to use
the check born of that. When a generation count fills up, the slot *retires* (`retired`). An old handle never comes back to life by secretly wrapping to 0.

== Bit fields --- `wire`

#demo("examples/ch35/fields.low")

When several values sit side by side in one integer, `wire` handles a field with *a single mask*. The mask `0xF00` (3840) states both the position and width, "4 bits
starting at bit 8". `pick` extracts the field's value, and `merge` swaps just that field. 1234 is `0x4D2`, so the field's value is 4, and swapping in 5 gives `0x5D2`
(1490). The two numbers "position" and "width", which always diverged when written by hand, come from one mask and so cannot diverge. Packing named on/off settings into
one word is the job of `flags`.

#misconception[Modules like these must be written in unsafe code underneath][
  `pool`, `shard`, `budget` and `wire` are all written in the Lowent this book taught, without `unsafe`. Generation comparison is slice indexing and comparison, brands are
  `newtype` and type parameters, tokens are `owned`, and budgets are contracts. No new word or core op was added. Because the library was written without privileges, the
  guarantees of programs using it are not broken.
]

== Other storage modules

#dtable(
  columns: 2,
  id: "alloc-more",
  caption: [Other storage modules],
  [*Module*], [*In one line*],
  [`allocs`], [Bump allocators, the allocator trait and the default allocator (#chref("fixed-memory"))],
  [`segarena`], [Grows in same-sized pieces and never moves what exists. Costs are written in op names],
  [`pagecache`], [Separates long-lived names (page numbers) from short-lived access rights (pins)],
  [`segview`], [Cursor, total length and flattening for views over scattered pieces],
  [`lifemode` · `lifeatom`], [Four ways to end a value --- single ownership, thread-confined reference counting, atomic reference counting, external completion],
)

== Common mistakes

#antipattern[Taking out the bytes of a returned block without asking][
  #demo("examples/ch35/mistake_staleuse.low")

  `pool.bytes` is *the only door* to a block's bytes, and it returns `none` when the generation does not match. In C, writing 7 into a
  freed block would silently corrupt another object; here the spot shows up as the value `none`. Taking it out with `some_value` without
  asking stops the program, as in this example. Stopping beats corruption, but code in which handles may live long should ask with
  `is_some` and handle stale handles separately.
]

#antipattern[Opening two stores under the same brand][
  #demo("examples/ch35/mistake_brandreuse.low")

  The brand `objects` names *one* store. If the second `pool.init objects …` were accepted, two pools would share one type, and the types
  could no longer stop a handle from the first pool being returned to the second. Hence `E-BRAND-REUSED`. Declare one `newtype` per store;
  that one-line declaration is a cost-free distinction.
]

#misconception[A value put into a `wire` field stops the program when it overflows][
  #demo("examples/ch35/wire_truncates.low")

  `wire.put` *truncates* values larger than the field. Putting 20 (`0b10100`) into a four-bit field keeps only the low four bits, 4, giving
  1024 (`0x400`). The module documentation promises this and advises asking with `wire.fits` first if truncation is unwanted (20 gives 0,
  i.e. false). Bit fields going onto a wire often truncate by standard, which is why it was designed so. Where a stop is wanted, write
  `requires wire.fits …` as a contract.
]

== This chapter's syntax at a glance

#dtable(
  columns: 3,
  id: "lib-alloc-glance",
  caption: [Shapes of stores and handles --- shape · meaning · why it looks this way],
  [*Shape*], [*Meaning*], [*Why*],
  [`def newtype objects u8 .`], [the brand of one store], [cost-free distinction --- mixing: `E-TYPE-INSTANCE`, opening twice: `E-BRAND-REUSED`],
  [`pool.init objects mem gens 16`], [a pool carving the caller's bytes into blocks (sealed in)], [later ops do not take `mem`/`gens` again],
  [`pool.take` · `pool.release` · `pool.alive`], [borrow (generation handle) · return (bump generation) · ask], [use-after-free and double free surface as values],
  [`pool.bytes objects p h` → `option mut slice u8`], [the only door to a block's bytes], [`none` when the generation differs],
  [`var r (owned shard.token grid) shard.open grid 8 .`], [an owned token for a non-overlapping piece], [after splitting, the old token is `E-OWN-MOVED`],
  [`budget.pack …`], [check a bit budget by contract], [a misfit is `E-CONTRACT-IMPOSSIBLE` at translation],
  [`wire.pick mask w` · `wire.merge mask w v` · `wire.fits`], [read · replace · check a bit field with one mask], [position and width come from one number --- `put` truncates],
)

#recap[
  `pool` lends blocks through generational handles and reports old handles after release and double frees as values at run time. `newtype` brands stop mixing pools at
  translation at no cost. `shard` and `pagecache` use `owned` tokens to stop overlap and eviction while pinned at translation. `budget` checks a handle's bit budget at
  translation time with contracts, and `wire` handles a bit field's position and width together with one mask. All are written with the language's rules and no new
  words.
]
