# <a id="mod-strbuf"></a>`strbuf` — owned string buffer and null-terminated `cstr`

Source

`lib/strbuf.low`

Layer

L0 — pure computation (the caller’s buffer)

Capabilities

none · only `as_cstr` is `unsafe`

Builds strings **by appending little by little**. Use it to assemble a path from a directory, “/” and a name, or to build a message from several pieces. If [`strings`](sec59.md#mod-strings) is views (reading), `strbuf` is writing — it appends, puts in as much as fits, and grows.

```lowent
use strbuf .

var b strbuf.str_buf be strbuf.new .
let r result void strbuf.sb_error . be strbuf.append b buf "hello" .
guard is_ok r . else return 1 .
```

In this module a “buffer” is two pieces — the **state** `b` remembering how much was written, and the **place** `buf` where the bytes actually go. They are separate, so ops always receive both. Where the buffer comes from (`cap allocator`, a region, a static buffer) is the caller’s business, and the library **never allocates behind your back**. The only builtin added is `cstr_of`, which gives the raw pointer of a slice.

## <a id="sx1"></a>Design and boundaries

**What to do when room runs out is chosen by the caller, by name.** It is the same discipline as `wrap_add`, `sat_add` and `chk_add`. No single op decides for itself depending on the situation.

| **op** | **Kind** | **Behaviour** |
|---|---|---|
| `append` | All or nothing | If it does not fit, it refuses **without writing a single byte** |
| `append_trunc` | As much as fits | Gives the count written — being truncated comes out **as a value** |
| `append_grow` | Grow | **The caller provides** the larger place |

*Table 50.1 — Three kinds of appending*

- **It does not own bytes.** `str_buf` holds only state (`len`), and the byte buffer is an argument on every call. Capacity written in two places would diverge, so it is not in the struct — the `len` of the `buf` the caller gives is the capacity.
- **It does not grow by itself.** Growth is expressed only by the caller giving a larger buffer to `append_grow`.
- **There is no direct path from a view to `cstr`.** A view cannot promise a trailing 0 byte. The only safe way to make a `cstr` is through `str_buf`, and that route makes allocation visible.

> **The null seal**
>
> > Every append **reserves 1 byte** — the content capacity is `len(buf) − 1`. After writing it seals with `buf[len] = 0`, so `as_cstr` is O(1) and allocation-free. The buffer must be at least 1 byte; in practice, remember **a buffer of n + 1 bytes for n bytes of content**.

There are three types. `str_buf` (state `len u64`), `sb_error` (one failure name, `no_room` — failure is a value, not a stop), and `cstr` (a `newtype` of a raw pointer to a null-terminated `char*`, only for the boundary with other languages).

## <a id="sx2"></a>Ops at a glance

| **op** | **Signature** | **When it cannot** |
|---|---|---|
| `new` | `fn () → str_buf` | — |
| `as_str` | `fn (b str_buf, buf slice u8) → slice u8` | — (a view, not a copy) |
| `room` | `fn (b str_buf, buf slice u8) → u64` | stops on underflow for an empty (length 0) buffer |
| `as_cstr` | `unsafe proc (b str_buf, buf mut slice u8) → cstr` | stops out of bounds if `len + 1 > len(buf)` |
| `append` | `proc (b mut str_buf, buf mut slice u8, s slice u8) → result void sb_error` | `error no_room` (buffer unchanged) |
| `append_trunc` | `proc (b mut str_buf, buf mut slice u8, s slice u8) → u64` | — (known from count written < `len s`) |
| `append_grow` | `proc (b mut str_buf, old mut slice u8, new_buf mut slice u8, s slice u8) → result void sb_error` | `error no_room` (original unchanged) |

*Table 50.2 — Ops of `strbuf`*

## <a id="sx3"></a>Ops in detail

- **`new`** — an empty state with `len 0`. No parameters — only state is made.
- **`as_str`** — a view of the bytes written so far, i.e. `subslice buf 0 len`. The null is not in the view — the length is the truth.
- **`room`** — remaining content capacity `len(buf) − 1 − len`. The 1 byte for the null is excluded. Use it to ask “can this much more go in” before appending.
- **`as_cstr`** — seals a null at position `len` and gives the start pointer as `cstr`. `buf` is `mut` because it **actually writes** the null byte. `cstr_of` is `effects unsafe`, so this op is `unsafe` too and cannot be called from pure code.
- **`append`** — if `len + len(s) + 1 ≤ len(buf)`, writes everything, seals and gives `ok`. Otherwise `error no_room`, and **not a single byte is written** (length and buffer unchanged). A half-written buffer is silently wrong content, which is worse than refusal.
- **`append_trunc`** — writes only as much as fits and returns the count written. Comparing it with `len s` tells whether it was truncated. Without comparing, you never know.
- **`append_grow`** — moving the old content to `new_buf`, appending `s` and sealing is one action. The `new_buf` parameter is the embodiment of “never allocates behind your back”. If even the new place is too small, `error no_room`, and **nothing is moved**. To grow you must be the one holding larger bytes.

## <a id="sx4"></a>Using it

The three kinds behave differently. The caller provides the buffer.

```lowent
module demo .

use strbuf .

proc build input buf mut slice u8 . . output u64 . do
  var b strbuf.str_buf be strbuf.new .
  let r1 result void strbuf.sb_error . be strbuf.append b buf "ab" .
  guard is_ok r1 . else return 90 .
  guard eq (field b len) 2 . else return 91 .
  rem the seal --- a null at position len (buf[2])
  guard eq (index buf 2) 0 . else return 92 .
  rem buffer 5 = content 4 + null 1 --- 2 + 3 + 1 > 5, so refused and the buffer stays clean
  let r2 result void strbuf.sb_error . be strbuf.append b (subslice buf 0 5) "xyz" .
  guard is_error r2 . else return 93 .
  guard eq (field b len) 2 . else return 94 .
  rem in the same situation trunc writes as much as fits (2) and returns 2
  let n u64 be strbuf.append_trunc b (subslice buf 0 5) "xyz" .
  guard eq n 2 . else return 95 .
  return field b len .
end
```

Growth is expressed by the caller giving a new place.

```lowent
proc grow input small mut slice u8 . . input big mut slice u8 . . output u64 . effects none . do
  var b strbuf.str_buf be strbuf.new .
  let r1 result void strbuf.sb_error . be strbuf.append b small "ab" .
  guard is_ok r1 . else return 90 .
  let r2 result void strbuf.sb_error . be strbuf.append b small "xyz" .
  guard is_error r2 . else return 91 .
  rem I provide the larger place --- the library does not allocate
  let r3 result void strbuf.sb_error . be strbuf.append_grow b small big "xyz" .
  guard is_ok r3 . else return 92 .
  guard eq (index big 0) 97 . else return 93 .
  guard eq (index big 2) 120 . else return 94 .
  return field b len .
end
```

`build` gives 4 (“ab” 2 + 2 written by trunc) and `grow` gives 5 (“abxyz”). The real use is path assembly — append with `append b pathbuf dpath`, `append b pathbuf "/"`, `append b pathbuf name`, then take the view with `as_str b pathbuf` and pass it to [`files`](sec113.md#mod-files).

## <a id="sx5"></a>Counter-examples

> **Counter-example. Calling `as_cstr` from pure code**
>
> > ```lowent
> > fn f input b strbuf.str_buf . input buf mut slice u8 . . output u64 . do
> >   let p strbuf.cstr be strbuf.as_cstr b buf .   rem ✗ calling an unsafe proc from a fn
> >   return 0 .
> > end
> > ```
> >
> > Rejected at translation with `E-EFFECT-CALC`. Making a `cstr` is a guarantee of null termination, and that guarantee stands only inside an `unsafe` boundary. Make the caller an `unsafe proc`.

> **Counter-example. A buffer size forgetting the null**
>
> > If the buffer is exactly 2 bytes, not even `"ab"` fits (2 + 0 + null 1 > 2). The very first `append` is `error no_room` and `field b len` stays at 0. Content of n bytes needs a buffer of n + 1 bytes.

> **Counter-example. Reading the buffer without checking the `result`**
>
> > ```lowent
> > let r result void strbuf.sb_error . be strbuf.append b buf "hello" .
> > let v slice u8 be strbuf.as_str b buf .   rem ✗ r was not checked
> > ```
> >
> > No stop and no error. The buffer is not broken either — refusal does not touch it. You just get **a short string missing what you expected**. `guard is_ok r . else …` comes first.

> **Counter-example. Mixing state and buffer**
>
> > ```lowent
> > let r result void strbuf.sb_error . be strbuf.append b buf1 "ab" .
> > let v slice u8 be strbuf.as_str b buf2 .   rem ✗ length from buf1, bytes from buf2
> > ```
> >
> > With no error, a view of the wrong bytes comes out. Pair one `str_buf` with one buffer only (or its successor moved by `append_grow`).

## <a id="sx6"></a>Cautions

- **Make the buffer 1 byte larger than the content.** Because of the null seal.
- **Refusal by `append` is quiet.** It is a value, not a stop, so without checking the `result` the program just keeps running.
- **After growing, discard the old buffer.** The moment `append_grow` gives `ok`, the truth is in `new_buf`. Do not pass `old` to later calls.
- **`as_cstr` is not a snapshot.** It gives the start pointer as is, so appending afterwards changes what it points to. Make it right before calling the other language and use it immediately.
- **`room` stops on a length-0 buffer** (`0 − 1`). The buffer is at least 1 byte.

---

[← Prev](sec59.md) · [Contents](README.md) · [Next →](sec61.md)
