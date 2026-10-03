# <a id="mod-ed25519"></a>`ed25519` — Ed25519 signature verification

Source

`lib/ed25519.low`

Layer

L0 — pure computation (the caller’s backing)

Capabilities

none

Verifies signatures on the twisted Edwards curve over **the same prime** as Curve25519, `p = 2^255 − 19` (RFC 8032 §5.1.7). Because the prime is the same, the field arithmetic of [`x25519`](sec82.md#mod-x25519) is reused as is — same prime, same field. That one does key agreement; this one verifies signatures.

> **What it promises and what it does not**
>
> > It does not promise constant time and has not been audited. **There is no signing.** No `ed25519ph`, `ed25519ctx` or Ed448. **This algorithm cannot be used for public web HTTPS server certificates** — public CAs practically do not issue Ed25519 server certificates. That is [`p256`](sec88.md#mod-p256)′s place. This module verifies **a peer’s** signatures.

**Edwards addition is complete.** It uses extended coordinates `(X:Y:Z:T)`, `xy = T/Z`. The Edwards addition formula has no exceptions — unlike Jacobian there is no “different formula when the points are equal”. The branch inside `p256`′s `padd` does not exist here at all, and without a branch nothing can go wrong in it or leak through its timing.

**Ambiguous input is rejected.** RFC 8032 lets implementations differ in handling non-canonical encodings and small-order points. Here they are rejected — `S < L` is enforced (otherwise one message has several valid signatures), and failed decompression is rejected. When a standard says “either is fine”, writing down the choice is the document’s job.

| **op** | **What it does** |
|---|---|
| `verify` | Does signature `sig` match public key `pub` and message `msg` — usually the only one used |
| `make_d` · `make_l` · `make_base` | Curve constant `d` · order `L` · base point `B` |
| `eadd` · `esmul` | Point addition · scalar multiplication |
| `compress` · `decompress` | Point ↔ 32-byte encoding |
| `fpow` · `feq` · `fbit0` | Field exponentiation · equality · lowest bit |
| `pow2_minus` · `reduce_l` | `2^n − k` · reduction `mod L` |

*Table 50.1 — Ops of `ed25519`*

The verification equation is `[S]B = R + [k]A` with `k = SHA-512(R ‖ A ‖ M) mod L`.

**Constants that must be transcribed are checked by a property.** `d`, `√−1` and `B` are computed — nothing to write down. The order `L` cannot be derived and had to be written. So a test checks that `[L]B` is the identity. Write it wrong and that check breaks — a way to measure a transcription instead of trusting it.

**What is checked** — RFC 8032 §7.1, 3 positive and **6 negative**, `[L]B = identity`, VM/native agreement.

---

[← Prev](sec90.md) · [Contents](README.md) · [Next →](sec92.md)
