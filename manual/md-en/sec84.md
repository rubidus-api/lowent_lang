# <a id="mod-gcm"></a>`gcm` — AES-128-GCM authenticated encryption

Source

`lib/gcm.low`

Layer

L0 — pure computation (the caller’s backing)

Capabilities

none

Gives “nobody can read it” and “nobody can alter it” **at once** (NIST SP 800-38D). [`aes`](sec83.md#mod-aes) gives only the first.

> **What it promises and what it does not**
>
> > It does not promise constant time and has not been audited. **Never repeat a nonce** — using the same nonce twice with the same key makes GCM **lose both the plaintext and the authentication key**. It is the most expensive mistake in this mode. **It is slow** — measured at 0.46 MB per second in the development repository, far slower than ChaCha20-Poly1305 on the same machine (41.8 MB). So the preferred suite is ChaCha20. This module exists **correctly** because the standard makes it a MUST, not to be fast. A tag check failure in `decrypt` is a value — always look at the return value.

**What is AAD.** Bytes that are not hidden but **must not be altered**. In TLS 1.3 it is a record’s 5-byte header — an observer sees the length anyway, but if the peer changes it the tag must not match. Unlike `seal` and `unseal` of [`aead`](sec81.md#mod-aead), this module uses the names `encrypt` and `decrypt`. There is a defect where, if two modules share a name, the processor measures the argument count against the other module’s signature even when called qualified, so the names were separated to avoid it.

| **op** | **What it does** | **Requires** |
|---|---|---|
| `gmul128` | GF(2^128) multiplication of GHASH — `z ← z·h` | `z ≥ 16` · `h ≥ 16` · `v ≥ 16` |
| `encrypt` | `msg` → ciphertext and a 16-byte tag | `nonce = 12` · `key = 16` |
| `decrypt` | **Checks the tag first** and gives the plaintext | on failure answers 0 and gives no plaintext |

*Table 50.1 — Ops of `gcm`*

The caller holds the backing (`scratch`). If short it answers 0 and writes nothing — guards keep that.

**GHASH bit order — where people slip.** The reduction polynomial of GF(2^128) is `x^128 + x^7 + x^2 + x + 1`, but GCM uses **reversed bit order** — the top bit of a block’s first byte is x^0. So multiplication runs with right shifts, and overflowing bits come back into the first byte as `0xE1`. Read that convention backwards and the ciphertext is right but the whole tag differs, looking only like “decryption fails”.

**On nonces, again.** GCM is counter mode, so the same (key, nonce) yields the same keystream. The XOR of two plaintexts is revealed, and worse, an equation arises from which the GHASH authentication key can be solved, letting the peer forge any message. TLS 1.3 avoids this by **deriving the nonce from the sequence number** (`record_nonce` of [`tls13`](sec96.md#mod-tls13)). If you use it directly, follow that method — random 96-bit nonces collide sooner than you think.

**What is checked** — NIST GCM test vectors, the record ciphertext of RFC 8448 §3 (ciphertext and tag byte for byte), VM/native agreement.

---

[← Prev](sec83.md) · [Contents](README.md) · [Next →](sec85.md)
