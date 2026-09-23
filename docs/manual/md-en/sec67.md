# <a id="mod-term"></a>`term` — terminal renderer (the pure half)

Source

`lib/term.low`

Layer

L0 — pure computation (the caller’s buffer)

Capabilities

none — sending is [`outbuf`](sec112.md#mod-outbuf)’s job

**Assembles the ANSI control bytes** to send to a terminal, and computes how much the screen changed and how many cells characters occupy. Used in TUIs to redraw only what changed instead of everything (chapter 37).

> **This module writes nothing to the screen**
>
> > It only builds bytes and puts them into the caller’s buffer. Every op is `effects none`. Actually sending to the screen is done by the side holding `cap io` ([`outbuf`](sec112.md#mod-outbuf)) — assembly and output are separate. Missing this leads to the dead end “the code runs but nothing appears”. In return, all escape assembly and diffing is verified by byte comparison without a screen. Key input and raw mode are [`tty`](sec114.md#mod-tty).

```lowent
use term as t .

let p option u64 . be t.goto buf 0 2 4 .
guard is_some p . else return 1 .
```

**Convention.** Sequence ops follow [`fmt`](sec61.md#mod-fmt)’s convention — `(buf, pos, …) → option u64` (new pos). Passing one op’s return as the next op’s `pos` chains sequences, and the last pos is the finished sequence’s length. **All or nothing** — short of room, not a single byte is written. A half-written escape is silently wrong output: the terminal reads the rest as characters and debris like `[3;5H` appears. Coordinates are taken **0-based** and emitted in ANSI’s 1-based form — 1-based is the wire’s business.

| **op** | **Sequence** | **Size** |
|---|---|---|
| `clear buf pos` | `ESC[2J` | 4 |
| `goto buf pos row col` | `ESC[<row+1>;<col+1>H` | 4 + decimal digits of both numbers |
| `sgr buf pos n` | `ESC[<n>m` — 0 reset · 1 bold · 4 underline · 7 reverse | 3 + digits of n |
| `color256 buf pos n bg` | `ESC[38;5;<n>m` (foreground) · `ESC[48;5;<n>m` (background) | 8 + digits of n |
| `cursor buf pos show` | `ESC[?25h` · `ESC[?25l` | 6 |

*Table 50.1 — Sequence ops and the bytes they need*

**Screen diff.** `diff prev nxt w out pos` compares the previous screen `prev` with the new screen `nxt` (same length; one-byte cells in rows of width `w` laid end to end) and, for each **run of consecutive changed cells within a row**, writes `goto + the changed bytes`. Runs never cross row boundaries. No change gives 0 bytes — **0 bytes means “leave the screen alone”**, a normal answer. Conditions: `len prev == len nxt`, `w > 0`, `len prev % w == 0`. UTF-8 screens use `diff_row_utf8 prev nxt row out pos` per row — emitting only changed **code point runs** as `goto(row, code point column)` + those `nxt` bytes. Wider cells change what a diff means, so rather than altering `diff` it sits beside it as a separate op — code relying on the old contract must not silently change.

| **op** | **Answer** | **Failure** |
|---|---|---|
| `diff prev nxt w out pos` | `some <new pos>` | condition violated · `out` short = `none`, **`out` contents undefined** |
| `diff_row_utf8 prev nxt row out pos` | `some <new pos>` | invalid UTF-8 · `out` short = `none`, **`out` undefined** |
| `row_cells row` | number of code points | invalid UTF-8 = `none` |
| `col_off row c` | byte offset of the c-th code point | invalid UTF-8 · row shorter than c cells = `none` |
| `cp_width cp` | 0 · 1 · 2 cells | no failure — 1 if unknown |
| `row_width row` | total cells of a row | invalid UTF-8 = `none` |
| `fit_width row cols` | **bytes** fitting within `cols` cells (never cutting a character) | invalid UTF-8 = `none` · `some 0` if not even one cell fits |
| `cluster_len s at` | byte length of the character cluster starting at `at` (at least 1) | out of range · not a boundary · invalid UTF-8 = `none` |
| `row_clusters row` | number of character clusters in a row | invalid UTF-8 = `none` |

*Table 50.2 — Diff and counting ops — all `effects none`*

**Confusing these four puts the cursor in the wrong place.** Bytes ≠ code points (`row_cells`) ≠ cells (`row_width`) ≠ characters (`row_clusters`). `"한글"` is 6 bytes, 2 code points, **4 cells** and 2 characters; `"e"` + U+0301 is 3 bytes, 2 code points, **1 cell** and **1 character**. `cp_width` is 0 for combining marks and format characters (`is_zerowidth` of [`unicode`](sec64.md#mod-unicode)), 2 for Korean, CJK and fullwidth symbols, and 1 otherwise. Even a wrong width guess of 1 is a usable default, so it never fails — unlike property tests. A character cluster is one leading code point plus all following zero-width code points — backspace and cursor movement must use this unit, or only the accent gets deleted or the cursor stops inside a character.

```lowent
proc diff_frame input out mut slice u8 . . output u64 . effects none . do
  guard ge (len out) 32 . else return 90 .
  let prev slice u8 be "aaaaaaaaaa" .
  let nxt  slice u8 be "aaaaaaxyaa" .
  let p option u64 . be t.diff prev nxt 5 out 0 .
  guard is_some p . else return 1 .
  guard eq (some_value p) 8 . else return 2 .
  let d option u64 . be t.diff_row_utf8 "가나다" "가라다" 0 out 0 .
  guard is_some d . else return 3 .
  guard eq (some_value d) 9 . else return 4 .
  return 42 .
end
```

The first diff changes only columns 1 and 2 of the second row: `ESC[2;2H` (6) + `xy` (2) = 8 bytes; the UTF-8 row diff is `ESC[1;2H` (6) + `라` (3) = 9 bytes. A render loop computes `nxt` → `diff` → on `some p` sends `subslice out 0 p` through `outbuf` and swaps `prev` and `nxt`. Forgetting the swap redraws the same runs every frame.

> **Counter-example. Sending `out` when `diff` returned `none`**
>
> > Only the two diffs are not all-or-nothing — if `out` runs short midway, written bytes remain. Sending that buffer after `none` pushes out a half-written escape and breaks the screen. Retry with a larger buffer, or `clear` and redraw everything.

> **Counter-example. Swapping `prev` and `nxt`**
>
> > Same type, same length, so neither compiler nor runtime catches it, and with equal byte counts tests may pass. The only symptom is the screen reverting to old content. Memorise the order `diff <previous> <new>`.

> **Counter-example. Cutting by bytes or counting cells by code points**
>
> > `subslice row 0 3` has no guarantee that 3 bytes is a character boundary, sending half a character — `fit_width` keeps boundaries. `row_cells "한글"` is 2 but the cells are 4, so aligning by code points is off by two — ask `row_width` for cells.

**Cautions.** If only `none` keeps coming, suspect the buffer size — measure by **room remaining after `pos`**. Estimate `out` at worst as rows × maximum goto width + screen size. Mistaking the no-change `some pos` for failure and falling back to a full redraw keeps the screen right but flickers every frame. Do not pass coordinates already 1-based — everything shifts by one cell. Feeding a UTF-8 screen to the one-byte-cell `diff` can cut multibyte characters — the two diffs are separate ops with different contracts. **Not built** — the remaining cluster rules such as flags (regional indicator pairs), emoji ZWJ sequences and Hangul jamo composition, and diffs of attribute (colour) cells. Better to write down what is not done than to pretend a partial rule is complete.

---

[← Prev](sec66.md) · [Contents](README.md) · [Next →](sec68.md)
