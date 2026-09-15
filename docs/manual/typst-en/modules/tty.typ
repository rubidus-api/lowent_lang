#import "../../typst-ko/lib.typ": *

= `tty` --- terminal input <mod-tty>

#modhead(file: "lib/tty.low", layer: [L2 --- the capability half (builtins) + pure parsing (this module)], caps: [`cap tty` for the builtins `tty_raw` · `tty_read` · `tty_size`])

For programs that *react to one key at a time*, like editors and menus. Normally a terminal hands lines over only after Enter; until then the kernel collects characters, echoes
them and handles backspace (line buffering and echo). To move the cursor the moment an arrow key is pressed, that convenience gets in the way, so the terminal is switched to
*raw mode*, receiving "immediately, without echo". And an arrow key is not one byte --- `↑` is the three bytes `ESC [ A`, `Delete` the four bytes `ESC [ 3 ~`. `parse_key` reads
such byte strings as keys.

```lowent
let p option u64 . be tty.parse_key buf 0 .
guard is_some p . else return none .
let code u64 be tty.key_of (some_value p) .
let used u64 be tty.len_of (some_value p) .
```

#aside[Raw mode changes the user's terminal settings, and the change survives the program's death][
  If a program dies in raw mode, the user's shell is left without echo. If any code could call such a power, any library could break someone's shell. So only holders of
  `cap tty` may use it --- calling `tty_raw` without the capability is `E-CAP-MISSING` at compile time. At the same time, putting key parsing behind the capability too would make
  it untestable without a real terminal. So it was split in half --- *the pure half is much larger*. The capability half is just three: raw mode, reading and screen size; key
  parsing (here) and ANSI assembly, screen diffs and display width (#modref("term")[`term`]) are pure (#chref("lib-terminal")).
]

*One keycode distinguishes characters from special keys.* Ordinary bytes are their own value (1 … 255, control characters included --- Ctrl-C is 3), and special keys are above
1000. So `is_char` is a single `lt keycode 1000`. *With no tuples*, the result packs into one value `keycode × 16 + length` (length 1 … 8). The length is needed to know where the
next key starts in the buffer. Extract with `key_of` and `len_of` so code survives if the packing changes.

#dtable(
  columns: 3,
  id: "mod-tty-ops",
  caption: [Ops of `tty` and the builtins],
  [*op*], [*Layer*], [*What it does*],
  [`key_up` · `key_down` · `key_right` · `key_left`], [pure], [1001 · 1002 · 1003 · 1004],
  [`key_home` · `key_end` · `key_delete` · `key_pageup` · `key_pagedown` · `key_esc`], [pure], [1005 · 1006 · 1007 · 1008 · 1009 · 1010],
  [`key_unknown`], [pure], [1011 --- an unrecognised special key],
  [`parse_key buf at`], [pure (`effects none`)], [byte string → `some (keycode × 16 + length)` or `none`],
  [`key_of` · `len_of` · `is_char`], [pure], [keycode · length from a packed value · is it an ordinary character (pass a *keycode*)],
  [`tty_raw t on` (builtin)], [`cap tty`], [enter (`true`) · leave (`false`) raw mode],
  [`tty_read t dst` (builtin)], [`cap tty`], [bytes that have arrived into the buffer → `option u64` (`some 0` = nothing yet, not an error)],
  [`tty_size t` (builtin)], [`cap tty`], [screen size → `option u64` (rows = `div v 4294967296`, columns = `mod v 4294967296`)],
)

*What `parse_key` recognises* --- one non-ESC byte (length 1), `ESC` with nothing after (the ESC key, length 1), `ESC O A`–`D` · `H` · `F` and `ESC [ A`–`D` · `H` · `F` (arrows,
Home, End, length 3), `ESC [ 1 ~` · `3 ~` · `4 ~` · `5 ~` · `6 ~` (Home, Delete, End, PageUp, PageDown, length 4). *Incomplete sequences are not invented* --- with only `ESC [`
arrived it is `none`, not a guess at "probably an arrow". One exception --- a lone `ESC` with nothing after is the ESC key (waiting forever is not an option). If `ESC` is followed
by neither `[` nor `O`, it answers ESC key + length 1 (on terminals sending Alt combinations as ESC + character, the next call reads the character).

```lowent
module keydemo .

use tty .

proc main input t cap tty . input al cap allocator . output u8 . effects alloc . do
  let g option mut slice u8 . . be alloc_bytes al capacity 32 .
  guard is_some g . else return 1 .
  let buf mut slice u8 . be some_value g .
  guard tty_raw t true . else return 1 .
  var going bool be true .
  var last u64 be 0 .
  while going . do
    let n option u64 . be tty_read t buf .
    guard is_some n . else do
      set going false .
      continue .
    end
    var off u64 be 0 .
    while lt off (some_value n) . do
      let p option u64 . be tty.parse_key (subslice buf 0 (some_value n)) off .
      guard is_some p . else do
        set off (some_value n) .
        continue .
      end
      let code u64 be tty.key_of (some_value p) .
      set last code .
      if eq code 113 . do set going false . end
      set off (add off (tty.len_of (some_value p))) .
    end
  end
  let r bool be tty_raw t false .
  return narrow u8 last .
end
```

Finish anything that can fail, like allocation, *before entering raw mode* --- failing and returning after entering leaves the terminal raw. Terminal builtins take `cap tty` *as
an argument* --- the op holding the capability is not enough; it must be handed over where it is used. `parse_key` is `effects none`, so all key parsing is verified by VM/native
comparison without a terminal --- the dividend of the split.

#antipattern[Not restoring raw mode][
  Leaving through a `guard … else return 2` after `tty_raw t true` skips restoration and leaves the shell broken. The language has no `defer`, so call `tty_raw t false` on every
  way out, or finish what can fail before entering raw mode. The example above is shaped so the loop always exits downwards.
]

#antipattern[Reading `none` as an error and quitting · advancing by 1 regardless of length][
  `none` from `parse_key` usually means "more bytes needed" --- a read boundary can cut a sequence in the middle. Quitting there makes a program that dies on every arrow key.
  `set off (add off 1)` reads `ESC [ A` as three keys --- always advance by `len_of`. Comparing the packed value directly with `1001` fails because the length is mixed in.
]

*Cautions.* Terminals differ in arrow sequences, so both `ESC [ A` and `ESC O A` are recognised. Sequences still not caught are `none` --- never silently another key. Korean input
comes from `parse_key` *one byte at a time*, so assemble code points with #modref("utf8")[`utf8`]. "Character" in `is_char` means "not a special key", not "printable". In raw mode
Ctrl-C arrives as byte 3, not a signal. Screen size changes (SIGWINCH) are not reported --- ask `tty_size` again each frame if needed. Mouse reports, bracketed paste and parsing
terminal query replies were not built.
