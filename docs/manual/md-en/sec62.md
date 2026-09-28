# <a id="mod-utf8"></a>`utf8` — UTF-8 code point iteration and validation

Source

`lib/utf8.low`

Layer

L0 — pure computation

Capabilities

none

Walks and checks UTF-8 byte strings **by code point**. A code point is the number Unicode gives a character (`'한'` = U+D55C = 54620). In UTF-8 one code point takes 1 … 4 bytes — one number is not one byte. Use it to count characters in a string mixing Korean and emoji, or to see whether input is valid UTF-8.

```lowent
use utf8 .

let c be option u64 utf8.decode s 0 .
guard is_some c . else return 1 .
```

`0` is not “the first character” but **byte offset 0**. Every position in this module is a byte offset, and advancing by character is done by `next_start`.

**Why validation is needed.** UTF-8 is variable-length, so not every byte string is valid. A sequence may be cut short, a continuation byte may be wrong, or the same character may be written in more bytes than needed — an **overlong encoding**. Overlong encodings are especially dangerous: if the same code point can be written as two byte strings, a filter like “does this string contain `/`” lies. It is a classic path for bypassing security filters.

**Why it is a library.** `str` is bytes (chapter 9), and UTF-8 was not enforced as a type invariant. Encoding is a choice of a higher layer, and this module is that layer. Rust enforces UTF-8 on `str` to get character iteration for free, at the cost of validation on every construction path. Here validation is paid **only when wanted**, and in exchange byte slicing (`subslice`) cannot fail. No builtin was added.

## <a id="sx1"></a>Design and boundaries

- **Invalid UTF-8 is answered with a value.** `none` means “from here it is not UTF-8”. It neither stops nor inserts a replacement character (U+FFFD) — replacement is an irreversible loss that takes the choice away from the caller.
- **What it rejects** — overlong encodings, surrogates (U+D800 … DFFF), values above U+10FFFF, truncated sequences, and continuation bytes in lead position. Surrogates are parts UTF-16 uses to split large characters over two units, so they have no place inside UTF-8 ([`utf16`](sec63.md#mod-utf16)).
- **No partial answers.** `count_chars` gives `none` on meeting an invalid byte.
- **Not built** — normalisation, locale comparison, grapheme clusters ([`term`](sec67.md#mod-term)), case conversion, encoding (code point → bytes). Iteration and validation only.

| **Code point range** | **Bytes** | **Lead byte** | **Continuation bytes** |
|---|---|---|---|
| U+0000 … U+007F | 1 | `0xxxxxxx` | none |
| U+0080 … U+07FF | 2 | `110xxxxx` | `10xxxxxx` × 1 |
| U+0800 … U+FFFF | 3 | `1110xxxx` | `10xxxxxx` × 2 |
| U+10000 … U+10FFFF | 4 | `11110xxx` | `10xxxxxx` × 3 |

*Table 50.1 — UTF-8 byte layout*

Three things follow from the table. The lead byte alone gives the length (`seq_len`). `10xxxxxx` in lead position is not the start of a character. A value smaller than the minimum code point each length can hold (at least 128 for 2 bytes, 2048 for 3, 65536 for 4) is overlong — `decode` filters with these thresholds.

## <a id="sx2"></a>Ops at a glance

| **op** | **Signature** | **When it cannot** |
|---|---|---|
| `is_cont` | `(b u8) → bool` | never fails |
| `seq_len` | `(b u8) → u64` | `0` if not a lead byte |
| `decode` | `(s slice u8, at u64) → option u64` | `none` if invalid, truncated or out of range |
| `next_start` | `(s slice u8, at u64) → option u64` | `none` if invalid, truncated or at the end |
| `count_chars` | `(s slice u8) → option u64` | `none` if any byte is invalid |
| `is_valid` | `(s slice u8) → bool` | never fails |

*Table 50.2 — Ops of `utf8` — all `fn` · `effects none`*

## <a id="sx3"></a>Ops in detail

This module holds no state, so where and from where to read is always given by the caller.

- **`is_cont`** — is it a continuation byte (`10xxxxxx`, i.e. `eq (bit_and b 192) 128`). A judgement needing no context, so it takes one byte.
- **`seq_len`** — the byte length (1 … 4) of a code point from its lead byte. Length information lives only in the high bits of the lead byte. **0 is not a length but an error signal** — adding it as is leaves the cursor still and the loop never ends.
- **`decode`** — reads one code point at byte offset `at` and gives its **value**. `none` if `at` is past the end, the lead is a continuation byte, the sequence is cut off by the slice end, a continuation byte is not `10xxxxxx`, the encoding is overlong, it is a surrogate, or it exceeds U+10FFFF. A character spans up to 4 bytes, so it takes the whole slice, and `len s` is the basis for judging truncation. `at` must be the **start** of a character — feeding only positions `next_start` gave is a safe habit. It does not return the length.
- **`next_start`** — the **start position** of the next code point. `none` if invalid — it does not skip and paper over. It looks only at lead, length and truncation; range checks are `decode`’s job. Using the returned value as the next `at` is one step of iteration.
- **`count_chars`** — walks everything and gives the number of code points. `none` on an invalid byte. “Everything” is the contract, so it takes no start position. Cost is O(bytes).
- **`is_valid`** — is all of it valid UTF-8. It embodies “validation is a separate op, not a type invariant”, implemented as `is_some (count_chars s)`. If you also need the count, calling `count_chars` once is better.

## <a id="sx4"></a>Using it

Validate first, read values with `decode`, advance with `next_start`. `"한"` is 3 bytes, 1 character, value 54620 (U+D55C).

```lowent
module cpdump .

use utf8 .
use fmt .

proc main
  input out cap io .
  input al  cap allocator .
  output u8 .
  effects alloc io .
do
  let s be slice u8 "a한😀" .
  guard utf8.is_valid s . else return 65 .

  let g be option mut slice u8 alloc_bytes al capacity 128 .
  guard is_some g . else return 70 .
  let buf be mut slice u8 some_value g .
  var pos be u64 0 .

  var i be u64 0 .
  while lt i (len s) . do
    let c be option u64 utf8.decode s i .
    guard is_some c . else return 66 .
    let a be option u64 fmt.put_str buf pos "U+" .
    guard is_some a . else return 71 .
    let b be option u64 fmt.put_hex buf (some_value a) (some_value c) .
    guard is_some b . else return 72 .
    let d be option u64 fmt.put_nl buf (some_value b) .
    guard is_some d . else return 73 .
    set pos (some_value d) .
    let nx be option u64 utf8.next_start s i .
    guard is_some nx . else return 67 .
    set i (some_value nx) .
  end

  let w be u64 write_out out 1 (subslice buf 0 pos) .
  return 0 .
end
```

The output is three lines: `U+61`, `U+d55c`, `U+1f600`. In one pass `i` jumps 0 → 1 → 4 → 8, through `a` (1 byte), `한` (3 bytes) and `😀` (4 bytes). **Never advance with `add i 1`** — on the second round it would point into the middle of a character. If you only need the count, it is one line: `let n be option u64 utf8.count_chars s .`

## <a id="sx5"></a>Counter-examples

> **Counter-example. Patching `none` with a replacement character and carrying on**
>
> > Skipping a byte with `set i (add i 1)` when `next_start` fails lets invalid input through silently, and comparisons and filters above stand on a false premise. There is no error — it is **a silently wrong answer**, and the character count quietly grows. Feed invalid input on purpose and see whether later processing runs although `is_valid` is `false`. The only fix is to stop at `none`.

> **Counter-example. Treating a byte offset as a character number**
>
> > `decode s 1` is not “the second character” but “read at byte 1”. Byte 1 of `"한글"` is the middle of a sequence, so it is `none`. If `is_valid` is `true` but one position gives `none`, that position is not a character boundary.

> **Counter-example. Treating `none` from `count_chars` as 0**
>
> > `none` is not “no characters” but “invalid UTF-8”. An empty slice is `some 0`. Merge the two and broken input can never be told apart from input with no characters. Taking it out with `some_value` without checking stops at run time with `E-VM-NONE`.

> **Counter-example. Slicing unvalidated input with `subslice` and assuming it is valid**
>
> > Byte slicing always succeeds but can cut through the middle of a sequence. If the original is fine but only the piece fails `is_valid`, the cut was mid-character. Nobody warns at the cut, and it shows only when output breaks much later. To cut at character boundaries, cut only at positions `next_start` gave.

## <a id="sx6"></a>Cautions

- **`len s` is a byte count.** Only `count_chars` knows the character count. Measuring “at most 10 characters” with `len` goes wrong (`"한글"` has `len` 6 and 2 characters).
- **Advance the cursor with `next_start`.** Advancing by 1 reads continuation bytes as leads, giving `none` or an inflated count.
- **`none` from `decode` and from `next_start` judge different things.** `next_start` checks structure only, not value range (overlong, surrogates, upper limit). For strict iteration use both, or validate everything first with `is_valid`.
- **`count_chars` and `is_valid` are O(bytes).** Calling them in a loop condition every time makes the whole thing O(n²) — count once and carry the result.
- **It works in code points, not in characters as people count them.** Combining characters and emoji sequences count as several code points (one flag emoji is two).
- **Everything is `effects none`.** It can be called from a `fn`, a contract or `comptime`.

---

[← Prev](sec61.md) · [Contents](README.md) · [Next →](sec63.md)
