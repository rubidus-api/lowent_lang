#import "../../typst-ko/lib.typ": *

= `io` --- stream reading over slices <mod-io>

#modhead(file: "lib/io.low", layer: [L2 --- the pure half of the outside world], caps: [none --- only the side filling the bytes needs capabilities])

Reads bytes already in memory *like a stream*, a little at a time. Used to scan a fully read file line by line, or when a parser needs a cursor. The reading surface is not
`read(buf) → n` but *peek · take · toss → views* --- instead of filling caller memory, it hands out borrowed views pointing into the original (zero copies).

```lowent
var r be io.mem_reader spawn actor io.mem_reader .
let z be u64 send r attach src .
let line be option slice u8 send r take_line 4096 .
```

*Why no capability is needed.* This module only cuts bytes already in memory and never reaches the kernel. Only the side actually filling bytes (`read_in` needs `cap io`,
`files.read` needs `cap file_system`) requires a capability. So all framing logic (buffer management, `take_line`) is verified by VM/native comparison before touching the
kernel, and kernel reading stays a thin leaf feeding bytes into that verified logic (#chref("io-files")).

#aside[`none` is not failure][
  `none` from `peek`, `take`, `take_until` and `take_line` is *absence* --- "the requested bytes are not there". `mem_reader` is pure arithmetic with nowhere to fail. So when
  #modref("files")[`files`] moved to the three-way `result (option T) file_error`, this module did not follow --- adding `result` here would make every caller write an `error`
  branch that can never happen. The three-way answer is needed by a reader of raw fds, which does not exist yet.
]

#dtable(
  columns: 3,
  id: "mod-io-ops",
  caption: [Ops of `io`],
  [*op*], [*Shape · effects*], [*Failure*],
  [`scan_until`], [`fn (src slice u8, from u64, delim u8, limit u64) → option u64`], [`none` if not within limit or at the end],
  [`mem_reader`], [actor --- state `src slice u8` · `pos u64`], [---],
  [`attach s`], [`→ u64` (always 0), state --- attaches the source, cursor 0. Calling again restarts], [none],
  [`remaining`], [`→ u64`, none], [none],
  [`peek n`], [`→ option slice u8`, none --- looks without consuming], [`none` if fewer than n remain],
  [`take n`], [`→ option slice u8`, state], [`none` if short, cursor unchanged],
  [`take_rest`], [`→ slice u8`, state], [none (an empty view at the end)],
  [`toss n`], [`→ u64` (bytes actually dropped), state], [none (drops only up to the end)],
  [`take_until delim limit`], [`→ option slice u8`, state --- consumes the delimiter but excludes it from the view], [`none` if not found, cursor unchanged],
  [`take_line limit`], [`→ option slice u8`, state --- `take_until 10 limit`], [`none` if no LF within limit],
)

*Design.* EOF is `none`, not an error. *The limit is a required argument* --- no default, so hostile input cannot trigger unbounded reads, and only the caller who knows the data
knows how long a line may legitimately be. *No partial consumption* --- on `none` the cursor does not move, so retrying with another length is safe. *LF is the only newline*
--- CR stays in the content. The library does not silently delete bytes (deletion cannot be undone; not deleting lets the caller choose). Positioning is the pure primitive
`scan_until`, and the stateful cursor (actor) is a thin shell over it --- to hold the cursor as a value (reentrancy, backtracking), use `scan_until` with a caller-held pos.

```lowent
proc count_lines input src slice u8 . output u64 . effects state . do
  var r be io.mem_reader spawn actor io.mem_reader .
  let z be u64 send r attach src .
  var lines be u64 0 .
  var going be bool true .
  while going . do
    let l be option slice u8 send r take_line 4096 .
    if is_some l . do set lines (add lines 1) . end
    if eq (is_some l) false . do set going false . end
  end
  if gt (send r remaining) 0 . do set lines (add lines 1) . end
  return lines .
end
```

`count_lines` knows neither files nor stdin --- `main` fills bytes with one capability-holding line (`read_in out 0 buf`) and passes them via `subslice`. A final piece without
a newline is not given by `take_line`, so `remaining` checks for it.

#antipattern[Treating `take_line`'s `none` as EOF][
  `none` means (a) already at the end or (b) no LF within the limit. Not collecting the tail silently drops a final line without newline, and an unusually long line makes it look
  as if nothing is read from then on. Tell them apart with `remaining`.
]

#antipattern[Comparing CRLF input views as is][
  `take_line` does not strip CR, so the view is `"data\r"`. It looks identical but `eq` is always `false` (one byte longer). Strip it with `strings.remove_suffix line "\r"` before
  comparing.
]

#antipattern[Forgetting `attach` · choosing loop continuation with `guard`][
  Spawning without `attach` is not a compile error but makes reads return `none` at once --- zero lines from real input means check `attach` first. `guard` must leave, so using it
  to choose continuation is `E-GUARD-FALLTHROUGH` --- choose with `if`.
]

*Cautions.* Do not hold views long --- slices from `take_line` are not copies, so refilling the original buffer changes what they point to. Copy values you will keep. Ops
calling actor handlers must declare `state` too. Why the attaching handler is `attach`, not `open` --- it collided with `files`' `open`, and effect resolution picked the wrong
one. Name things so they do not collide with modules used alongside.
