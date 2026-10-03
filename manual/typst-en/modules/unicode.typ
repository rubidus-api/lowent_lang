#import "../../typst-ko/lib.typ": *

= `unicode` --- Unicode property tables <mod-unicode>

#modhead(file: "lib/unicode.low", layer: [L0 --- pure computation], caps: [none])

This is where you ask "is this character a letter, a number, a space?". With ASCII only, you could answer by hand: `ge c 97` and `le c 122` means lowercase. The
moment `'한'`, `'あ'` or `'Ω'` arrives, that approach collapses. The code points that are "letters" in Unicode span *658 scattered ranges*, impossible to write by
hand. This module carries those range tables *extracted mechanically from the Unicode 15.1.0 source* and answers by binary search.

```lowent
use unicode .

proc ident_start input cp u64 . output bool . effects none . do
  if unicode.is_letter cp . do return true . end
  return eq cp 95 .
end
```

`is_letter 54620` (`'한'`) is `true` and `is_letter 128512` (😀) is `false` --- emoji are symbols, not letters. *The input is a code point (`u64`), not a byte.*
Getting code points out of UTF-8 byte strings is #modref("utf8")[`utf8`]'s job, and the two are used together.

*Tables are data --- written as code, lies creep in.* Without this module everyone writes letter tests by hand in their own programs, and those tests are almost
always wrong. The problem is how they are wrong. A missed range comes out not as an error but as `false` --- *a silently wrong answer*. A parser cuts a token at
that character, a search misses that word, and no warning appears anywhere. Display width (#modref("term")[`term`]'s `cp_width`) had a usable default of 1 even when
wrong, but here there is no such default.

== Design and boundaries

- *Tables are carried as one fixed-width hex string.* One range is `<start 6 digits><end 6 digits>` = 12 bytes. The position of range `n` is simply `n × 12`, so binary
  search works without decoding. No array to expand, no initialisation code, no allocation, so the whole layer stays `effects none` and is verified inside the
  VM/native cross-check.
- *The range is U+0000 … U+2FFFF.* Above that is still hardly used. *It does not pretend to carry what it does not* --- everything from U+30000 up answers "no property".
- *Subcategories are split when something needs to ask.* Lu, Ll and Nd were split when regex `\p{Lu}` created a place to ask, and the rest followed. All
  subcategories of L and N now exist (L = Lu+Ll+Lt+Lm+Lo, N = Nd+Nl+No). Subcategories of P, Z and M are not split yet.
- *Tables can be re-extracted.* The development repository's extraction script produces the tables and checks every time that *the tables carried match the
  extraction byte for byte*. A table pasted by hand once becomes, from that moment, an unverifiable lump of constants. This check has caught something --- an old
  document claimed `is_space` included tab and newline, and the table did not.
- *Conversion is pairs, not ranges.* Categories ask "is it in this range", conversion asks "*where* does it go". So `tab_toupper` and `tab_tolower` use the same fixed
  width but carry `<from><to>` pairs. *Only one-to-one* mappings are carried, and that limit is not hidden (`is_special_case`).
- *Not built* --- normalisation (NFC, NFD), composition, subcategories of P, Z and M, character names, full case folding (conversions that change length, like ß → ss).
  Each needs yet another table, and tables are not free.

Tables are `slice u8` returned by `fn`s (string literals --- no copy, no allocation), and there is no struct or state. Tables keep four invariants --- a range is 12
lowercase hex bytes, sorted by start ascending, no overlaps (adjacent ranges merged), and *end inclusive* (`start ≤ cp ≤ end`). If you build a table yourself for
`has_cp`, keep sorting and no overlap for binary search to hold.

== Ops at a glance

#dtable(
  columns: 2,
  id: "mod-unicode-ops",
  caption: [Ops of `unicode`],
  [*op*], [*What it does*],
  [`tab_letter` · `tab_number`], [Letter (L) table, 658 ranges · number (N) table, 137 ranges],
  [`tab_punct` · `tab_space` · `tab_mark`], [Punctuation (P) · space (Z, no tab or newline) · mark (M) tables],
  [`tab_zerowidth`], [*Zero width* (Mn, Me, Cf) table, 354 ranges],
  [`tab_upper` · `tab_lower` · `tab_digit`], [Uppercase (Lu) 646 · lowercase (Ll) 658 · decimal digit (Nd) 64 ranges],
  [`tab_title` · `tab_modifier` · `tab_other_letter`], [Lt 10 · Lm 71 · Lo 509 ranges],
  [`tab_numletter` · `tab_numother`], [Nl 12 · No 72 ranges],
  [`tab_toupper` · `tab_tolower`], [Case *pair* tables, 1423 · 1432 pairs],
  [`tab_casespecial`], [103 code points that cannot be converted one-to-one],
  [`range_count`], [Number of ranges in a table (`len tab / 12`)],
  [`has_cp`], [Is this code point in the table --- binary search],
  [`is_letter` · `is_number` · `is_punct` · `is_space` · `is_mark`], [Predicates for each table],
  [`is_alnum`], [Letter or number],
  [`is_upper` · `is_lower` · `is_digit`], [Lu · Ll · *decimal* digit (Nd)?],
  [`is_zerowidth`], [Zero width (combining or format character)?],
  [`is_title` · `is_modifier` · `is_other_letter` · `is_numletter` · `is_numother`], [Lt · Lm · Lo · Nl · No?],
  [`to_upper` · `to_lower`], [To uppercase · lowercase (*unchanged* if no mapping)],
  [`is_special_case`], [A code point that cannot be converted one-to-one (such as ß)?],
)

== Ops in detail

- *`tab_*`* --- no parameters; each returns a read-only view valid for the life of the program. You rarely use them directly --- mainly to run several tables in one loop
  or to see sizes with `range_count`.
- *`range_count tab`* --- `len tab / 12`. Passing a slice that is not a table gives a meaningless number (it does not check).
- *`has_cp tab cp`* --- the core. Cost is O(log n) comparisons, each reading two 6-digit hex numbers. No allocation, no buffer. *If the table is broken* (non-hex bytes), it
  returns `false` --- it invents nothing, which also means it passes a broken table silently.
- *`is_letter` and friends* --- thin wrappers over `has_cp <table> cp`. Any `u64` gets an answer (`false` out of range). `is_alnum` exists separately as the combination
  most used for identifier checks.
- *`is_digit` is narrower than `is_number`.* The Roman numeral `Ⅶ` (U+2166) and the fraction `½` are N but not Nd. Computing digit values (`d = cp − '0'`) requires Nd.
- *Korean is `false` for both `is_upper` and `is_lower`* (Lo --- characters without case). Yet `is_letter` is `true`. "Not uppercase" does not mean "not a letter".
- *`is_zerowidth`* --- `true` for Mn (nonspacing marks), Me (enclosing marks) and Cf (format characters). These code points take no cell of their own; they sit on the
  previous character. #modref("term")[`term`]'s width computation and cluster counting come from *the same table*, so "width right but cursor movement wrong" cannot
  happen. `is_mark` is the whole M category (including Mc, which does take a cell), so use `is_zerowidth` to count width.
- *`to_upper` · `to_lower`* --- if nothing changes they return the input as is (not `none`). Most code points in the world have no case, and making callers unwrap an
  `option` every time would fill call sites with `some_value`. *Only one-to-one* --- the uppercase of German `ß` (U+00DF) is the two letters `"SS"`, which cannot be written
  as a pair, so `to_upper 223` returns 223 as is. `is_special_case` names such code points separately. Round trips do not always hold either (Turkish `ı`, Greek final
  sigma). Case-insensitive comparison is folding, which does not exist yet.

== Using it

Counting words (runs of letters and numbers) in a UTF-8 string:

```lowent
module wordcount .

use unicode .
use utf8 .

export proc count_words input s slice u8 . output option u64 . effects none . do
  var off be u64 0 .
  var words be u64 0 .
  var inword be bool false .
  while lt off (len s) . do
    let cp be option u64 utf8.decode s off .
    guard is_some cp . else return none .
    let n be u64 utf8.seq_len (idx s off) .
    guard gt n 0 . else return none .
    if unicode.is_alnum (some_value cp) . do
      if eq inword false . do set words (add words 1) . end
      set inword true .
    end
    if eq (unicode.is_alnum (some_value cp)) false . do
      set inword false .
    end
    set off (add off n) .
  end
  return some words .
end
```

`"한글 word 123"` gives 3 --- a test that knows only ASCII cannot count `'한글'`. Regex `\p{L}` and `\P{L}` use exactly these tables (#modref("regex")[`regex`]). If a
regular expression is enough, use it; where only one code point needs asking, calling this module directly is much cheaper.

== Counter-examples

#antipattern[Passing a byte as is][
  ```lowent
  rem ✘ index gives a byte. The first byte of '한' is 0xED
  if unicode.is_letter (widen u64 (idx s 0)) . do … end
  ```
  0xED (237) is the code point U+00ED (í) --- judged a letter by chance, but *not the character being asked about*. When handling UTF-8, always go through `utf8.decode`.
]

#antipattern[Using `is_mark` while counting width][
  It counts Mc (combining marks that take a cell) as 0 too. Width needs `is_zerowidth` --- and `term`'s `cp_width` already handles width anyway.
]

#antipattern[Assuming `to_upper` always converts][
  `ß` comes back unchanged. If precision matters, ask `is_special_case` first; expanding to two or more characters is the caller's decision.
]

#antipattern[Computing digit values with `is_number`][
  ```lowent
  rem ✘ Ⅶ (U+2166) passes too, and cp − 48 is meaningless
  if unicode.is_number cp . do set v (add (mul v 10) (sub cp 48)) . end
  ```
  To accept decimal digits only, use `is_digit`.
]

#antipattern[Treating `false` from `is_upper` as lowercase][
  Korean, Chinese and Arabic characters are `false` for both. That test classifies most of the world's characters as lowercase. Ask `is_lower` directly.
]

== Cautions

- *Out of range is silently `false`.* A program handling U+30000 and above must know this module cannot answer.
- *`false` from `has_cp` does not distinguish "not in it" from "the table is broken".* Built-in tables are always intact. If you build a table, check that
  `mod (len tab) 12` is 0 before passing it.
- *Tables follow Unicode 15.1.0.* When Unicode updates, re-extract the tables rather than editing them by hand.
- *Binary size.* All tables go in as strings (tens of KB). If that matters on a small machine, a module carrying only the needed tables is the right answer.
- *`is_space` is exactly category Z --- tab, newline and CR are not in it.* In Unicode those are control characters (Cc). If you want regex `\s` behaviour, add them at
  the call site. They were not mixed in because this table is used as is for `\p{Z}`.
