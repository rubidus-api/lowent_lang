#import "../../typst-ko/lib.typ": *

= `segarena` --- fixed-size segment arena <mod-segarena>

#modhead(file: "lib/segarena.low", layer: [L1 --- the caller's storage], caps: [none])

Grows storage not as one block but in *equal-size segments*. Inside a segment memory is contiguous, so scanning is fast, and growth *never moves what already exists*. One
index splits into a segment and a slot --- that is all.

```text
segment = i >> kbits          slot = i & ((1 << kbits) - 1)
address = directory[segment] + slot
```

```lowent
def newtype grid u8 .
var a be (segarena.arena grid) segarena.open grid 4 .
let b be option u64 segarena.grow grid a dir 1024 .
guard is_some b . else return 1 .
let v be option u64 segarena.at grid a dir mem 9 .
```

#aside[The cost is written in the op name --- this is the point of the module][
  The same sum over 20.48 million elements was measured two ways. `sum_seg`, borrowing each segment once, looks at the directory once per segment and took 21 ms;
  `sum_global`, reading by global index every time, looks per element and took 81 ms. The answers are equal. The compiler does not know about segments --- choosing the fast
  path is *the op you call*. The cost of global random access is not hidden as if it were a flat slice.
]

#dtable(
  columns: 3,
  id: "mod-segarena-ops",
  caption: [Ops of `segarena`],
  [*op*], [*What it does*], [*On failure*],
  [`arena`], [The arena type (holds only a cursor and shape)], [---],
  [`open`], [Opens an arena (`2^kbits` slots per segment)], [contract `1 ≤ kbits ≤ 20` --- rejected at compile time for constants],
  [`grow`], [Opens one more segment and records its start in `dir`], [`none` if no room or over capacity],
  [`seg_slots` · `seg_of` · `slot_of`], [Slots per segment · global index → segment · slot], [---],
  [`at`], [Read one slot by global index (convenient, consults the directory every time)], [`none` if out of range or segment not opened],
  [`walk_base`], [Borrows a segment's start once (for hot traversal)], [`none` if segment not opened],
  [`sum_seg` · `sum_global`], [The same sum by two paths --- witnesses of the cost comparison], [---],
)

#antipattern[A meaningless segment size][
  `segarena.open g 0` is the compile error `E-CONTRACT-IMPOSSIBLE` --- a one-slot segment is not a segment. The argument is constant, so it is decided at the call site
  (#chref("contracts")).
]

#antipattern[Hot traversal by global index][
  Calling `segarena.at` per element inside a `while` gives no error, only slowness (3.9× in the measurement). Traverse segment by segment and borrow the start once with
  `walk_base`.
]

*Cautions.* Storage belongs to the caller --- `dir` (start of each segment) and `mem` (where slots live) come from outside (the same discipline as #modref("pool")[`pool`] and
#modref("shard")[`shard`]). Reclamation is bulk --- there is no way to return one segment. If individual reclamation is needed, `pool` is the place. Variable-size segments
are not built --- the `i >> k` formula would not hold and the cost would change, so that must be a different module. Views borrowing segments belong to
#modref("segview")[`segview`].
