#import "../../typst-ko/lib.typ": *

= `utf16` --- UTF-16 surrogate arithmetic <mod-utf16>

#modhead(file: "lib/utf16.low", layer: [L0 --- pure computation], caps: [none])

Turns UTF-16 surrogate pairs into code points and back. Use it when exchanging values with worlds that hold strings as 16-bit units, such as the Windows API, Java
and JavaScript. `slice u16` is a UTF-16 string, and one element is one unit.

```lowent
use utf16 .

let c be option u64 utf16.decode s 0 .
guard is_some c . else return 1 .
```

`0` is not "the first character" but *unit 0*. Every position in this module is a unit index, and advancing by character is done by `next_start` (two units for a pair).

*Why surrogates exist.* UTF-16 first tried to hold every character as "one character = one 16-bit unit". When Unicode grew beyond 65,536 characters, the value
range U+D800 … U+DFFF was left unused as characters, and two units from that range were combined to write large code points. 1024 high values × 1024 low values =
1,048,576, covering exactly U+10000 … U+10FFFF. So in UTF-16 *reading one unit and reading one character differ*.

*Why it is a library.* `slice u16` and `slice u32` already do reading, writing and indexing. No capability and no expression missing from the language is needed. So it
became a library with no new type, word or builtin. The only real work left is *surrogate arithmetic* and how it fails. There is no UTF-32 library --- one unit of
`slice u32` is a code point, so all that is needed is one line of validation (`cp_valid`).

== Design and boundaries

- *What it does* --- converting between code points and UTF-16 units, and rejecting what is wrong. An unpaired high surrogate (D800 … DBFF not followed by a low), an
  unpaired low surrogate (DC00 … DFFF on its own), and an out-of-range code point (> U+10FFFF) give `none`.
- *It pairs with #modref("utf8")[`utf8`].* `utf8` rejects surrogates inside UTF-8, so surrogates exist *only inside UTF-16 and only as pairs*. The two libraries keep
  that invariant together.
- *What it does not do* --- normalisation (NFC, NFD), case, combining characters, byte order (BOM, big/little endian). `slice u16` is already in machine order.
- *Everything is `effects none`.* `put` is a `proc`, but it writes only into the buffer the caller handed over, not its own state, so it is `effects none` too.

A BMP code point (U+0000 … U+FFFF excluding the surrogate range) is one unit, value as is. U+10000 … U+10FFFF is two units --- make 20 bits with `v = c − 0x10000`
and split into the high unit `hi = 0xD800 + v/1024` and the low unit `lo = 0xDC00 + v%1024`. The reverse arithmetic is `c = 0x10000 + (hi − 0xD800) × 1024 + (lo − 0xDC00)`.

#dtable(
  columns: 3,
  id: "mod-utf16-units",
  caption: [What a single unit is],
  [*Unit value (decimal)*], [*What it is*], [*Meaning*],
  [55296 … 56319], [High surrogate], [The *front* piece of a two-unit character --- a low must follow],
  [56320 … 57343], [Low surrogate], [The *back* piece of a two-unit character --- wrong if alone],
  [Otherwise], [An ordinary BMP unit], [Its value is the code point],
)

The code point upper limit is 1114111 (U+10FFFF). Surrogate values themselves are not code points, and `cp_valid` rejects them.

== Ops at a glance

#dtable(
  columns: 3,
  id: "mod-utf16-ops",
  caption: [Ops of `utf16`],
  [*op*], [*Signature*], [*When it cannot*],
  [`is_high`], [`fn (u u64) → bool`], [never fails],
  [`is_low`], [`fn (u u64) → bool`], [never fails],
  [`cp_valid`], [`fn (c u64) → bool`], [never fails (false is the answer)],
  [`units`], [`fn (c u64) → u64`], [never fails (1 or 2)],
  [`unit_hi`], [`fn (c u64) → option u64`], [`none` for an invalid code point],
  [`unit_lo`], [`fn (c u64) → option u64`], [`none` for an invalid code point or BMP (one unit)],
  [`put`], [`proc (dst mut slice u16, at u64, c u64) → option u64`], [`none` for an invalid code point or no room],
  [`decode`], [`fn (s slice u16, at u64) → option u64`], [`none` if out of range or unpaired],
  [`next_start`], [`fn (s slice u16, at u64) → option u64`], [`none` at the end or at a broken place],
  [`count_chars`], [`fn (s slice u16) → option u64`], [`none` on meeting a broken place],
  [`is_valid`], [`fn (s slice u16) → bool`], [never fails],
)

The most used in practice are `put` (writing) and `decode` + `next_start` (iteration).

== Ops in detail

- *`is_high` · `is_low`* --- is a unit's value a high (D800 … DBFF) or low (DC00 … DFFF) surrogate. A judgement by value range alone.
- *`cp_valid`* --- is it valid as a code point. `false` if `c > 1114111` or in the surrogate range (55296 … 57343). The gate that filters numbers from outside before
  writing.
- *`units`* --- how many units a code point takes. 2 if `c ≥ 65536`, otherwise 1. Use it to size buffers in advance. *It does not check validity.*
- *`unit_hi`* --- the high unit. `some c` for BMP, `some (0xD800 + v/1024)` for two units, `none` if invalid.
- *`unit_lo`* --- the low unit. `none` for BMP --- here meaning "there is only one unit", not an error. `some (0xDC00 + v%1024)` for two units. `none` also for an invalid
  code point.
- *`put`* --- writes the code point into `dst` from `at` and returns *the next position to write* (`some (at+1)` or `some (at+2)`). It is on the same position axis as
  `decode` and `next_start`. If you need how many units were written, `units c` answers. If invalid or `at + units c > len dst`, `none`, and *not a single unit is written*. It
  makes no memory itself, so the place to write comes from outside.
- *`decode`* --- reads one code point at `at` in `s`. An ordinary unit gives its value; a high surrogate checks that the next unit is a low and combines them. `none` if
  `at ≥ len s`, a low comes alone, a high is not followed by a low, or it ends with a high (truncated). A pair is two units, so it takes the whole slice. `at` must be a
  character's *start* unit.
- *`next_start`* --- the position of the next code point. `decode` must succeed; starting at a high surrogate gives `some (at+2)`, otherwise `some (at+1)`. It spares the caller
  counting two units for a pair.
- *`count_chars`* --- walks everything and counts code points. On a broken place it *gives no count* --- better no count than a wrong one. O(units).
- *`is_valid`* --- whether `count_chars` is `some`. Half a pair looks invalid on its own, so validity is not asked in parts.

== Using it

Writing calls `put` and continues from the returned next position. Reading reads values with `decode` and advances with `next_start`. Either way the caller holds the cursor.

```lowent
module ex_utf16 .

use utf16 as u .

proc round_trip input buf mut slice u16 . output u64 . effects none . do
  guard ge (len buf) 4 . else return 90 .
  rem '가' (U+AC00) --- BMP, so one unit. put returns the next position to write
  let a be option u64 u.put buf 0 44032 .
  guard is_some a . else return 1 .
  guard eq (some_value a) 1 . else return 2 .

  rem U+1F4A9 --- outside the BMP, so split into two units (a surrogate pair)
  let b be option u64 u.put buf (some_value a) 128169 .
  guard is_some b . else return 3 .
  guard eq (some_value b) 3 . else return 4 .
  guard eq (idx buf 1) 55357 . else return 5 .
  guard eq (idx buf 2) 56489 . else return 6 .

  rem read it back as one --- decode joins the pair
  let rb be option u64 u.decode buf 1 .
  guard is_some rb . else return 7 .
  guard eq (some_value rb) 128169 . else return 8 .

  rem 'A' + emoji (pair) + '가' = four units, three characters
  set (idx buf 0) 65 .
  set (idx buf 3) 44032 .
  let n be option u64 u.count_chars (subslice buf 0 4) .
  guard is_some n . else return 9 .
  guard eq (some_value n) 3 . else return 10 .
  return 42 .
end
```

The high unit `buf[1]` is 55357 (0xD83D) and the low unit `buf[2]` is 56489 (0xDCA9). Iterate with `next_start`.

```lowent
var i be u64 0 .
while lt i (len s) . do
  let c be option u64 u.decode s i .
  guard is_some c . else return 80 .
  let nx be option u64 u.next s i .
  guard is_some nx . else return 81 .
  set i (some_value nx) .
end
```

+1 for BMP, +2 for a pair --- you do not count. `none` means stop (whether it is the end or breakage is told apart by measuring `is_valid` first).

== Counter-examples

#antipattern[Trying to pass unpaired input through `decode`][
  ```lowent
  set (idx buf 0) 55357 .          rem ✗ after the high 0xD83D
  set (idx buf 1) 65 .             rem   comes 'A', not a low
  let a be option u64 u.decode buf 0 .
  ```
  It is `none`. So is a low surrogate alone (`decode buf 2`, value 56489) or ending with a high surrogate (`decode (subslice buf 0 4) 3`). All three are caught right at
  `guard is_some`. Letting them through silently would make everything after wrong, so *rejection is the value of this library*. Intact pairs still pass.
]

#antipattern[Putting a two-unit character into the last unit][
  `u.put buf (sub (len buf) 1) 128169` is `none` and *that unit is not touched*. No need to worry about half a write.
]

#antipattern[Trying to use a surrogate value as a code point][
  `u.cp_valid 55357` is `false`, so `put`, `unit_hi` and `unit_lo` all give `none`. Taking these `none`s out with `some_value` without checking stops at run time with
  `E-VM-NONE` --- it translates, so `guard` comes first.
]

== Cautions

- *`len s` is a unit count.* Only `count_chars` knows the character count. One emoji takes two units.
- *Do not cut through a pair.* Splitting between high and low with `subslice` makes both pieces invalid. Cut only at positions `next_start` gave.
- *Advance the cursor with `next_start`.* Advancing by 1 reads a pair's low unit as a character start, giving `none` or an inflated count.
- *Costs.* `decode`, `next_start` and `put` are O(1); `count_chars` and `is_valid` are O(units). Do not count in a loop condition every time.
- *`none` from `unit_lo` has two overlapping meanings* (invalid code point · BMP). If you need to tell them apart, ask `cp_valid` first. When combining, `put` is better,
  since its failure collapses into one `none`.
- *Byte serialisation is outside.* Turning *bytes* from a file or network into a `slice u16` is the caller's job.
