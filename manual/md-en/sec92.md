# <a id="mod-der"></a>`der` — minimal DER parser

Source

`lib/der.low`

Layer

L0 — pure computation

Capabilities

none

Tells **where the public key starts and how many bytes it is** in X.509 certificate bytes, and finds the EC scalar in a PKCS#8 private key file. Verifying signatures with that key is the job of [`p256`](sec88.md#mod-p256), [`rsa`](sec87.md#mod-rsa) and [`ed25519`](sec91.md#mod-ed25519).

> **This is not PKI**
>
> > Chain validation, CA trust stores, name matching, validity periods, revocation, extensions — **it does none of them**. So this alone cannot answer “can this certificate be trusted”. The caller must **pin** the public key or judge by its own rules. A half-built PKI is worse than none — it makes you believe you can trust it.

**Lengths were written by the other side.** Every DER length field is chosen by the attacker. So this parser keeps three rules — any length exceeding the buffer fails immediately (answers 0); long-form lengths are accepted up to 4 bytes only; there is no recursion (depth is a loop the caller controls). **Every failure is `0`** — a value, not an exception.

| **op** | **What it does** |
|---|---|
| `tag_at` | Tag byte at `off` |
| `value_off` · `value_len` | Where that TLV’s **value** starts · its length |
| `next_off` | Position of the next sibling TLV |
| `is_spki` | Is this position **shaped** like SubjectPublicKeyInfo |
| `find_spki` | Finds the SPKI inside a certificate |
| `alg_oid_off` · `alg_oid_len` | Position · length of the algorithm OID |
| `key_off` · `key_len` | Position · length of the public key bit string |
| `rsa_n_off` · `rsa_n_len` · `rsa_e` | Modulus and exponent inside an RSA key |
| `p8_alg_off` · `p8_inner_off` · `p8_inner_len` | PKCS#8 algorithm · inner key position · length |
| `ec_priv_off` · `ec_priv_len` | Position · length of the EC private key (scalar) |

*Table 50.1 — Ops of `der`*

**Found by shape, not counted by position.** The SPKI is the sixth or seventh field of a certificate — depending on whether `version` is present. Counting positions goes silently wrong on old certificates. So `is_spki` looks at the shape `SEQUENCE { SEQUENCE { OID … }, BIT STRING }`. Counting breaks when what comes before changes; recognising does not.

**What is checked — parse results are not eyeballed.** The key is extracted from the real certificate of RFC 8448 §3 and used to **verify** that handshake’s CertificateVerify signature. If parsing slips by one byte the signature does not match. Two negatives (one bit each of signature and transcript) are checked too. A check where a person looks at whether the extracted value seems plausible is not a check.

---

[← Prev](sec91.md) · [Contents](README.md) · [Next →](sec93.md)
