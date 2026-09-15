# <a id="mod-rsa"></a>`rsa` — RSASSA-PSS verification

Source

`lib/rsa.low`

Layer

L0 — pure computation

Capabilities

none

Answers whether an RSA-PSS signature from a peer matches the public key and message (RFC 8017 §8.1.2). TLS 1.3′s `rsa_pss_rsae_sha256` is this.

> **What it promises and what it does not**
>
> > It does not promise constant time and has not been audited. It does **PSS only** — PKCS#1 v1.5 is not built, because RFC 8446 §4.4.3 forbids it in CertificateVerify. There is no SHA-1 family, key generation or decryption (RSA encryption). Signing is not built either — a TLS 1.3 server may need it someday, but what came first is [`p256`](sec87.md#mod-p256), with smaller keys and signatures.

The old PKCS#1 v1.5 had deterministic padding and shallow structure, and attracted several attacks. PSS mixes in a salt and covers it with a mask. PSS verification has three pieces.

```text
① s^e mod n  →  EM (encoded message, emLen = key length)
② make a mask with MGF1(SHA-256) and strip DB
③ does H' = SHA-256(0x00×8 ‖ mHash ‖ salt) equal H inside EM
```

① is `mod_exp` of [`bigint`](sec85.md#mod-bigint), and ② and ③ are this module’s job. The eight `0x00×8` bytes are not decoration — they are a prefix the standard added to separate signed content from certificate signatures. Leaving them out lets signatures from another context be reused.

| **op** | **What it does** | **Requires** |
|---|---|---|
| `mgf1` | The MGF1-SHA256 mask generation function | `out ≥ n` · `w` is backing |
| `pss_verify` | Given EM, is the PSS encoding right | only ② and ③ |
| `verify_pss` | signature · modulus · exponent · hash → 0 or 1 | all of ①②③ |

*Table 50.1 — Ops of `rsa`*

Usually only `verify_pss` is used. `pss_verify` is exposed separately to test ① apart from ② and ③.

**What is checked** — standard PSS test vectors (positive), two negatives (one bit of signature · one bit of hash), MGF1 against a reference implementation, and VM/native agreement. There are two negatives because changing the signature and changing the message are different failures — checking only one cannot filter out an implementation that lets the other pass.

---

[← Prev](sec85.md) · [Contents](README.md) · [Next →](sec87.md)
