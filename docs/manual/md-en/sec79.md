# <a id="mod-chacha"></a>`chacha` — ChaCha20 stream cipher

Source

`lib/chacha.low`

Layer

L0 — pure computation

Capabilities

none

Makes a **keystream** from key and nonce and XORs it with the input (RFC 8439 §2.4). It is symmetric, so the same call encrypts and decrypts.

> **This alone is not safe**
>
> > ChaCha20 **only hides** — it does not stop forgery. Flip a bit of ciphertext and the same bit of plaintext flips, unnoticed. It must be used together with authentication, and that place is [`aead`](sec81.md#mod-aead). Without a special reason, use `aead`. **Nonce reuse is fatal** — running the same (key, nonce) twice reveals the XOR of two plaintexts. It does not promise constant time and has not been audited.

It is pure computation using only 32-bit addition, XOR and rotation, so it is expressible in the language — VM/native cross-checks and contracts apply as they are. **It does 32-bit arithmetic on `u64`.** This language’s `add` stops on overflow, so places needing wraparound state the mask **explicitly** (`bit_and … 4294967295`). The discipline “if you want wrapping, say it by name” pays off directly in cryptography — silent wrapping is a silent defect.

| **op** | **What it does** | **Requires** |
|---|---|---|
| `prep` | Sets up 16 words of state from key, nonce and counter | key ≥ 32 · nonce ≥ 12 |
| `rounds` | Runs 20 rounds (10 double rounds) | work is 16 words |
| `keystream` | Produces a 64-byte block from the state | ks ≥ 64 |
| `stream` | Runs the whole input and writes to `out` | out ≥ len(inp) |

*Table 50.1 — Ops of `chacha`*

The face usually used is `stream`. It answers the number of bytes processed, and **0 means failure**.

**What is checked** — the RFC 8439 §2.3.2 block function vector and §2.4.2 encryption vector. The block function is measured separately because matching only the stream can coincide even with a wrong state layout, and then the next person to build on it goes wrong. **Not built** — authentication (`aead`), XChaCha20 (extended nonce), counter exhaustion detection, constant-time guarantees, parallel block generation.

---

[← Prev](sec78.md) · [Contents](README.md) · [Next →](sec80.md)
