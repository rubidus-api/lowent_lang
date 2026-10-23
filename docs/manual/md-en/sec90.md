# <a id="mod-bigint32"></a>`bigint32` — big-number modular arithmetic (32-bit limbs)

Source

`lib/bigint32.low`

Layer

L0 — pure computation (the caller’s backing)

Capabilities

none

**Modular multiplication and exponentiation** for numbers that do not fit in one `u64` — such as RSA’s 2048-bit numbers.

> **What it promises and what it does not**
>
> > It does not promise constant time and has not been audited. **Only what is needed was built** — what RSA verification (`s^e mod n`) and curve arithmetic require. There is no big-number division, no negatives, no GCD, no inverse. The exponent `e` is **one `u64`** — narrowed to hold RSA’s public exponent (usually 65537). What was not built cannot be wrong.

**There are three of this module — only the limb width differs.** [`bigint16`](sec89.md#mod-bigint16) · `bigint32` · [`bigint64`](sec91.md#mod-bigint64) have the same op names and the same inputs; they differ only in how many bits each limb holds. The caller picks a width by changing one `use` line. **A fourth**, [`bigint`](sec92.md#mod-bigint), gives the same ops but lets **the machine** pick the width (the target’s multiplier width · `--mul-bits`) — the standard library’s [`rsa`](sec93.md#mod-rsa) · [`p256`](sec94.md#mod-p256) · [`p384`](sec95.md#mod-p384) · [`ecdsa`](sec96.md#mod-ecdsa) · [`ed25519`](sec97.md#mod-ed25519) use it (since 2026-10-23, RFC-0152). Until 2026-10-22 there was one module named `bigint` (16-bit limbs, then 32-bit the same day).

| **module** | **limb** | **x86-64** | **where it fits** |
|---|---|---|---|
| `bigint16` | 16 bits | 0.71 s | machines with a narrow multiplier (8- and 16-bit CPUs) — not yet measured on one |
| `bigint32` | 32 bits | 0.22 s | machines with a 32-bit multiplier (`bigint` picks it on `cortex_m` · `mips_be` · `cheriot`) |
| `bigint64` | 64 bits | 0.12 s | 64-bit machines (`bigint` picks it); in a 32-bit build (x86 `-m32`) it was also 1.35 times faster than `bigint32` |

*Table 50.1 — The three widths — RSA-2048 exponentiation 300 times, native `-O2` (measured 2026-10-22)*

**Sizes are given in bytes.** Every op takes the number’s size in bytes, `nb` (RSA-2048 is 256 · P-256 is 32 · P-384 is 48), not a limb count. Only the module knows the limb count — `limbs nb.` is that number. So changing the width does not break the calling text. A number takes `limbs nb.` slots, and the workspace `t` two more. **⌈`nb` / 2⌉ + 2 slots fit every width.**

**Representation — the limb width is set by accumulation.** Numbers are **32-bit limbs** held in `u64`, laid out little-endian. The Montgomery product passes the carry on after every single product, so one slot’s sum is (2^32−1) + (2^32−1)^2 + (2^32−1) = 2^64 − 1 — it fits `u64` exactly. If it overflowed, `add` would stop, so a wrong count shows at once.

| **op** | **What it does** | **Requires · notes** |
|---|---|---|
| `limbs` | The limb count of an `nb`-byte number | ⌈`nb` / limb bytes⌉ |
| `zero` | Sets the number in `a` to 0 | `a` holds at least `limbs nb.` slots |
| `from_bytes` · `to_bytes` | **Big-endian** bytes ↔ limbs | RSA’s encoding · at most `nb` bytes (missing high bytes are 0) · `from_bytes` returns `nb` |
| `from_bytes_le` · `to_bytes_le` | **Little-endian** 32 bytes ↔ limbs | curve convention · return 32 |
| `ge_mod` | Is `a ≥ n` (0 · 1) |  |
| `dbl_mod` | `a ← 2a mod n` | used to make `R²` |
| `mont_mul` | `out ← a·b·R⁻¹ mod n` (CIOS) | `t` is workspace · the Montgomery constant comes from `n0inv` |
| `n0inv` | `−n⁻¹ mod 2^(limb width)` — the Montgomery constant | `n` is odd |
| `mod_exp` | `out ← bse^e mod n` | `e` is `u64` · `r2`, `acc`, `tmp`, `t` are workspace |
| `add_mod` · `sub_mod` | `o ← (a ± b) mod m` | `a`, `b` are below `m` · `o` may be the same slots as `a` or `b` (2026-10-23) |
| `is_zero` · `equal` | Is it 0 · are the two equal (0 · 1) | 2026-10-23 |
| `bit` | Bit `i` of the number | 0 is the lowest · 2026-10-23 |

*Table 50.2 — Ops of `bigint16` · `bigint32` · `bigint64` (the same in all three)*

**The type `limb` picks a width** — each of the four modules gives a type `limb` that satisfies the trait [`bigwidth.width`](sec88.md#mod-bigwidth). Pass `bigint32.limb.` to a library op that takes a width and only that call computes in 32-bit limbs (`rsa.verify_pss_by` …).

[`bigint64`](sec91.md#mod-bigint64) has one more — the upper half of a product, `mul_hi`.

**Why Montgomery — to avoid building division.** In the usual modular product `a·b mod n`, the `mod` is big-number division. Montgomery multiplication yields `a·b·R⁻¹ mod n` instead, and with `R = 2^(32k)` the division becomes a **shift**. The price is moving values into and out of the Montgomery domain and needing `R² mod n`, which is also obtained without division — doubling 1 `2·32k` times (`dbl_mod`) and subtracting `n` whenever it overflows. The whole design of this module is that one line: **the product was changed so that division need not be built.**

**What is checked** — all three widths match Python integer arithmetic byte for byte (sizes 9 · 17 · 32 · 33 · 48 · 100 · 255 · 256 bytes, VM and native), and RSA-PSS verification passes end to end ([`rsa`](sec93.md#mod-rsa)). **Not built** — constant time, big-number division, negatives, GCD, modular inverse (the curve side’s inverse is Fermat exponentiation in [`p256`](sec94.md#mod-p256)), very large `e`.

---

[← Prev](sec89.md) · [Contents](README.md) · [Next →](sec91.md)
