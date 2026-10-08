# <a id="mod-verify"></a>`verify` — certificate signatures and one link of a chain

Source

`lib/verify.low`

Layer

L0 — pure computation (the caller’s backing)

Capabilities

none

Once [`x509`](sec95.md#mod-x509) has **read** a certificate, this module answers on top of it: **was this certificate signed by that one**. Besides the signature, it gathers what one **link** of a chain needs — validity, names that connect, and host name matching.

> **What it promises and what it does not**
>
> > It has not been audited. It accepts four signature algorithms only — RSA v1.5 + SHA-256 · RSA v1.5 + SHA-384 · ECDSA + SHA-256 · ECDSA + SHA-384. Anything else (the SHA-512 family · RSA-PSS certificates · Ed25519 certificates · P-521) is **refused**. There is no revocation check. Letting an unknown algorithm through would turn this module’s answer from “checked” into “could not tell”.

## <a id="sx1"></a>A chain is a line of links

A web server usually sends three layers. Each is signed by the one above it, and the top (the root) must be one we **decided in advance to trust**.

```text
  root (in the trust store)     ← found by trust.find_anchor
     │  signs
     ▼
  intermediate CA               ← link_ok intermediate · root
     │  signs
     ▼
  leaf (example.com)            ← link_ok leaf · intermediate  +  host_ok leaf · "example.com"
```

Call `link_ok` for each link from the bottom up, and add `host_ok` for the leaf. If any answer is not 1, the chain is broken. **How** the chain is gathered (the order the server sent, how many links) is the caller’s choice — `apps/lowget` is a real example.

| **value** | **meaning** |
|---|---|
| `1` | links — signature, CA, names and validity all hold |
| `2` | the child’s issuer name differs from the parent’s subject name |
| `3` | the parent is not allowed to issue certificates (not a CA) |
| `4` | one of the two is outside its validity period |
| `5` | the signature does not hold (or the algorithm is unknown) |
| `6` | the workspace for checking the signature was too small — not a result, but **could not measure** |

*Table 50.1 — what `link_ok` returns — anything but 1 says why it refused*

## <a id="sx2"></a>ops

| **op** | **what it does** |
|---|---|
| `link_ok` | links one step (table above). Byte workspace ≥ 1024 · limb workspace ≥ 2200 |
| `signed_by` | was the child signed by the parent — `ok 1` / `ok 0`, `error short_workspace` if there is not enough room |
| `sig_kind` | the algorithm the child names: 1 RSA+SHA-256 · 2 ECDSA+SHA-256 · 3 RSA+SHA-384 · 4 ECDSA+SHA-384 · 0 unknown |
| `dn_eq` | are two names equal **as bytes** |
| `dates_ok` | does the validity period contain now (one number `YYYYMMDDhhmmss`) |
| `host_ok` · `san_matches` | does a host name match one of the subject alternative names |
| `ecdsa_rs` | takes `r` · `s` out of a DER-wrapped ECDSA signature, right-aligned to the curve size (32 · 48 bytes) |
| `curve` · `curve384` | fills the P-256 · P-384 constants (`p` · `n` · `Gx` · `Gy`) into a workspace |

*Table 50.2 — ops of `verify`*

## <a id="sx3"></a>Design

**Signatures are checked over the original bytes.** `x509.tbs_off … tbs_end` is hashed as is. Checking a copy could make a signature look right even when the copy is wrong.

**The public key length picks the curve.** 65 bytes is P-256 ([`p256`](sec89.md#mod-p256)), 97 bytes is P-384 ([`p384`](sec90.md#mod-p384)). RSA goes to [`rsa`](sec88.md#mod-rsa).

**Names are compared as bytes.** DER has one byte string per name, so that is right. Decoding and comparing as text lets case, encoding and whitespace rules in, and each rule shifts what “equal” means.

**A wildcard covers the first label only.** `*.a.b` matches `x.a.b` but not `a.b` or `y.x.a.b` (RFC 6125 §6.4.3). Case folding is ASCII only.

**`ecdsa_rs` right-aligns.** A DER integer may carry a leading 0 (when the top bit is 1) or be short. Copying it as is verifies a value shifted by one byte, and that mistake only ever shows up as “bad signature”.

**A short workspace is a failure.** It is `error short_workspace` — once “not enough room” and “not signed” were both 0, and a program that passed the wrong workspace looked like it had a bad signature.

---

[← Prev](sec95.md) · [Contents](README.md) · [Next →](sec97.md)
