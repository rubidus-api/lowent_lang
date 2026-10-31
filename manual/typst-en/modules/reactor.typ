#import "../../typst-ko/lib.typ": *

= `reactor` --- non-blocking reads and writes <mod-reactor>

#modhead(file: "lib/reactor.low", layer: [L2 --- host (wrappers over builtin ops)], caps: [all three ops take `cap io`])

Reads and writes several channels *without blocking*. The state of a reactor lives in backing the caller provides; this module obtains no memory. A handle is
a `u64` and can be made up, so the *capability (`cap io`) shows at every call*. The module is three wrappers that pass the builtin ops `reactor_new`,
`r_read` and `r_write` through.

```lowent
use reactor .

proc echo_once input k cap io . input buf mut slice u8 . output usize . effects io . do
  let r option u64 reactor.new k. subslice buf. 0 32 . 1 . .
  guard is_some r. . else return 0 . .
  let n option usize reactor.read k. some_value r. . 0 subslice buf. 32 64 . . .
  guard is_some n. . else return 0 . .
  return some_value n. . .
end .
```

#dtable(
  columns: 3,
  id: "mod-reactor-ops",
  caption: [The ops of `reactor`],
  [*op*], [*shape*], [*notes*],
  [`reactor_new`], [`(cap io, backing mut slice u8, n u64) → option u64`], [Makes a reactor. `backing` is where its state lives; `n` is how many channels it handles at once],
  [`r_read`], [`(cap io, r u64, d u64, dst mut slice u8) → option u64`], [Reads from channel `d`. 0 when nothing is there yet; `none` when the read failed],
  [`r_write`], [`(cap io, r u64, d u64, b slice u8) → option u64`], [Writes to channel `d`. Returns the bytes written; `none` when the write failed],
)

*Not built*: timers, ops to register and remove a channel, an op that waits on several channels at once.
