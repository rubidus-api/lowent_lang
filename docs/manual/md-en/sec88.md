# <a id="mod-bigwidth"></a>`bigwidth` — the trait of big-number widths

Source

`lib/bigwidth.low`

Layer

L0 — pure computation

Capabilities

none

It holds one trait, `width`. [`bigint16`](sec89.md#mod-bigint16) · [`bigint32`](sec90.md#mod-bigint32) · [`bigint64`](sec91.md#mod-bigint64) · [`bigint`](sec92.md#mod-bigint) each give a type `limb` that satisfies it — `bigint16.limb` is a value meaning «compute in 16-bit limbs». When a library op that uses big numbers takes this trait, the caller picks the width **on each call**. The ops that take it are [`rsa`](sec93.md#mod-rsa)’s `verify_pss_by` · `verify_pkcs1_by` and, since 2026-10-24, the `_by` twin of every curve op that computes with big numbers ([`p256`](sec94.md#mod-p256) · [`p384`](sec95.md#mod-p384) · [`ed25519`](sec97.md#mod-ed25519) · [`ecdsa`](sec96.md#mod-ecdsa) — generated from the hand-written ops by `scripts/gen-curve-by.py`).

```text
proc verify_pss_by input comptime wt type . input lw wt . … requires bigwidth.width wt. . .
  … method lw. mod_exp. ow. sw. e. nw. mb. r2. acc. mw. tw. . …

let w lit bigint16.limb do tag 0 . end . .
rsa.verify_pss_by bigint16.limb. w. sig. n. …     ← only this call uses 16-bit limbs
```

The methods have the names and inputs of the big-number ops — `limbs` · `zero` · `from_bytes` · `to_bytes` · `from_bytes_le` · `to_bytes_le` · `ge_mod` · `dbl_mod` · `mont_mul` · `n0inv` · `mod_exp` · `add_mod` · `sub_mod` · `is_zero` · `equal` · `bit`. The width is fixed at translation, so choosing costs nothing (300 RSA-2048 exponentiations · native `-O2` · x86-64: direct call 0.101 s, through the trait 0.102 s — measured 2026-10-23).

**Why a module of its own** — the four width modules use it, and `bigint` uses three width modules. With the trait inside one of them, they would call each other in a cycle.

There is a separate way to change the width **for the whole program** — `build limb_bits` in `pkg.low` or `lowentc --limb-bits` ([`bigint`](sec92.md#mod-bigint)).

---

[← Prev](sec87.md) · [Contents](README.md) · [Next →](sec89.md)
