# <a id="mod-x509"></a>`x509` — reading X.509 certificates

Source

`lib/x509.low`

Layer

L0 — pure computation (the caller’s backing)

Capabilities

none

**Reads the skeleton** of an X.509 certificate (DER): where the signed part starts and ends, who the issuer and subject are, and where the public key, validity and extensions sit — all as **offsets and lengths**. It is the step after [`der`](sec93.md#mod-der), which stopped at “extract the public key”.

> **It only reads**
>
> > This module **does not check** a certificate. Whether the signature holds, whether a chain links, whether a name matches — that is [`verify`](sec96.md#mod-verify), built on top of this one. Not built: revocation (CRL · OCSP) · name constraints · policies · the old v1/v2 shapes · comparing names that are not UTF-8.

The shape of a certificate and where this module’s ops point:

```text
Certificate
├─ tbsCertificate ─────────────── tbs_off … tbs_end   ← the bytes the signature covers
│   ├─ [0] version (may be absent)
│   ├─ serialNumber                tbs_field 0
│   ├─ signature                   tbs_field 1
│   ├─ issuer (issuer name)        issuer_off · elem_len
│   ├─ validity                    not_before · not_after
│   ├─ subject (subject name)      subject_off · elem_len
│   ├─ subjectPublicKeyInfo        spki_off
│   └─ [3] extensions              ext_value_off · is_ca · san_next
├─ signatureAlgorithm ─────────── sigalg_oid_off · sigalg_oid_len
└─ signatureValue ─────────────── sig_off · sig_len
```

| **op** | **what it does** |
|---|---|
| `tbs_off` · `tbs_end` | start and end of the signed bytes. A signature is checked over **these original bytes** |
| `sigalg_oid_off` · `sigalg_oid_len` | offset and length of the outer signature algorithm OID |
| `sig_off` · `sig_len` | the signature value (past the BIT STRING’s “unused bits” byte) |
| `tbs_field` | offset of the `n`-th field in tbsCertificate (0 serial · 1 signature · 2 issuer · 3 validity · 4 subject · 5 public key) |
| `issuer_off` · `subject_off` · `elem_len` | offset and length of the **whole** issuer / subject name element — names are compared as bytes |
| `spki_off` | offset of the public key (SubjectPublicKeyInfo) |
| `not_before` · `not_after` | validity as one comparable number (`YYYYMMDDhhmmss`). Only the `Z` time zone is accepted |
| `ext_value_off` | value offset of the extension whose OID is `2.5.29.<n>` (17 = subject alternative name · 19 = basicConstraints) |
| `is_ca` | may this certificate issue others. **No extension means false** |
| `san_next` | the DNS names of the subject alternative name, one at a time (`cur` 0 = the first, 0 back = no more) |

*Table 50.1 — ops of `x509` — an op that returns an offset returns 0 for “malformed”*

**No copies.** Every op returns only an offset and a length. A few certificates must fit in a 64 KiB arena at once, and a signature must be checked over the original bytes — checking a copy could make a signature look right even when the copy is wrong.

**Every length was written by the other side.** A certificate is bytes nothing has vouched for yet. Every offset passes through `der.value_off` · `der.value_len`, which keep it inside the buffer. This module adds **order** on top: X.509 fields have no labels, so a field’s position is what it is — ignore the order and you read a field someone slipped in as if it were in its own place.

**The default of `is_ca` matters.** Without a basicConstraints extension a certificate is not a CA (RFC 5280 §4.2.1.9). Miss that default and a leaf certificate can act as an intermediate — the most common hole in chain checking.

---

[← Prev](sec94.md) · [Contents](README.md) · [Next →](sec96.md)
