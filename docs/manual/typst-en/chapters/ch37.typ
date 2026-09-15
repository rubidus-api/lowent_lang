#import "../../typst-ko/lib.typ": *

= The terminal — `term` and `tty`

#chapter-toc()

#prereq(
  ([#chref("lib-text"), Text and encodings], [`fmt` assembles into the caller's buffer and returns the next position]),
  ([#chref("lib-io-net"), Input/output, networking, time, randomness, cryptography], [separate computation from authority]),
)

#deepqa[
  In #chref("lib-io-net"), why were the `http` parser and `net` different modules?
][
  Because receiving bytes (authority --- needs `cap net`) and interpreting them (computation --- pure) are different jobs. Merged, even pure interpretation drags
  capabilities and effects along and becomes hard to test. This chapter's terminal modules are split in half the same way.
]

#why[
  Terminal programs do two things. They build control bytes to send to the screen, and they read the user's keys. For both, the part touching the operating system is very
  thin, and the rest is computation. How much of the screen to redraw, how many cells one Korean character takes, which key `ESC [ A` is --- these are computation. The
  standard library split this computation into pure `term` and `tty` parsing, and the part touching the operating system into three builtin ops needing `cap tty`. As the
  last chapter of the library tour, it shows what this split actually buys.
]

#organizer[
  You will learn that `term` writes nothing to the screen but assembles ANSI control bytes into the caller's buffer, and how screen diffs redraw only changed cells. You will
  confirm that display width and grapheme clusters differ from byte counts. You will also interpret keys purely with `tty.parse_key`, and see why raw mode must always be
  restored when using the `cap tty` builtins that switch it on and off.
]

#chapter-questions()

== `term` does not write to the screen

Every op of `term` is `effects none`. It only assembles control bytes that move the cursor (`goto`), change colours (`sgr`, `color256`) or clear the screen (`clear`) into
*the caller's buffer* and returns the next position to write. Actually emitting to the screen is done by `outbuf` (#chref("lib-io-net")).

Not knowing this leads to the dead end "the code runs but nothing appears on screen". But thanks to this split, all screen code can be tested without a screen. Just compare
the assembled bytes.

#demo("examples/ch37/assemble.low")

- `term.goto buf 0 2 5` writes the cursor-move bytes into `buf` from position 0 and returns the next position to write, 6, as an `option`. If the
  buffer is too short it writes nothing and gives `none`, because half-written control bytes would garble the screen.
- Rows and columns count *from 0*. The terminal convention (ANSI) counts from 1, so `goto` adds 1 and writes `ESC [ 3 ; 6 H` (27 91 51 59 54 72).
  The choice keeps the same base as array indexes.
- `term.sgr buf 6 1` writes `ESC [ 1 m`, which turns on bold, and the last `term.sgr … 0` writes `ESC [ 0 m`, which resets attributes. The 104 and
  105 in between are the letters `hi`.
- Every op returns "the next position", so the answer of one op becomes the `pos` of the next. Sixteen bytes are written in all, and the `buf` the
  VM shows is exactly those bytes.

== Redrawing only changed cells

#demo("examples/ch37/diffing.low")

`term.diff prev next w out pos` compares the previous screen `prev` with the new screen `next` and writes into `out` only the changed runs, as "go there and write this". If the
two screens are the same it is 0 bytes --- *0 bytes is exactly the answer "don't touch the screen"*. When "hello" becomes "help!", it is 8 bytes of cursor movement and two
changed characters. Redrawing the whole screen every time flickers, and over remote connections transfers a lot.

== The number of cells is not the number of bytes

#demo("examples/ch37/widths.low")

- `hi` is 2 bytes and 2 cells.
- "안녕" is 6 bytes, but Korean characters are full-width, so it is 4 cells.
- The smiling face emoji (U+1F600) is 2 cells.
- The 4 bytes of `e` followed by a combining accent (U+0301) and then `x` are, to a person, two characters (é, x). `row_clusters` counts those clusters.

When aligning or cutting lines in a terminal, counting by bytes or code points misaligns cells for Korean and emoji. `row_width`, `fit_width` and `cluster_len` count by
cells and clusters. The width table was extracted from Unicode data like the `unicode` module (#chref("lib-text")).

#qa[
  Doesn't cell width differ between terminals?
][
  It does. East Asian "ambiguous width" characters and new emoji are drawn differently by different terminals. `term` follows the Unicode standard's tables and writes that
  choice and its limits in the module document. Display width has a usable default --- 1 even when wrong --- so slight misalignment does not collapse the screen, but
  `unicode`'s tables judging whether something is a letter have no such default, so extracting them mechanically was essential.
]

== Reading keys is computation

#demo("examples/ch37/keys.low")

What a terminal sends for the up arrow is the three bytes `ESC [ A`. `tty.parse_key buf 0` reads those bytes as one value holding the key code and the length consumed.
`tty.key_of` extracts the key code and `tty.len_of` the length. Ordinary characters are their byte value as is (1 … 255), and special keys are placed above 1000 (`key_up`
is 1001). So one value distinguishes "this is a character" from "this is an arrow key".

Reading a byte sequence as keys is computation, not input/output, so it is `effects none` and is verified by the VM/native cross-check without a screen. Unknown sequences or
too few bytes are `none`.

== Raw mode is a capability

The part touching the operating system is only three builtin ops: `tty_raw t on` (enter and leave raw mode), `tty_read t buf` (read key bytes) and `tty_size t` (screen size).
All take `cap tty` as their first argument.

#demo("examples/ch37/rawmode.low")

- The buffer is obtained *before* entering raw mode. If allocation failed after entering and it returned, the terminal would be left in raw mode.
- Bytes read with `tty_read` are interpreted with `tty.parse_key`, and pressing `q` ends it.
- At the end it always restores with `tty_raw t false`.

Raw mode *changes the user's terminal settings*. Even if the program dies, the change remains, and in the user's shell input becomes invisible and line breaks stop working.
If such a capability floated around ambiently, any library could wreck someone's shell. So only its holder exercises it. This example waits for key input, so the verification
script only checks it.

#misconception[Restoring raw mode could just be enforced with ownership too][
  A good idea, but this edition does not do so. `tty_raw` is a builtin op returning a boolean, and there is no owned value representing "in raw mode". So forgetting to restore
  is not stopped at translation. It contrasts with `outbuf` and `files` enforcing completion through ownership, and for now it is a discipline a person must remember. The
  module document warns about this in bold too.
]

== What is not built yet

The `tty` module document lists these itself --- mouse reporting, bracketed paste, interpreting terminal query responses, and screen resize signal (SIGWINCH) integration. Each
is a separate piece to build. Because the list is in the source and document, anyone building an editor with this module knows what they must do themselves before importing
it.

== Common mistakes

#antipattern[Creating a path that leaves raw mode without restoring it][
  #demo("examples/ch37/mistake_rawleak.low")

  This code passes translation (restoring `tty_raw` is not enforced by ownership). But if allocation fails, it leaves through `return 2` with
  the terminal still in raw mode. The user's shell stops echoing input and line breaks go wrong. As `rawmode.low` in this chapter does, finish
  every preparation that can fail (getting the buffer) *before* entering raw mode, and make every path after entering it pass through
  `tty_raw t false`.
]

#antipattern[Cutting a row by byte count to fit the screen width][
  #demo("examples/ch37/mistake_bytecut.low")

  Cutting "안녕" at five bytes splits the second character, which is no longer valid text, so `row_width` returns `none` (999). Sent to the
  screen as is, it would have printed broken characters. `term.fit_width row 3` gives, *as a byte length*, the longest prefix that fits in
  three columns --- one "안", 3 bytes. Always cut by that answer.
]

#misconception[The bytes from one read always hold one whole key][
  #demo("examples/ch37/partial_key.low")

  The up arrow is the three bytes `ESC [ A`, but a read may deliver only the first two. `tty.parse_key` then returns `none` (0 here), and
  once the third byte has arrived, it reads up (1). Do not throw the bytes away; prepend the leftover piece to the next read. Discarding
  `none` as "unknown key" becomes the bug where arrow keys occasionally vanish.
]

== This chapter's syntax at a glance

#dtable(
  columns: 3,
  id: "lib-terminal-glance",
  caption: [Shapes of the terminal modules --- shape · meaning · why it looks this way],
  [*Shape*], [*Meaning*], [*Why*],
  [`term.goto` · `term.sgr` · `term.clear` (`buf pos …`)], [assemble control bytes into the caller's buffer], [nothing is written to the screen --- test by bytes],
  [`term.diff prev next w out pos`], [bytes that redraw only the changed runs --- 0 if equal], [less flicker and less traffic],
  [`term.row_width row` · `term.row_clusters row` · `term.cp_width cp`], [columns · grapheme clusters · code point width], [columns are not bytes],
  [`term.fit_width row cols`], [byte length of the longest prefix fitting the columns], [never cut inside a character],
  [`tty.parse_key buf at` · `tty.key_of` · `tty.len_of`], [interpret key bytes (pure) --- `none` when incomplete], [reading keys is computation],
  [`tty_raw t true` · `tty_read t buf` · `tty_size t`], [raw mode · read key bytes · screen size], [`cap tty` --- always restore raw mode],
  [`term.goto buf 0 2 5`], [row and column counted from 0 --- the bytes are `ESC [ 3 ; 6 H`], [same base as array indexes; the op adapts to the terminal's 1-based count],
)

#recap[
  `term` is a pure module assembling ANSI control bytes into the caller's buffer, and emitting to the screen is `outbuf`'s job. `diff` redraws only changed cells and is 0
  bytes when nothing changed. Cell counts differ from byte counts, so count with `row_width` and `row_clusters`. `tty.parse_key` interprets key bytes purely. `tty_raw`,
  `tty_read` and `tty_size`, which touch the operating system, receive `cap tty`, and raw mode must always be restored.
]
