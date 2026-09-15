# <a id="mod-strings"></a>`strings` — string view operations

Source

`lib/str.low`

Layer

L0 — pure computation

Capabilities

none

Compares, searches, trims and splits strings **without copying**. Use it to split input on spaces, strip a prefix, or find a substring. The result is always a **view** over the original — a window pointing at someone else’s bytes — and not a single byte of new memory is used.

```lowent
use strings .

if strings.starts_with line "GET " . do
  let rest slice u8 be strings.remove_prefix line "GET " .
end
```

`rest` points directly at part of `line`. This module was built only from what the language already has (`len`, `index`, `subslice`, `eq`, `option`, `guard`, `while`, `actor`) and adds no builtin. Most string operations need no ownership — cutting, searching and comparing only produce views. Work that needs a writable buffer, such as appending, belongs to [`strbuf`](sec60.md#mod-strbuf).

## <a id="sx1"></a>Design and boundaries

- **What it does** — byte-level comparison, search, prefixes and suffixes, trimming and splitting. Every `fn` is `effects none` and allocates nothing.
- **It does not interpret encodings.** `str` is bytes. UTF-8 validation and decoding belong to [`utf8`](sec62.md#mod-utf8).
- **It does not own or modify.** It never changes a byte of the original.
- **There is no `str_len` or `str_sub`.** `str` is an alias of `slice u8`, so the builtins `len` and `subslice` work as they are. One meaning does not get two names.
- **No sentinel values.** Conventions like “−1 means not found” are not used. Not found is `none` of an `option`.

`type str slice u8 .` is a **transparent alias**. It is not a `newtype`, so string literals, `slice u8` values and `subslice` results all fit a `str` position as they are. Only the `str_splitter` actor holds state — the original `src`, the separator byte `sep`, the cursor `pos` and the exhausted flag `fin`.

## <a id="sx2"></a>Ops at a glance

Every `fn` is `effects none`, and the actor’s two `proc`s are `effects state`. Call them qualified by module name, as in `strings.find`.

| **op** | **Signature** | **When it cannot** |
|---|---|---|
| `eq_str` | `(a str, b str) → bool` | `false` if different |
| `has_byte` | `(s str, b u8) → bool` | `false` if absent |
| `find` | `(hay str, needle str, from u64) → option u64` | `none` if not found or `from > len hay` |
| `has` | `(hay str, needle str) → bool` | — |
| `starts_with` | `(s str, prefix str) → bool` | — |
| `ends_with` | `(s str, suffix str) → bool` | — |
| `remove_prefix` | `(s str, prefix str) → str` | **The original as is** if no prefix |
| `remove_suffix` | `(s str, suffix str) → str` | The original as is if no suffix |
| `trim_start` | `(s str, cut str) → str` | An empty view if everything is cut |
| `trim_end` | `(s str, cut str) → str` | An empty view if everything is cut |
| `split_next` | `(src str, sep u8, pos u64) → option str` | `none` if `pos > len src` |
| `str_splitter` | actor: `init(s2 slice u8, b u8) → u64` · `next → option slice u8` | `next` is `none` after exhaustion |

*Table 50.1 — Ops of `strings`*

## <a id="sx3"></a>Ops in detail

The `slice u8` passed in is a view, and what comes back is another view over the same bytes. That is why it is not `mut`, is `effects none`, and is valid only while the original lives. The two arguments of comparison and search are always (the thing searched, the thing sought) — `has_byte s b` reads “is `b` in `s`”.

**`eq_str`** — byte-wise equality. If lengths differ it is `false` before looking at bytes. The builtin `eq` is scalar-only, so slice comparison lives here. That is why the name could not be shortened to `eq`.

**`has_byte`** — is the single byte `b` in `s`. A byte, not a string. The cut-set test of `trim_start` and `trim_end` uses it.

**`find`** — searches `hay` for `needle` from `from` and gives the start index as `some`. Why `from` exists is the character of this module: **the cursor is a value the caller holds.** The next search calls again with the position after the match as `from`. The library hides state nowhere, so scanning the same string from several places at once causes no interference. `from > len hay` gives `none`, and an empty `needle` gives `some from`.

**`has`** — when you only ask “is it in there”. Inside it is `is_some (find hay needle 0)`.

**`starts_with` · `ends_with`** — prefix and suffix tests. If the piece is longer than `s`, it is `false` without looking.

**`remove_prefix` · `remove_suffix`** — a view with the prefix or suffix removed. If the piece **is not there, the original is returned as is**. That is not failure, so the return is not an `option`. If you need to know whether it was removed, ask `starts_with` or `ends_with` first.

**`trim_start` · `trim_end`** — a view with certain bytes stripped from the front or back. `cut` is not an ordered string but **a set of bytes** — `" \t"` means “space or tab”, not “space followed by tab”. If everything is cut, a zero-length view comes out.

**`split_next`** — when splitting `src` on the separator byte `sep`, gives as `some` the piece from `pos` up to the next `sep`. **Advancing the cursor is the caller’s job** — on receiving a piece, push `pos ← pos + len(piece) + 1` yourself (+1 for the separator byte). The last piece with no `sep` after it is also `some`, and `pos > len src` giving `none` is the end signal. `"a,b"` gives `"a"`, `"b"`; `"a,"` gives `"a"`, `""` (a trailing empty piece); `""` gives one `""`.

**`str_splitter`** — the pull version of `split_next`. The actor’s state holds the cursor. After putting the original and separator into `init`, `next` takes no arguments, giving pieces as `option` until it is exhausted and gives `none`.

## <a id="sx4"></a>Using it

The module name is the `module` declaration in the file. The module of `lib/str.low` is `strings`.

```lowent
use strings from "lib/str.low" .   rem write the path directly (relative to the using file)
use strings .                            rem resolved from the standard module location
use strings as s .                       rem alias --- then call s.eq_str
```

Finding and splitting:

```lowent
module demo .

use strings .

fn t_find output u64 . do
  let r option u64 . be strings.find "hello world" "world" 0 .
  guard is_some r . else return 99 .
  return some_value r .
end

fn t_split output u64 . do
  rem "aa,b,,cc" split on ',' (byte 44): "aa", "b", "", "cc" --- four pieces
  var pos u64 be 0 .
  var pieces u64 be 0 .
  var r option slice u8 . be strings.split_next "aa,b,,cc" 44 pos .
  while is_some r . do
    set pieces (add pieces 1) .
    set pos (add pos (add (len (some_value r)) 1)) .
    set r (strings.split_next "aa,b,,cc" 44 pos) .
  end
  return pieces .
end
```

`t_find` gives 6 and `t_split` gives 4. If you want state, use the actor version — the actor holds the cursor.

```lowent
proc t_splitter output u64 . effects state . do
  var sp strings.str_splitter . be spawn actor strings.str_splitter .
  let d u64 be send sp init "one,two,three" 44 .
  var pieces u64 be 0 .
  var r option slice u8 . be send sp next .
  while is_some r . do
    set pieces (add pieces 1) .
    set r (send sp next) .
  end
  return pieces .
end
```

The only difference between the two is who holds the cursor. If the same original must be scanned along several paths at once, `split_next` fits; if a single loop just pulls to the end, the actor version is shorter.

## <a id="sx5"></a>Counter-examples

> **Counter-example. Comparing slices with the builtin `eq`**
>
> > ```lowent
> > guard eq "abc" "abc" . else return 0 .        rem ✗ eq is scalar-only
> > ```
> >
> > It is `E-VM-TYPE`. Compare slices with `strings.eq_str`.

> **Counter-example. Not advancing the cursor in a split loop**
>
> > ```lowent
> > var r option slice u8 . be strings.split_next src 44 pos .
> > while is_some r . do
> >   set r (strings.split_next src 44 pos) .   rem ✗ pos stays the same
> > end
> > ```
> >
> > It is not a translation error — **the program never ends.** Giving the same `pos` yields the same piece forever. Advance by `pos + len(piece) + 1` each time.

> **Counter-example. Using an `option` as a value**
>
> > ```lowent
> > let r option u64 . be strings.find "abc" "zz" 0 .
> > return some_value r .                         rem ✗ no none check
> > ```
> >
> > It translates, and stops at run time with `E-VM-NONE` the moment nothing is found. Testing only with inputs that are found never reveals it. `guard is_some r . else …` comes first (chapter 11).

> **Counter-example. Changing the original a view points at, later**
>
> > ```lowent
> > let piece slice u8 be strings.remove_prefix line "GET " .
> > set (index line 4) 88 .                                     rem ✗ the original was changed
> > ```
> >
> > With no error, the content of `piece` silently changes — because it is a window, not a copy. To hold on to the content, copy it with [`strbuf`](sec60.md#mod-strbuf).

## <a id="sx6"></a>Cautions

- **It is byte-wise, not character-wise.** Characters made of several bytes, like Korean or emoji, are not one unit. A `needle` is found as a byte string as it is, but `cut` is a byte set and cannot treat a multibyte character as one unit. For characters, go to [`utf8`](sec62.md#mod-utf8).
- **A view shares lifetime and content with its original.** If the original changes, so does the view’s content; if the original goes away, the view cannot be used. Read it as “received a window”, not “received a value”.
- **`remove_prefix` does not report failure.** If you need to know whether it removed anything, ask `starts_with` first.
- **`find` is a naive search.** Its cost is O(`len hay` × `len needle`). Account for it where very long strings are scanned repeatedly.
- **Cursor version and actor version.** `split_next` shares no state, so several cursors may read at once. `str_splitter` holds state, so whoever spawned it is responsible until it is exhausted.
- **There is no direct path to a null-terminated string.** A view cannot promise a trailing 0 byte. Go through `as_cstr` of [`strbuf`](sec60.md#mod-strbuf).
- **Names clashing with builtins.** `eq` and `contains` are builtins, so this module named its ops `eq_str` and `has_byte`. Avoid builtin names for your own variables too.

---

[← Prev](sec58.md) · [Contents](README.md) · [Next →](sec60.md)
