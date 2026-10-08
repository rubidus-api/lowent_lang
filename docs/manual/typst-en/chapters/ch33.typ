#import "../../typst-ko/lib.typ": *

= Text and encodings — `strings`, `fmt`, `utf8`, `codec`, `hash`

#chapter-toc()

#prereq(
  ([#chref("slices"), Sequences], [a string is not a type but `slice u8`]),
  ([#chref("lib-map"), A map of the standard library], [the caller's buffer, failure as a value, all or nothing]),
)

#deepqa[
  How did #chref("slices") answer the question "is there no separate string type"?
][
  A string literal is `slice u8`, and how many characters it has or whether it is valid UTF-8 is answered by library ops, not by the type. It is a choice to avoid the
  defects that arise the moment bytes and characters are treated as the same. This chapter tours that library.
]

#why[
  Almost every program cuts, searches and joins strings, turns numbers into text, and moves bytes into hex or base64. In C, null-terminated strings and overflowing
  buffers follow all of these. Lowent's text modules do not allocate, return results as views or into the caller's buffer, and report failure as values. The shared
  conventions of #chref("lib-map") show up here most often, so this is the first group of modules in the tour.
]

#organizer[
  You will learn to handle strings as views with `strings` and to assemble text into the caller's buffer with `fmt`. You will see that `utf8` reports invalid bytes as
  `none` without replacement characters, and what `codec`'s hex and base64 and `hash`'s three kinds of hash (choosing slots, detecting damage, SHA-256) are each for.
  You will also see where the other text modules (`strbuf`, `utf16`, `unicode`, `regex`) fit.
]

#chapter-questions()

== Cut, find, assemble

#demo("examples/ch33/request.low")

- `strings.starts_with`, `strings.remove_prefix` and `strings.find` all take `slice u8`. What `remove_prefix` and `subslice` return is not a new buffer but a *view*
  pointing at the original bytes. There is no copy.
- `fmt.put_str buf pos s` and `fmt.put_u64 buf pos n` write into the caller's buffer `buf` from position `pos`, and give *the next position to write* as an `option`.
  If there is not enough room, not one character is written and it is `none`.
- When assembly is done, `subslice buf 0 end` is written to standard output.

The buffer was obtained with `cap allocator`, so `alloc` shows in the head. `strings` and `fmt` themselves do not allocate. Where the buffer comes from is the caller's
business.

#qa[
  Isn't writing `guard is_some` every time you call `fmt.put_str` tedious?
][
  It is. In exchange, the moment room runs out it stops right there, and half-written output never goes out. To take a generous buffer and reduce repetition, use
  `strbuf`. `strbuf.append` appends to an owned buffer and gives failure as a `result`. If `strings` is reading (views), `strbuf` is writing.
]

== UTF-8 --- no replacement characters

#demo("examples/ch33/chars.low")

- `utf8.count_chars` counts characters. The Korean "안녕" is 6 bytes and 2 characters.
- The truncated `[236,149]` is not valid UTF-8, so it is `none`, and the example returns 999.
- `utf8.decode s 0` gives the one character at position 0 as a code point. 50504 is U+C548, "안".

Many libraries insert a replacement character (U+FFFD) when they meet invalid bytes and carry on. Then the fact that the input was broken is buried in the output.
`utf8` reports `none`. How to handle broken input is decided by the caller.

When exchanging values with the UTF-16 world (Windows APIs, Java, JavaScript), `utf16` joins and splits surrogate pairs over `slice u16`. Unmatched pairs are `none`
too. Tables asking whether something is a letter, digit, space or zero-width are in the `unicode` module. Those tables were extracted mechanically from Unicode data,
because hand-picked judgements silently give `false` instead of an error in ranges they missed.

== Hex and base64

#demo("examples/ch33/hexout.low")

`codec.hex_enc src dst` writes `src` as hex characters into `dst` and gives *the number of bytes actually written*. The result is the first `n` bytes, not all of `dst`.
The six characters "Lowent" become the twelve characters `4c6f77656e74`. Trying to encode 18 characters into 8 bytes of room gives `none` and writes nothing --- all or
nothing. `hex_dec`, `b64_enc` and `b64_dec` have the same shape.

#misconception[Encoding is a kind of encryption][
  Hex and base64 are only ways of *transcribing* bytes as text; they keep no secrets. Anyone can reverse them. When secrecy is needed, use `aead`, which seals
  (#chref("lib-io-net")). `codec` and `aead` are different modules for the same reason as the charter's "separate computation from authority" --- different jobs do not
  get one name.
]

== Hashes --- three questions, three answers

#demo("examples/ch33/codes.low")

The `hash` module gives different hashes to different questions.

#dtable(
  columns: 3,
  id: "text-hash",
  caption: [Questions the `hash` module answers],
  [*Question*], [*op*], [*Hash used*],
  [Which slot of a hash map does it go in], [`of` · `bucket_of` · `bucket_mask`], [FNV-1a --- fast and even],
  [Was it damaged in storage or transit], [`crc` · `crc_ok`], [CRC-32 --- catches accidental bit errors],
  [Did someone change it on purpose], [`sha256` · `sha256_ok`], [SHA-256 --- collisions are hard to find],
)

`slot` puts the key "hello" into slot 11 of 16. `bucket_of` picks the slot with the remainder `mod`, so the result is always smaller than the slot count
(#chref("numbers")), and so the index bounds check of the hash table is removed. Using a slot-choosing hash to prevent tampering, or a cryptographic hash to choose hash
map slots, are both the wrong tool. The names separate the questions.

== Regular expressions --- no backtracking

`regex` compiles a pattern once into a program (`slice u64`) and matches many inputs with the same program. The compiled result and the workspace are both the caller's
slices, so there is no allocation.

Common regex engines *backtrack* --- at a fork they go down one path to the end and come back when stuck --- so for some patterns and inputs time grows exponentially in
input length (ReDoS). `regex` uses the Pike VM approach, advancing all forks together one step at a time, so time is proportional to input length. In exchange,
features that require backtracking, such as backreferences, are absent. What it does not do is written at the top of the module document.

== Common mistakes

#antipattern[Trusting the result of removing a prefix without asking whether it was there][
  #demo("examples/ch33/mistake_removeprefix.low")

  `strings.remove_prefix` returns *the original unchanged* when the prefix is absent. The design gives up reporting failure so that the
  result is always a usable view. So given `POST /a`, seven bytes come back as if "GET " had been removed. Where the prefix must be present
  for the meaning to hold, ask with `starts_with` first.

  #demo("examples/ch33/removeprefix_fixed.low")
]

#antipattern[Treating the whole output buffer as the result of encoding][
  #demo("examples/ch33/mistake_wholedst.low")

  `codec.hex_enc` writes 12 bytes into the 16-byte buffer and returns 12 (the result 1612 puts the buffer size 16 and the count 12 side by
  side). Using all of `dst` as the result tacks four unwritten bytes onto the end. It is the same mistake as discarding the return value of
  C's `sprintf` and sending the whole buffer. The result is always `subslice dst 0 n`.
]

#misconception[`len` is the number of characters, and a byte position is a character position][
  #demo("examples/ch33/bytes_not_chars.low")

  `len` counts bytes. "안녕" is 6 bytes and 2 characters, so `lengths` returns 62. Byte 1 lies in the middle of the first character, so
  `utf8.decode` returns `none` (0 here), and the second character "녕" (U+B155, 45397) starts at byte 3. To count or step through
  characters, use the ops of `utf8`; the number of columns a character takes on screen is yet another matter (#chref("lib-terminal")).
]

== This chapter's syntax at a glance

#dtable(
  columns: 3,
  id: "lib-text-glance",
  caption: [Shapes of the text modules --- shape · meaning · why it looks this way],
  [*Shape*], [*Meaning*], [*Why*],
  [`strings.starts_with s p` · `strings.find hay needle from`], [ask · search (`option u64`)], [on views --- no copying],
  [`strings.remove_prefix s p`], [a view without the prefix --- the original if absent], [always a usable view, no failure],
  [`fmt.put_str buf pos s` · `fmt.put_u64 buf pos n`], [assemble into the caller's buffer --- next position as `option`], [all or nothing],
  [`utf8.count_chars s` · `utf8.decode s at`], [character count · code point at that position], [invalid input gives `none`, not a replacement character],
  [`codec.hex_enc src dst` · `b64_enc`], [transcribe and return the count], [the result is `subslice dst 0 n` --- not encryption],
  [`hash.bucket_of key n` · `hash.crc data` · `hash.sha256`], [choosing a slot · detecting damage · detecting tampering], [a different hash per question],
  [`regex`], [compile once, match many inputs], [no backtracking --- time proportional to input length],
)

#recap[
  `strings` gives copy-free views, `fmt` assembles into the caller's buffer and returns the next position as an `option`, and `strbuf` appends to an owned buffer.
  `utf8` and `utf16` report invalid input as `none` without replacement characters, and `unicode`'s tables were extracted mechanically. `codec` transcribes hex and
  base64 all or nothing. `hash` gives different hashes for slot choice, damage detection and tamper detection, and `regex` does not backtrack.
]
