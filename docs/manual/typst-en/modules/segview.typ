#import "../../typst-ko/lib.typ": *

= `segview` --- cursors, total length and coalescing for segment views <mod-segview>

#modhead(file: "lib/segview.low", layer: [L1 --- the caller's storage], caps: [none])

The language holds only three segment view operations --- `view_segments` (build), `segs` (segment count), `seg` (the i-th segment). This module adds *cursors, total length
and coalescing* on top. All three are written with those three, so there is no reason for them to be builtins.

*Why `segments` is in the language.* Without holding several segments as one value, sending them out takes one call per segment. Sending 64 KiB as 64 segments differed by
28× in the development repository's measurement (one `writev` versus 64 `write`s). A segment is an `iovec` --- a slice in emitted C has the same size and both offsets as
`struct iovec`, so passing a segment array across an `extern` boundary is a cast, not a copy (a test compares it every run).

```lowent
rem the descriptor is (at, n) pairs in a flat slice u64 --- no new struct type
set (index d 0) 0 .  set (index d 1) 4 .
set (index d 2) 8 .  set (index d 3) 4 .
let ss be view_segments back d .
let n u64 be segview.total_len ss .
let w option u64 . be segview.coalesce ss out .
```

#dtable(
  columns: 3,
  id: "mod-segview-ops",
  caption: [The cost is written in the op name],
  [*Call*], [*What it touches*], [*Cost*],
  [`segs` · `seg` (builtin)], [descriptor only], [a few ns or less per segment, zero copies],
  [`segview.total_len`], [descriptor only], [O(segments)],
  [`segview.cursor` · `seek` · `byte_at`], [descriptor only], [O(segments) --- scans segments to find the position],
  [`segview.coalesce`], [*every byte*], [O(n) --- the only place copying happens],
)

That is the point of the design --- traversal only borrows segments, and the cost of flattening into one piece is paid only when called. The cost is visible at the call
site.

*Things to know.* Storage belongs to the caller --- `coalesce` writes into the given buffer and answers *the length written*; if the buffer is short it does not truncate but
answers `none`. No cursor pointing to a nonexistent position is made --- `seek` past the end is `none`.

*One name hurt other modules.* `total_len` was first called `total`. Then the library pair tests caught breakage when `segview` was used together with `tls13` or `tlssrv`
--- it collided with a local variable `total` inside `tls13` and produced an unrelated diagnostic (`E-GUARD-FALLTHROUGH`). Common words are not exported. (The diagnostic
not reporting the name collision as a name collision is a separate remaining defect.)

*Not built* --- variable element width (T of `segments T` is a scalar slice today), zero-copy optimisation of `coalesce`, the read-side (`readv`) counterpart.
