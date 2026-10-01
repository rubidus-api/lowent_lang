#import "../../typst-ko/lib.typ": *

= `codec` --- hex and base64 <mod-codec>

#modhead(file: "lib/codec.low", layer: [L0 --- pure computation (the caller's buffer)], caps: [none])

Turns bytes *into hex or base64 text and back*. Many places let only text through --- a line of a configuration file, a log, a URL, a mail body, screen output.
Putting raw bytes there lets zero and control bytes break the channel. Hex (one byte as two characters `0-9 a-f`) and base64 (arbitrary bytes as 64 characters
`A-Z a-z 0-9 + /`) are *wrappings for carrying arbitrary bytes using only safe characters*. Hex is easy to read but exactly doubles the size; base64 is hard to read
but ends at about 4/3. Hex is usual for eyeballing hashes, base64 for carrying chunks.

```lowent
use codec as c .

let n be option u64 c.hex_enc "abc" dst .
guard is_some n . else return 1 .
```

`dst` is not made by this module but is *an output place the caller sets aside in advance* (at least 6 bytes here). `n` is *the number of bytes actually written*, and
the result is `dst[0..n)`.

A wrapping keeps no secrets --- anyone can undo it. If secrecy is needed, use #modref("aead")[`aead`], which seals (#chref("lib-text")). This module is pure computation
from bytes to bytes, using only `idx`, `set`, `shl`, `shr`, `bit_and`, `bit_or`, `guard` and `while`, and no builtin was added.

== Design and boundaries

- *Invalid input is answered with a value --- one kind, `none`.* Odd-length hex, non-hex characters, non-base64 characters, misplaced padding and *a too-small output
  buffer* all give `none`. It neither stops nor truncates --- truncating would put every layer after on the false premise "all written". Success is `some <bytes written>`,
  and the caller gets the exact result with `subslice`.
- *Hex encoding emits lowercase.* Decoding accepts both cases --- liberal in what it accepts, conservative in what it sends.
- *base64 uses the standard alphabet* (`A–Z a–z 0–9 + /`) and `=` padding. base64 writes input 3 bytes at a time as 4 characters, and pads a short last group with `=`
  so the length is always a multiple of 4.
- *Not built* --- the URL-safe variant (`-_`) and line wrapping. If needed they will be built as *separate ops* --- a mode argument on one op is itself confusion.
- *The output buffer belongs to the caller.* Only the caller knows the cost and policy of providing a buffer. So it is `effects none` with no hidden allocation.

#dtable(
  columns: 3,
  id: "mod-codec-sizes",
  caption: [Output room needed],
  [*op*], [*`dst` size needed*], [*Success returns*],
  [`hex_enc`], [`2 × len src`], [`some (2 × len src)`],
  [`hex_dec`], [`len src / 2`], [`some (len src / 2)`],
  [`b64_enc`], [`4 × ⌈len src / 3⌉`], [`some (4 × ⌈len src / 3⌉)`],
  [`b64_dec`], [`3 × (len src / 4) − padding count`], [`some <bytes written>`],
)

If `dst` is smaller than the formula the result is always `none`; if larger it does not matter --- it writes from the front and returns the count. Empty input
succeeds (`hex_enc ""`, `b64_enc ""` and `b64_dec ""` all give `some 0`).

== Ops at a glance

#dtable(
  columns: 3,
  id: "mod-codec-ops",
  caption: [Ops of `codec`],
  [*op*], [*Signature*], [*When it cannot*],
  [`hex_digit`], [`fn (v u64) → u8`], [never fails (assumes 0 … 15)],
  [`hex_val`], [`fn (c u8) → option u64`], [`none` outside `0-9 a-f A-F`],
  [`hex_enc`], [`proc (src slice u8, dst mut slice u8) → option u64`], [`none` if `dst` too small],
  [`hex_dec`], [`proc (src slice u8, dst mut slice u8) → option u64`], [`none` for odd length · non-hex characters · `dst` too small],
  [`b64_digit`], [`fn (v u64) → u8`], [never fails (assumes 0 … 63)],
  [`b64_val`], [`fn (c u8) → option u64`], [`none` outside the alphabet (`=` too)],
  [`b64_enc`], [`proc (src slice u8, dst mut slice u8) → option u64`], [`none` if `dst` too small],
  [`b64_dec`], [`proc (src slice u8, dst mut slice u8) → option u64`], [`none` if length not a multiple of 4 · outside the alphabet · misplaced padding · `dst` too small],
)

Usually only `hex_enc`, `hex_dec`, `b64_enc` and `b64_dec` are used; the other four are single-character parts.

== Ops in detail

Every conversion takes `src` (bytes to read) and `dst` (place to write) separately. There is no in-place conversion because encoding grows the result, and overwriting the
original would trample bytes not yet read. *The length of `src` is the whole input* --- no end marker is sought, only `len src` is trusted, so when passing a result held in
a larger buffer back in, cut it exactly with `subslice`. The result length is not the length of `dst` but the `n` in the returned `some n`.

- *`hex_digit v`* --- a nibble (0 … 15) as a lowercase hex character. `'0'+v` if `v < 10`, otherwise `'a'+(v−10)`. It does not check the range; the caller guarantees it.
- *`hex_val c`* --- the value of one hex character. Accepts both cases; `none` otherwise.
- *`hex_enc src dst`* --- first checks `2×len src ≤ len dst`, and if short writes nothing and gives `none`.
- *`hex_dec src dst`* --- needs even length, all characters hex, and `len src / 2 ≤ len dst`. A non-hex character is found *while scanning*, so bytes before it may already
  be written to `dst`. On `none`, treat the content of `dst` as undefined.
- *`b64_digit v`* --- the `v`-th (0 … 63) character of the standard alphabet. 0 … 25 are `A-Z`, 26 … 51 `a-z`, 52 … 61 `0-9`, 62 `+`, 63 `/`. base64 maps 6-bit pieces, not
  bytes, to characters.
- *`b64_val c`* --- the value of one base64 character. *`=` is `none` too* --- padding is not a character but structure handled by the decoder body.
- *`b64_enc src dst`* --- writes including `=` padding. A 1-byte tail gives `XX==`, a 2-byte tail `XXX=`.
- *`b64_dec src dst`* --- needs a length that is a multiple of 4, every character in the alphabet or valid padding, and enough `dst`. `=` may appear only in *the last group*,
  and if the third position is `=` the fourth must be too. Success gives the exact length reduced by the padding. On `none` the content of `dst` is undefined.

== Using it

Check buffer sizes first, convert, and cut by the returned count.

```lowent
module ex_codec .

use codec as c .

proc hex_roundtrip input enc mut slice u8 . . input dec mut slice u8 . . output u64 . effects none . do
  guard ge (len enc) 6 . else return 90 .
  guard ge (len dec) 3 . else return 91 .
  let en be option u64 c.hex_enc "abc" enc .
  guard is_some en . else return 1 .
  guard eq (some_value en) 6 . else return 2 .
  rem the count written is the boundary --- passing all of enc makes trailing garbage input
  let dn be option u64 c.hex_dec (subslice enc 0 6) dec .
  guard is_some dn . else return 3 .
  guard eq (some_value dn) 3 . else return 4 .
  guard eq (idx dec 0) 97 . else return 5 .
  rem uppercase is accepted too: "4A" → 74
  let up be option u64 c.hex_dec "4A" dec .
  guard is_some up . else return 6 .
  guard eq (idx dec 0) 74 . else return 7 .
  return 42 .
end

proc b64_roundtrip input enc mut slice u8 . . input dec mut slice u8 . . output u64 . effects none . do
  rem 2 bytes make one group (4 characters) with one = at the end: "aGk="
  guard ge (len enc) 4 . else return 90 .
  guard ge (len dec) 2 . else return 91 .
  let e be option u64 c.b64_enc "hi" enc .
  guard is_some e . else return 1 .
  guard eq (some_value e) 4 . else return 2 .
  let d be option u64 c.b64_dec (subslice enc 0 4) dec .
  guard is_some d . else return 3 .
  guard eq (some_value d) 2 . else return 4 .
  return 42 .
end
```

== Counter-examples

None of these stop; all give `none`. The symptom is always "the result is `none` and `guard` falls to `else`".

#dtable(
  columns: 3,
  id: "mod-codec-bad",
  caption: [Inputs that commonly go wrong],
  [*Input*], [*Result*], [*Usual cause, and `dst`*],
  [`c.hex_dec "abc" dst`], [`none`], [Odd length --- caught by the length check first, `dst` unchanged],
  [`c.hex_dec "zz" dst`], [`none`], [Non-hex character --- found while scanning, so the front of `dst` may be overwritten],
  [`c.hex_enc "abcdefgh" dst3`], [`none`], [16 needed in 3 bytes --- not a byte is written],
  [`c.b64_dec "abc" dst`], [`none`], [Length not a multiple of 4 --- often `enc` passed whole instead of cut with `subslice`],
  [`c.b64_dec "a?cd" dst`], [`none`], [Outside the alphabet --- mostly URL-safe `-`, `_` or newlines mixed in],
  [`c.b64_dec "aG==YWJj" dst`], [`none`], [Padding in the middle --- what you get by simply concatenating two base64 strings],
)

== Cautions

- *If `none` keeps coming, suspect the buffer size first.* The cause of "the code is right but always `none`" is almost always a `dst` smaller than the formula. Allocating
  generously costs nothing.
- *On `none`, do not use `dst` as the result.* Encoders check size first so `dst` is unchanged on failure, but decoders write while scanning, so the front may be
  overwritten. Only `dst[0..n)` of a successful `some n` is the result.
- *Do not discard the returned count and use all of `dst` as the result.* Old bytes left behind mix in.
- *Do not overlap the buffers of encoding and decoding.* If `src` and `dst` overlap, output overwrites input not yet read. Overlap is not checked, and the result is silently
  wrong.
- *Do not handle padding by hand.* Asking `b64_val` about `=` gives `none`. Leave it to `b64_dec`.
- *URL-safe base64 and multi-line base64 are not accepted* --- unsupported, not a defect. The caller strips newlines before passing it in.
- *Linear cost, free reentrancy.* Everything is linear in input length with no state, so calls from many places at once do not interfere.
