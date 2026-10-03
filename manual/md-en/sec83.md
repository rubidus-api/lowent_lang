# <a id="mod-aes"></a>`aes` — the AES-128 block cipher

Source

`lib/aes.low`

Layer

L0 — pure computation (the caller’s backing)

Capabilities

none

Scrambles 16 bytes with a key and yields 16 bytes (FIPS 197). That is all — the length does not change, there is no authentication, and the same input always gives the same output. It exists because TLS 1.3 makes `TLS_AES_128_GCM_SHA256` a MUST (RFC 8446 §9.1).

> **Not a module to use alone**
>
> > The same plaintext block becomes the same ciphertext block, so patterns show through, and alterations to the ciphertext go unnoticed. What you actually need is `encrypt` and `decrypt` of [`gcm`](sec84.md#mod-gcm) this module is the part beneath it. It does not promise constant time and has not been audited. It is a pure Lowent implementation without AES-NI and is slow — if you need throughput, consider [`aead`](sec81.md#mod-aead) (ChaCha20-Poly1305) first. Keys are 128 bits only.

| **op** | **What it does** | **Requires** |
|---|---|---|
| `gmul` | GF(2^8) product (irreducible polynomial `0x11b`) | — |
| `ginv` | GF(2^8) inverse — `x^254` (Fermat) | the inverse of `0` is `0` |
| `sbox` · `inv_sbox` | One S-box byte — **computed from the definition**, not a table · inverse S-box | — |
| `expand_key` | 16-byte key → 176 bytes of round keys | `rk ≥ 176` · `key ≥ 16` |
| `encrypt_block` | Encrypts 16 bytes of `st` in place | `rk ≥ 176` · `st ≥ 16` · `tmp ≥ 16` |

*Table 50.1 — Ops of `aes`*

**There is no block decryption.** GCM is counter mode and uses encryption only — decryption is done with encryption too. `inv_sbox` existing means a place was left for it someday, not that it was built.

**The S-box is not written as a table.** Instead of transcribing 256 constants by hand, the definition `S(x) = affine(x⁻¹ in GF(2^8))` is used directly. Without a table to transcribe there is nothing to transcribe wrongly. A test compares the 256 computed values with the canonical table.

**Performance — as measured.** In the development repository, one `sbox` took 180 ns (7.5 ns if read from a table), one `encrypt_block` about 31 µs, 93 % of it the 160 `sbox` calls. Whole AES-128-GCM ran at 0.46 MB/s; ChaCha20-Poly1305 at 41.8 MB/s. A factor of 15 has already been repaid — `ginv` was multiplying `x^254` 254 times; switching to square-and-multiply took it from 254 to 15 multiplications, and not one digit of the test vectors moved. A table would bring GCM to about 2.5 MB/s, still 17 times slower than ChaCha20, and a table brings in a **secret-indexed cache side channel** — AES’s classic attack surface. We do not buy a side channel for a slow fallback.

**What is checked** — FIPS 197 Appendix B and C vectors, the record ciphertext of RFC 8448 §3 (used end to end), VM/native agreement.

---

[← Prev](sec82.md) · [Contents](README.md) · [Next →](sec84.md)
