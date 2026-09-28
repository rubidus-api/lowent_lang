#import "../../typst-ko/lib.typ": *

= `fmt` --- formatting assembled into the caller's buffer <mod-fmt>

#modhead(file: "lib/fmt.low", layer: [L0 --- pure computation (the caller's buffer)], caps: [none])

Prints numbers and text *into a buffer the caller provides* to make strings. Use it to assemble a line to output or one line of a log. This module makes no
paper; it only writes on the paper handed to it (`mut slice u8`).

```lowent
use fmt .

let p be option u64 fmt.put_u64 buf 0 1234 .
guard is_some p . else return 1 .
```

`0` is `pos` (the next place to write), and the `option u64` returned is *the new `pos` on success, `none` if room ran out*.

*Formatting is not output.* Every op only assembles bytes into the caller's buffer and, without exception, is `effects none`. Actual output is done separately
by a caller holding `cap io`, with `write_out` or #modref("outbuf")[`outbuf`]. So assembly code can be called from anywhere without capabilities, tests compare
buffers rather than screens, and the caller decides what to emit and when. Turning numbers into bytes needs neither kernel nor allocation, so it is a library ---
no builtin was added, and the whole file is inside the VM/native cross-check.

== Design and boundaries

- *There is one convention --- every writer is `(buf, pos) → new pos`.* Success gives `some newpos`, shortage gives `none`. It refuses with a value rather than
  stopping. Give the new `pos` to the next call as it is and the text joins up.
- *All or nothing.* If room runs out, not a byte is written --- if "12" remains instead of "1234", nobody notices. That is why `put_u64` and `put_hex` count the
  digits first (`dec_width`, `hex_width`) and write from the front.
- *A line break is one LF* (`put_nl`). It is the same convention as `take_line` in #modref("io")[`io`], and CRLF is neither made nor removed.
- *What it does not do* --- floating-point formatting, padding and alignment, locale (thousands separators). If needed, measure the width with `dec_width` and fill
  spaces yourself with `put_byte`.
- *No state.* No struct, no enum. The buffer and cursor both belong to the caller, so any number of uses anywhere at once do not interfere.

== Ops at a glance

#dtable(
  columns: 3,
  id: "mod-fmt-ops",
  caption: [Ops of `fmt` --- all `effects none`],
  [*op*], [*Signature*], [*When it cannot*],
  [`put_byte`], [`proc (buf mut slice u8, pos u64, b u8) → option u64`], [`none` if no room, buffer unchanged],
  [`put_str`], [`proc (buf, pos, s slice u8) → option u64`], [`none` unless all fits, buffer unchanged],
  [`dec_width`], [`fn (n u64) → u64`], [never fails],
  [`put_u64`], [`proc (buf, pos, n u64) → option u64`], [`none` if no room, buffer unchanged],
  [`put_i64`], [`proc (buf, pos, n i64) → option u64`], [`none` if no room],
  [`hex_width`], [`fn (n u64) → u64`], [never fails],
  [`put_hex`], [`proc (buf, pos, n u64) → option u64`], [`none` if no room, buffer unchanged],
  [`put_bool`], [`proc (buf, pos, b bool) → option u64`], [`none` if no room],
  [`put_nl`], [`proc (buf, pos) → option u64`], [`none` if no room],
)

`put_` in a name means "write into the buffer" and `_width` means "only count how many bytes are needed". Those that change the buffer are `proc`, those that
only count are `fn`, but none of the nine touches the outside.

== Ops in detail

Every writer's first two parameters are the same. `buf` is *where* to write (received because the module does not allocate, `mut` because it changes it), and
`pos` is *from which place* in it (the module does not remember the cursor, so passing the new `pos` from the previous call is the only way to say "continue").

- *`put_byte`* --- writes the single byte `b` to `buf[pos]` (`'-'` is 45, LF is 10). Success gives `some (pos+1)`.
- *`put_str`* --- writes the whole byte string `s`. `s` is only read, so a string literal can be given as is. `none` if `pos + len s` exceeds `len buf`.
- *`dec_width` · `hex_width`* --- the number of digits when writing `n` in decimal or hex. At least 1 (`0` is one digit). They precompute for all-or-nothing, but
  you may use them directly to measure in advance "how many bytes are enough".
- *`put_u64` · `put_hex`* --- unsigned decimal and hex (lowercase, no `0x`). If you need `0x`, put `put_str buf pos "0x"` in front.
- *`put_i64`* --- signed decimal. If negative, writes `-` first and handles the magnitude in `u64`. The i64 minimum (−9223372036854775808) cannot be negated to a
  positive, so `0 − n` is computed in `u64`. This boundary value prints correctly too.
- *`put_bool`* --- writes `"true"` or `"false"`. Not `"1"`/`"0"`, so 4 or 5 bytes are needed.
- *`put_nl`* --- one LF byte. Same as `put_byte buf pos 10`.

== Using it

Assemble purely, output once at the end with `cap io`. The key is *passing the returned new `pos` on to the next call*.

```lowent
module report .

use fmt .

proc main
  input out cap io .
  input al  cap allocator .
  output u8 .
  effects alloc io .
do
  let g be option mut slice u8 alloc_bytes al capacity 64 .
  guard is_some g . else return 70 .
  let buf be mut slice u8 some_value g .

  rem assembly --- not a byte has gone out yet
  let p1 be option u64 fmt.put_str buf 0 "answer=" .
  guard is_some p1 . else return 71 .
  let p2 be option u64 fmt.put_u64 buf (some_value p1) 42 .
  guard is_some p2 . else return 72 .
  let p3 be option u64 fmt.put_str buf (some_value p2) " hex=" .
  guard is_some p3 . else return 73 .
  let p4 be option u64 fmt.put_hex buf (some_value p3) 255 .
  guard is_some p4 . else return 74 .
  let p5 be option u64 fmt.put_nl buf (some_value p4) .
  guard is_some p5 . else return 75 .

  rem output --- only the assembled front part (0 … p5) goes out
  let w be u64 write_out out 1 (subslice buf 0 (some_value p5)) .
  return 0 .
end
```

The output is `answer=42 hex=ff` and one line break. Giving each piece a different exit code tells at once where room ran out. Measuring the buffer size in
advance works with the same tools.

```lowent
fn need_for input n u64 . output u64 . do
  let w be u64 fmt.dec_width n .
  return add (add 7 w) 1 .
end
```

== Counter-examples

#antipattern[Outputting inside an `effects none` op][
  ```lowent
  proc bad input out cap io . output u64 . effects none . do
    return write_out out 1 "x" .
  end
  ```
  The declaration becomes a lie and is rejected with `E-EFFECT` (`E-EFFECT-CALC` for a `fn`). Even changed to `effects io`, it is `E-EFFECT-NO-CAP` without a
  `cap io` input.
]

#antipattern[Discarding the returned `pos` and reusing the old one][
  ```lowent
  let p1 be option u64 fmt.put_str buf 0 "answer=" .
  let p2 be option u64 fmt.put_u64 buf 0 42 .        rem ✗ pos should be some_value p1
  ```
  It translates, and the result overlaps like `42swer=` instead of `answer=42`. If the front of the output is mangled, first check whether `pos` was passed on.
]

#antipattern[Carrying on ignoring `none`][
  If room runs out, that piece *was never written*. Carrying on without `guard is_some … else return …` sends the output with the middle piece missing. If output
  goes wrong only for long values, the buffer is short. All or nothing guarantees one op, not a whole sequence of calls. Conversely, taking out with `some_value`
  without `guard` stops with `E-VM-NONE` the moment room runs out --- testing only with short values never reveals it.
]

#antipattern[Giving the whole buffer to `write_out`][
  Giving `buf` instead of `subslice buf 0 (some_value p5)` sends the unwritten back part too. Mysterious bytes stick to the end of the line. The last `pos` is *the
  length written*.
]

== Cautions

- *Running short is a normal path.* It is an expected answer, not an error, so receive it with `guard`. If you need an upper bound, measure in advance ---
  `u64` decimal is at most 20 digits, hex at most 16, and `i64` adds one for the sign.
- *`pos` is both an index and a length.* After assembly, `pos` equals the bytes written so far, so `subslice buf 0 pos` is the result.
- *To reuse the buffer, just reset `pos` to 0.* Old content need not be cleared --- emit only up to the current `pos` and the old bytes are not seen.
- *Write CRLF yourself.* If a protocol needs it, write `put_str buf pos "\r\n"`.
- *Every op is `effects none`.* They can be called inside a `fn` or an actor handler. Only output needs `cap io` and `effects io`.
- *Prefixes, signs and padding are the caller's job.* Attach `0x`, `+`, thousands commas or filler spaces yourself with `put_str` and `put_byte`.
