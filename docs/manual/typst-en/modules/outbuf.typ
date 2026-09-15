#import "../../typst-ko/lib.typ": *

= `outbuf` --- buffered output where forgetting to flush is a compile error <mod-outbuf>

#modhead(file: "lib/out.low", layer: [L2 --- the outside world], caps: [`cap io` for `flush` · `write` · `finish`])

Output is *gathered and sent at once*. `write_out` flushes on every call, so calling it per piece costs a system call per piece --- this writer accumulates bytes in a caller
buffer and flushes only when full, reducing system calls from the number of bytes to the number of buffers. And it forces a final `finish`, preventing "ended without sending
what was gathered" *at compile time*.

```lowent
var p owned outbuf.pending be outbuf.open 1 .
let w result (owned outbuf.pending) outbuf.io_error . be outbuf.write out p buf "hi\n" .
guard is_ok w . else return 1 .
set p (ok_value w) .
let r result void outbuf.io_error . be outbuf.finish out p buf .
```

*What is owned is not the buffer but the bytes not yet sent.* The buffer stays the caller's (the same discipline as #modref("fmt")[`fmt`]), and `owned` guards the unflushed
state `pending` --- exactly what must not be lost is what is owned. `finish` takes `owned pending` and returns a `result`, so silently dropping such a value is `E-OWN-INCOMPLETE`
(#chref("ownership")). This is inferred from signatures, not annotations. Elsewhere, forgetting to flush loses the output entirely, or a destructor flushes and discards the
error --- here it does not compile.

*The writer does not hold `cap io`.* A capability is a static token, not a value, and cannot be stored in a struct. So the caller supplies it at every point bytes actually
leave --- *the writer cannot flush behind your back* (#chref("capabilities")).

#dtable(
  columns: 3,
  id: "mod-outbuf-ops",
  caption: [Ops of `outbuf`],
  [*op*], [*Shape*], [*Failure*],
  [`io_error` · `pending`], [enum `write_failed` · struct `pos u64` (bytes accumulated) · `fd u64` (1 = stdout, 2 = stderr)], [---],
  [`open`], [`fn (d u64) → pending`, effects none], [none --- but opening creates a debt],
  [`flush`], [`proc (out cap io, p owned pending, buf mut slice u8) → result (owned pending) io_error`], [`error write_failed`],
  [`write`], [`proc (out cap io, p owned pending, buf mut slice u8, s slice u8) → result (owned pending) io_error` --- flushes itself when full], [`error write_failed`],
  [`finish`], [`proc (out cap io, p owned pending, buf mut slice u8) → result void io_error` --- flushes the tail and completes], [`error write_failed`],
)

`write` and `flush` consume ownership and return the new state in `ok` --- the caller takes it over with `set p (ok_value w)` every time. `set` is reinitialisation, not use, so
ownership holds inside loops. *A partial write is failure* --- if the count written differs from the request it is `write_failed`, the pending is consumed and bytes in the buffer
at that moment are lost (retry was not built). A small buffer still behaves correctly; size only affects speed. Call `flush` directly only when "this line must appear on screen
now".

```lowent
module emit .

use fmt .
use outbuf .

proc main input out cap io . input al cap allocator . output u8 . effects alloc io . do
  let g option mut slice u8 . . be alloc_bytes al capacity 16 .
  guard is_some g . else return 70 .
  let buf mut slice u8 . be some_value g .
  let ng option mut slice u8 . . be alloc_bytes al capacity 32 .
  guard is_some ng . else return 71 .
  let nb mut slice u8 . be some_value ng .
  var p owned outbuf.pending be outbuf.open 1 .
  var i u64 be 1 .
  while le i 5 . do
    let a option u64 . be fmt.put_str nb 0 "line " .
    guard is_some a . else return 72 .
    let b option u64 . be fmt.put_u64 nb (some_value a) i .
    guard is_some b . else return 73 .
    let c option u64 . be fmt.put_nl nb (some_value b) .
    guard is_some c . else return 74 .
    let w result (owned outbuf.pending) outbuf.io_error .
      be outbuf.write out p buf (subslice nb 0 (some_value c)) .
    guard is_ok w . else return 75 .
    set p (ok_value w) .
    set i (add i 1) .
  end
  let f result void outbuf.io_error . be outbuf.finish out p buf .
  guard is_ok f . else return 76 .
  return 0 .
end
```

Pure formatting (`fmt`) assembles bytes and buffered output (`outbuf`) sends them. The assembly buffer `nb` and the writer's buffer `buf` are separate.

#antipattern[Omitting `finish` · reusing a moved `p`][
  Returning without completing an `owned pending` is `E-OWN-INCOMPLETE` --- including paths leaving early with `return`. Passing the old `p` after `write` without `set p …` is a
  use of a moved value, a compile error. The compiler catches both.
]

#antipattern[Using the buffer for something else between `write` and `finish`][
  `pending.pos` counts bytes accumulated in that buffer. Sharing it with the assembly buffer compiles, but unsent bytes get overwritten and the wrong content goes out. It is the
  only silently wrong mistake in this module --- dedicate one buffer to one writer.
]

*Cautions.* Flushing without a capability is a compile error (declaring `effects io` without receiving a capability is `E-EFFECT-NO-CAP`). The fd is an integer not checked for
openness. If the whole output is already in one buffer, no writer is needed --- one `write_out` does it. Vectored writes (writev), retry after a failed flush and interruptible
output were not built.
