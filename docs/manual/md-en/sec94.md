# <a id="mod-http"></a>`http` — HTTP/1.1 request parser

Source

`lib/http.low`

Layer

L0 — pure computation

Capabilities

none

Takes request bytes and answers **where each field starts and how many bytes it is** (RFC 9112). It does not cut — the caller cuts with `subslice`. **Every failure is `0`**. `0` can never be a legitimate position — the smallest request is `GET / HTTP/1.1␍␊␍␊`, so no field starts at 0.

> **What it promises and what it does not**
>
> > Server-side request parsing only (no response parsing). There is no transport — the caller gathers the bytes. **Chunked transfer (`Transfer-Encoding: chunked`) is not built** — it only finds that header, so the caller must reject. No trailers, multi-line headers (obs-fold), URL percent-decoding or HTTP/2. HTTP/1.0 is not accepted — its persistent connection rules differ and were not built.

```lowent
let t u64 be http.target_off b .
let n u64 be http.target_len b .
guard gt n 0 . else return 0 .
let target slice u8 . be subslice b t (add t n) .
```

| **op** | **What it does** |
|---|---|
| `method_get` · `method_head` · `method_post` · `method_put` · `method_delete` | Method codes (1 … 5) |
| `method_code` | Method in the request line → code. 0 = unknown |
| `line_next` · `line_len` | Position of the next line · content length of this line (without CRLF) |
| `target_off` · `target_len` | Position · length of the request target |
| `version_ok` | Is it `HTTP/1.1` |
| `headers_off` · `header_next` | First header · next header (0 at the blank line) |
| `name_len` · `value_off` · `value_len` | Header name length · value position · value length |
| `name_eq` | Is this header’s name that one (case-insensitive) |
| `header_find` · `header_find_len` | Value position · length for that name. **0 if duplicated** |
| `content_length` | `option u64` — `some 0` if absent, `none` if malformed |
| `body_off` | Where the body starts |

*Table 50.1 — Ops of `http`*

**The core of this module is rejection.** A parser is defined more by what it rejects than what it accepts. Wrong acceptance in HTTP has a name — **request smuggling**. If the front (proxy) and back (server) read the same bytes differently, a request one sees the other does not.

| **Rejected** | **Why** |
|---|---|
| Bare `LF` as line end | If the front accepts only CRLF, boundaries diverge |
| Space between name and colon (`Host : x`) | RFC 9112 §5.1 requires rejection |
| Two `Content-Length` | Rejected even if the values agree |
| `Content-Length: 5, 5` · `+5` · empty | Digits only — that leniency is smuggling |
| `HTTP/1.0` | Different persistent connection rules, not built |
| The same header twice (`header_find`) | Whether merging is allowed varies per header and that table was not built — unknown means reject |
| Empty target (`GET  HTTP/1.1`) · unterminated headers (no blank line) | — |

*Table 50.2 — What `http` rejects*

**Why `content_length` is an `option`.** “Absent” and “wrong” are different answers. Absent gives `some 0` (a normal request without a body); malformed gives `none` (the connection must be dropped). With one value the two mix, and where they mix is where attacks live.

**This module caught a processor defect.** It was first written with `input b str .`. `--check` passed, but 16 of 21 ops fell onto the slow interpreted path (about 80×). `str` was not a builtin but a local alias in [`strings`](sec59.md#mod-strings) the checker let the name through, but the typed lowering did not know its meaning. The answers were right, so tests could never see it — a silent 80×. Now using such a name in a signature gives a `W-NOT-YET` warning. Passing checks and being fast are different things, and without looking at `--why-slow` this file would have shipped as it was.

**What is checked** — RFC 9112 examples (8 normal), 9 rejections, VM/native agreement, zero ops falling onto the slow path.

---

[← Prev](sec93.md) · [Contents](README.md) · [Next →](sec95.md)
