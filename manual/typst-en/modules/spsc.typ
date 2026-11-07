#import "../../typst-ko/lib.typ": *

= `spsc` --- lock-free SPSC ring buffer <mod-spsc>

#modhead(file: "lib/spsc.low", layer: [L0 --- pure computation (the caller's storage)], caps: [`cap atomic`])

The simplest way to pass values between threads is a lock. But with exactly one producer and one consumer no lock is needed --- the producer writes only `tail`, the
consumer only `head`, and each only *reads* the other's atomically. That is SPSC (single-producer single-consumer), and almost the only lock-free structure that is honestly
short.

#aside[One producer and one consumer is the contract][
  If two threads `push` or two threads `pop` it is no longer SPSC, and this module cannot prevent it. That is why convenience ops like `peek` or `drain` were not added ---
  the more convenience ops, the more ways to break that contract. In a standard library a small surface is not laziness but a means of keeping the contract. There is no
  MPSC, MPMC or seqlock --- with CAS retries and ABA attached, putting them in the standard would make unverified code standard.
]

*One slot is always left empty.* So the real capacity is `len buf − 1`. In return `head == tail` *always* means "empty" and is never confused with full.

#dtable(
  columns: 3,
  id: "mod-spsc-ops",
  caption: [Ops of `spsc`],
  [*op*], [*What it does*], [*Returns*],
  [`spsc_capacity buf`], [Maximum number it can hold], [`len buf − 1`],
  [`spsc_push k ctl buf v`], [Push (*producer only*)], [1 success · 0 full],
  [`spsc_pop k ctl buf out`], [Pop (*consumer only*), value in `out[0]`], [1 success · 0 empty],
  [`spsc_count_about k ctl buf`], [Current count (an observation)], [count],
)

`ctl` is a `mut slice u64` of length at least 2, with `[0]` = head (consumer) and `[1]` = tail (producer). `k` is `cap atomic` --- the right to touch shared places
atomically. Why `count_about` says `about` --- it may already have changed by the time it is read. If the name does not say so, users trust the value.

*What holds this code up.* Two proofs. First, wrapping an index with `mod … (len buf)` always lands in range --- `lowentc --emit-proof lib/spsc.low` records that the checks
on both ring buffer accesses (`idx`, `index.store`) and on the two index-advancing additions were removed by proof (#chref("proofs-bounds")). Second, write data → publish
index → observe index → read data carries the data under the weak memory model (RC11) --- and the correctness of the algorithm itself (on success the resource moves into the
buffer, on failure it comes back) borrows an existing proof (gpfsl's `circ_buff`). The correspondence that our `spsc_push` matches that proof's code line by line was checked
by a person reading it, not by machine (#chref("proofs-locks")).

*Orderings are exactly as strong as needed.* When the producer reads head it uses `acquire` (published by the consumer), when it reads its own tail `relaxed` (a value I
wrote), when it publishes tail `release` (the preceding data write must be visible first). The consumer side is symmetric. Only two of the four places need synchronisation.
This notation (`order <name>`) was once unreachable because of a parser defect, found and fixed while trying to use this library --- a feature nobody uses might as well not
exist.

*What is checked* --- pop from empty fails, push to full fails, FIFO order, and correctness after wrapping around the ring. Those tests are single-threaded (the VM is
single-threaded). Correctness of memory ordering with two real threads comes from proof, not from running --- the two are not mixed.
