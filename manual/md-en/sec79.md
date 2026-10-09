# <a id="mod-hmac"></a>`hmac` — HMAC-SHA256 and HKDF

Source

`lib/hmac.low`

Layer

L0 — pure computation (the caller’s backing)

Capabilities

none

It does two things. **HMAC** is a token only someone knowing the key can make (`mac`), answering “who sent it” (RFC 4231). **HKDF** is the standard way to split several keys out of one secret (`extract` + `expand1`) (RFC 5869).

> **What it promises and what it does not**
>
> > It does not promise constant time and has not been audited. **Comparing MACs is the caller’s job** — compare without early return (accumulating bytes with XOR). A comparison returning at the first differing byte lets the tag be learned one byte at a time. `expand1` produces **only one block (at most 32 bytes)**.

**Why a hash will not do.** `sha256` (SHA-256) of [`hash`](sec75.md#mod-hash) answers only “is the content unchanged”. Anyone can recompute it, so it cannot answer who sent it. A key must be involved to open that question. The caller holds the backing (`scratch`). The size requirement is enforced by **guards**, not documentation, so if short it answers 0 and writes nothing.

| **op** | **What it does** | **Requires** |
|---|---|---|
| `mac` | `HMAC-SHA256(key, msg)` → 32 bytes into `out` | `out ≥ 32` · `scratch ≥ 64 + len(msg)` |
| `key_block` | Makes K′ (the key fitted to 64 bytes) | `pad ≥ 64`. Keys longer than 64 are hashed down |
| `extract` | HKDF-Extract: `PRK = HMAC(salt, ikm)` | same as `mac` |
| `expand1` | First block of HKDF-Expand: `T(1) = HMAC(prk, info ‖ 0x01)` | `scratch ≥ 130 + len(info)` · `len(info) ≤ 60` |

*Table 50.1 — Ops of `hmac`*

**Why `expand1` is one block only.** TLS 1.3′s `HKDF-Expand-Label` uses only 32 bytes or fewer (keys, IVs and finished values alike). So the loop chaining T(2) was not built. **Why cut at 60.** `msg` lives inside `scratch` (from 128) and `mac` overwrites `scratch[0 .. 64+len(msg)]`, so a long `info` makes the two overlap and the hash tramples its own input. The guard rejects the moment they would overlap — rejecting beats being silently wrong.

**What is checked** — RFC 4231 and RFC 5869 standard vectors, VM/native agreement. **Not built** — constant-time guarantees, a MAC comparison op, multi-block HKDF-Expand (L > 32), hashes other than SHA-256, streaming MACs. See also — `hash` (SHA-256 itself) · [`aead`](sec82.md#mod-aead) (where the split keys are used) · [`x25519`](sec83.md#mod-x25519) (where the secret is made).

---

[← Prev](sec78.md) · [Contents](README.md) · [Next →](sec80.md)
