#import "../../typst-ko/lib.typ": *

= `lifemode` · `lifeatom` --- when a value ends <mod-lifemode>

#modhead(file: "lib/lifemode.low · lib/lifeatom.low", layer: [L1 --- the caller's storage], caps: [`cap atomic` for `lifeatom`])

There are four ways to decide when a value ends, and each has a different cost. No new word or core op was added --- this only bundles what the language already knew into
something convenient.

#dtable(
  columns: 3,
  id: "mod-lifemode-modes",
  caption: [Four ways to decide when a value ends],
  [*Mode*], [*Written with*], [*Cost*],
  [① One owner], [`owned` --- one word does it (#chref("ownership"))], [no counting],
  [② Thread-confined reference count], [`lifemode.near_*` --- plain addition], [0 atomic instructions],
  [③ Atomically shared reference count], [`lifeatom.rc_*` + `cap atomic`], [atomic instructions present],
  [④ External completion], [a completion token as an `owned` ticket], [no counting; the compiler counts instead],
)

```lowent
guard lifemode.near_open c. 0 . else return 1 . .
let a option u64 lifemode.near_share c. 0 . .
let b option u64 lifemode.near_drop c. 0 . .
rem answering 0 means it was the last --- the caller then reclaims the storage
```

The atomic side is a different module --- `lifeatom.rc_open`, `rc_share k c`, `rc_release k c` (`k` = `cap atomic`). `rc_release` answering 0 means it was the last. ④ is not a
library but a shape --- make completion *consume* the token, as in `fn finish input t owned mymod.ticket . output result u64 mymod.late .`, and completing twice is
`E-OWN-MOVED`, forgetting to complete (when completion can fail) is `E-OWN-INCOMPLETE`. *Exactly once* comes from the language, not a library.

#dtable(
  columns: 2,
  id: "mod-lifemode-ops",
  caption: [Ops of `lifemode` · `lifeatom`],
  [*op*], [*What it does*],
  [`lifemode.ceiling`], [The count ceiling both modes share],
  [`lifemode.near_open` · `near_share` · `near_drop`], [Confined count: open at 1 · add one · remove one (0 = last)],
  [`lifeatom.rc_open` · `rc_share` · `rc_release`], [Atomic count: the same three, taking `cap atomic`],
)

*Why two modules --- `use` costs what is exported, not what is called.* At first all four were in one module. Then the binary of a program using only confined counting still
had 2 `lock`-prefixed instructions. Exported ops get global symbols through C ABI wrappers, so the linker cannot discard them. Splitting the atomic side into `lifeatom`
brought it to 0. The cost of bringing in atomics is visible in one line, `use lifeatom`.

*Overflow --- it saturates, and that object lives forever.* Counting with plain atomic addition silently wraps to 0 at the ceiling (confirmed by running natively). A wrapped
count reaches 0 early and frees a live object --- the road to use-after-free. So this module *saturates* (the same answer Linux `refcount_t` chose). At the ceiling it neither
increments nor decrements. That object never dies, but memory safety is not broken. The cost is about 19 % more instructions; the atomic side's `lock` count stays at one per
attempt (a CAS loop only spins under contention).

*Not built* --- true asynchronous completion (no surface without backing), completion callbacks (they hide call timing and effects), weak references, cycle collection.
