#import "../../typst-ko/lib.typ": *

= `stdio` --- standard input and output <mod-stdio>

#modhead(file: "lib/stdio.low", layer: [L2 --- host (wrappers over builtin ops)], caps: [both ops take `cap io`])

Two ops that reach the three channels a process is born with: 0 (input), 1 (output) and 2 (error). Writing to the screen and reading input cannot be
written in the language, so the compiler offers them as *builtin ops* (`call_builtin write_out`, `call_builtin read_in`) and a program calls the ops
of this module. A wrapper passes its arguments through unchanged; in native code the C compiler inlines it, so it costs nothing.

```lowent
use stdio .

proc main input out cap io . output u8 . effects io . do
  let n usize stdio.write out. 1 "hello\n" . .
  return 0 .
end .
```

#dtable(
  columns: 3,
  id: "mod-stdio-ops",
  caption: [The ops of `stdio`],
  [*op*], [*shape*], [*notes*],
  [`write_out`], [`(cap io, d u64, b slice u8) → usize`], [Writes to channel `d`. Returns *the number of bytes written*: on a pipe it may be less than `len b. .`, and 0 means "cannot write any more"],
  [`read_in`], [`(cap io, d u64, dst mut slice u8) → option usize`], [Reads from channel `d`. 0 is the end of input; `none` means the read failed],
)

Writing to the end (`write_all`) and buffered writing live in #modref("outbuf")[`outbuf`]; cutting the bytes into lines lives in #modref("io")[`io`].

*Not built*: formatted output (#modref("fmt")[`fmt`] does the assembling), line-at-a-time reading, closing a channel.
