# <a id="mod-bigint"></a>`bigint` — big-number modular arithmetic

Source

`lib/bigint.low`

Layer

L0 — pure computation (the caller’s backing)

Capabilities

none

**Modular multiplication and exponentiation** for numbers that do not fit in one `u64` — such as RSA’s 2048-bit numbers.

> **What it promises and what it does not**
>
> > It does not promise constant time and has not been audited. **Only what is needed was built** — what RSA verification (`s^e mod n`) and curve arithmetic require. There is no big-number division, no negatives, no GCD, no inverse. The exponent `e` is **one `u64`** — narrowed to hold RSA’s public exponent (usually 65537). What was not built cannot be wrong.

**Representation — the limb width is set by accumulation.** Numbers are **16-bit limbs** held in `u64`, laid out little-endian. A product is 16×16 = 32 bits, and adding 128 of them stays at 2^39, safe inside `u64`. With 32-bit limbs the product reaches 2^64 and accumulation overflows. “The register is 64 bits, so limbs are 64 bits” looks natural but is wrong — the limb width is set not by the product but by accumulation.

| **op** | **What it does** | **Requires · notes** |
|---|---|---|
| `zero` | Zeroes the first `k` limbs of `a` | `a ≥ k` |
| `from_bytes` · `to_bytes` | **Big-endian** bytes ↔ limbs | RSA’s encoding |
| `from_bytes_le` · `to_bytes_le` | **Little-endian** bytes ↔ limbs | curve convention |
| `ge_mod` | Is `a ≥ n` (0 · 1) | compares `k` limbs |
| `dbl_mod` | `a ← 2a mod n` | used to make `R²` |
| `mont_mul` | `out ← a·b·R⁻¹ mod n` (CIOS) | `t` is workspace |
| `n0inv16` | `−n⁻¹ mod 2^16` — the Montgomery constant | takes only the lowest limb of `n` |
| `mod_exp` | `out ← bse^e mod n` | `e` is `u64` · `r2`, `acc`, `tmp`, `t` are workspace |

*Table 50.1 — Ops of `bigint`*

**Why Montgomery — to avoid building division.** In the usual modular product `a·b mod n`, the `mod` is big-number division. Montgomery multiplication yields `a·b·R⁻¹ mod n` instead, and with `R = 2^(16k)` the division becomes a **shift**. The price is moving values into and out of the Montgomery domain and needing `R² mod n`, which is also obtained without division — doubling 1 `2·16k` times (`dbl_mod`) and subtracting `n` whenever it overflows. The whole design of this module is that one line: **the product was changed so that division need not be built.**

**What is checked** — byte comparison with Python integer arithmetic, RSA-PSS verification passing end to end ([`rsa`](sec87.md#mod-rsa)), VM/native agreement. **Not built** — constant time, big-number division, negatives, GCD, modular inverse (the curve side’s inverse is Fermat exponentiation in [`p256`](sec88.md#mod-p256)), very large `e`.

---

[← Prev](sec85.md) · [Contents](README.md) · [Next →](sec87.md)
