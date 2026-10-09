# <a id="mod-aead"></a>`aead` — ChaCha20-Poly1305 seal and open

Source

`lib/aead.low`

Layer

L0 — pure computation (the caller’s backing)

Capabilities

none

**Hides** a message ([`chacha`](sec80.md#mod-chacha)) and **testifies to** the message and associated data **together** ([`poly`](sec81.md#mod-poly)). It is exactly what TLS 1.3′s default suite (`TLS_CHACHA20_POLY1305_SHA256`) uses (RFC 8439 §2.8). **If you use cryptography, sealing starts here.**

> **What this implementation cannot promise**
>
> > **It is not constant time.** This code does not branch on secrets, but the language has bounds checks and stops, so timing cannot be promised. Do not use it as is where a remote attacker can measure time. **It has not been audited** — it is checked against RFC 8439 §2.8.2 vectors, and the guarantee goes as far as “the value the standard specifies comes out”. **Reusing a nonce is the end** — two uses of the same nonce with the same key overlap the streams, reveal plaintext and allow tag forgery. This module does not prevent it — counting is the caller’s job.

It receives no capabilities — it works on the key and nonce it **is given**. Sealing and opening allocate nothing themselves, hence many arguments: result places (`ct`, `msg`, `tag`) and workspaces (`otk`, `st`, `work`, `ks`, `pst`, `pad`) are all passed in.

| **op** | **What it does** | **Answers** |
|---|---|---|
| `seal` | Seals `msg` into `ct` and produces a 16-byte `tag` | tag length 16. **0 means failure** |
| `unseal` | Checks `tag` first and gives `msg` **only if it matches** | plaintext length. **0 means failure (or forgery)** |
| `key_gen` | Makes a one-time Poly1305 key per nonce | 32 |

*Table 50.1 — Ops of `aead`*

- `seal` starts the stream at counter **1** — counter 0 was already used for the Poly1305 key. If that one slot overlapped, the tag key would equal the plaintext stream.
- The array fed to the MAC is `AAD ‖ pad16(AAD) ‖ CT ‖ pad16(CT) ‖ le64(|AAD|) ‖ le64(|CT|)`. Thanks to `pad16` every block is a full 16 bytes, and internal calls all share one shape.
- **Failure is 0, the same value as forgery.** `unseal`’s 0 means “the tag does not match” or “a buffer is too small”. They are not told apart because the caller must treat both alike — either way, the plaintext must not be used. Code that receives 0 and carries on is already wrong.
- Tag comparison **accumulates XOR** — no early return. It is the best this language can do, and it is also the limit the warning above speaks of.

**What is checked** — RFC 8439 §2.8.2 vectors and **tamper rejection** (flip one bit of ciphertext or AAD and `unseal` must answer 0), VM/native agreement. **Not built** — constant-time guarantees, streaming sealing, nonce management and counter exhaustion detection, XChaCha20, key derivation (HKDF of [`hmac`](sec79.md#mod-hmac)). See also — [`x25519`](sec83.md#mod-x25519) (key agreement).

---

[← Prev](sec81.md) · [Contents](README.md) · [Next →](sec83.md)
