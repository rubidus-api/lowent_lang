#import "../../typst-ko/lib.typ": *

= `flags` --- named on/off settings in one word <mod-flags>

#modhead(file: "lib/flags.low", layer: [L1 --- pure computation], caps: [none])

Handles many on/off settings *in one integer*. Positions are called by *bit number* (0, 1, 2). Names live only at compile time; at run time one integer remains. Toggles on a
settings screen, feature switches, dirty markers, device status bits --- places where carrying each separately is wasteful and bundling makes comparison easy.

```lowent
fn f_sound output u64 . do return 0 . end
fn f_music output u64 . do return 1 . end
let s0 u64 flags.with (flags.empty) (f_sound) .
let playing bool flags.has s0 (f_music) .
```

#aside[What this module guards][
  *Nonexistent positions stop on entry* --- every op taking a position carries `requires lt n 64`. C's `1u << 64` is undefined behaviour; here it is rejection. *The cost is
  zero* --- code written with the library and hand-written bit operations have the same instruction count (x86-64 gcc -O2: set 17 = 17, test 18 = 18, subset 24 = 24,
  measured by tests every run). *Bit numbers must not overlap* --- the moment settings are saved to a file or device, the numbers become a promise. Deleting a middle
  position shifts what follows and silently changes the meaning of saved settings, so a deleted position stays empty and is never reused.
]

#dtable(
  columns: 2,
  id: "mod-flags-ops",
  caption: [Ops of `flags` --- one word (up to 64 positions)],
  [*What it does*], [*op*],
  [Empty value · one position], [`empty` · `of n`],
  [Set · clear · toggle · test], [`with s n` · `without s n` · `toggle s n` · `has s n`],
  [Union · intersection · difference · subset], [`join a b` · `both a b` · `minus a b` · `inside a b`],
  [How many · lowest position], [`ones s` · `lowest s` (contract `s > 0`)],
  [Any · none · all], [`has_any s` · `has_none s` · `is_full s w` (width `w`) · `widthmask w`],
  [What changed], [`changes a b` · `changes_in a b m`],
  [Only what differs from the default · replace only these positions], [`from_default s d` · `overlay base over m`],
)

*Beyond 64 positions* it is a byte array with different names --- `wide_bits s` (bytes × 8), `wide_has`, `wide_with`, `wide_without`, `wide_toggle`, `wide_ones`,
`wide_next s from` (the next set position, skipping empty bytes whole). The cost at the boundary was measured --- `has s 63` is 17 instructions, `wide_has s 64` is 26. The
nine-instruction difference is the price of "which byte" (division, indexing) and "does that byte exist" (bounds check). That is why the two implementations are not hidden
behind one name. Out of range is answered as a value (`false`, `0`) --- the width is decided at run time, so it cannot be a contract.

*Names are opt-in.* The label table belongs to the caller; the library holds no strings. `flags.label_in "sound\x00music\x00shadow\x00" 2` gives `"shadow"`, and
`label_count` answers the number of names. A program not using names has none of those characters in its binary --- tests compare the read-only data size of both outputs.

*When several flows modify the same word.* This module's ops take a value and return a value --- they do not modify a stored word. If several flows update with
`set shared (flags.with shared 2)`, other updates vanish between read → modify → write. Atomic operations answer that --- `atomic_or s 0 4` (set bit 2), `atomic_and s 0 251`
(clear), `atomic_xor s 0 4` (toggle). With a constant position the mask is constant and it lowers to one machine instruction (#chref("parallel-atomic")). For device
registers use `to_set` and `to_clear` of #modref("wire")[`wire`].

#antipattern[Writing a position as a mask][
  `flags.with (flags.empty) 64` meant mask 64 (= position 6), but this module reads "position 64", and with both constant it is a compile error (`E-CONTRACT-IMPOSSIBLE`).
  Numbers here are always positions. To work with masks, use `wire`.
]

#antipattern[Saving settings only as their current value][
  When defaults change, saved values go stale. Recording only the difference with `flags.from_default s d` is small and keeps its meaning when defaults change.
]

*Cautions.* Fields wider than one bit are `wire`'s job. Bit numbers are not managed automatically --- people write them, and a check in the development repository catches
two names using the same position. Why the ops are not `set`, `union`, `count` or `first` --- they are syntax words, existing operations, or were used as local names in
other files (`union` was a core op then; it is `bitset_union` now). Exporting `first` broke `lib/regex.low`, and the combination check caught it.
