# <a id="mod-p256"></a>`p256` — the NIST P-256 curve and ECDSA verification

Source

`lib/p256.low`

Layer

L0 — pure computation (the caller’s backing)

Capabilities

none

Point arithmetic on the elliptic curve `y² = x³ − 3x + b`, and **ECDSA signature verification** built on it. TLS 1.3′s `ecdsa_secp256r1_sha256` uses this curve.

> **What it promises and what it does not**
>
> > It has not been audited. The verification `smul` **branches** on scalar bits — in verification that scalar is public, so that is fine. For secret scalars (private key, nonce) use `smul_ct` — each step does both doubling and addition and chooses by mask, not by branch. That promise extends to **no secret-dependent branches or memory access**; cache hierarchy, power and electromagnetic leakage are not promised. There is no point decompression and no P-384 or P-521.

**Why this curve.** What public CAs actually issue and browsers actually accept is P-256 and RSA. [`ed25519`](sec89.md#mod-ed25519) is mathematically cleaner but does not work on the public web — the ecosystem decides, not the maths. Signature **generation** is done by [`ecdsa`](sec88.md#mod-ecdsa) on top of this module.

| **op** | **What it does** |
|---|---|
| `limbs` | Limb count (16 = 256 bits) |
| `msub` · `madd` | Modular subtraction · addition |
| `minv` | Modular inverse (Fermat exponentiation) |
| `pmul` | Montgomery product (reusing [`bigint`](sec85.md#mod-bigint)’s) |
| `make_r2` | `R² mod p` — the constant for entering the Montgomery domain |
| `pdbl` · `padd` | Jacobian point doubling · point addition |
| `is_zero` | Is it the point at infinity |
| `smul` | Scalar multiplication `[k]P` — for public scalars |
| `smul_ct` | Scalar multiplication — for secret scalars, choosing by mask without branches |
| `ecdsa_verify` | Does signature `(r, s)` match hash `e` and the public key |

*Table 50.1 — Ops of `p256`*

Curve constants (p · n · Gx · Gy) are passed in by the caller.

**Why Jacobian coordinates — to postpone the inverse.** Point addition in affine coordinates needs a modular inverse every time, and an inverse costs hundreds of times a product. Jacobian coordinates carry the denominator inside the coordinates and postpone the inverse to a single one at the end. For the same reason `ed25519` uses extended coordinates, but its addition formula is **complete** with no exceptional branch, and here there **is** one (a different formula when the points are equal). That branch lives inside `padd`.

**What is checked** — NIST CAVP P-256/SHA-256 signature verification vectors, 2 positive and **8 negative**, field and scalar multiplication compared with a Python reference curve, VM/native agreement. A signature verification test without negatives cannot catch “always true” — a verification test with only positives verifies nothing.

---

[← Prev](sec86.md) · [Contents](README.md) · [Next →](sec88.md)
