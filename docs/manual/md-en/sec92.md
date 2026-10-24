# <a id="mod-bigint"></a>`bigint` — big-number modular arithmetic (the machine picks the width)

Source

`lib/bigint.low`

Layer

L0 — pure computation (the caller’s backing)

Capabilities

none

The op names, inputs and meanings are the same as [`bigint32`](sec90.md#mod-bigint32)′s. One thing differs: **the machine picks the limb width** — when the target’s **limb width** (`machine.limb_bits`) is 16 it calls [`bigint16`](sec89.md#mod-bigint16), when 32 `bigint32`, otherwise [`bigint64`](sec91.md#mod-bigint64). The standard library’s [`rsa`](sec93.md#mod-rsa) · [`p256`](sec94.md#mod-p256) · [`p384`](sec95.md#mod-p384) · [`ecdsa`](sec96.md#mod-ecdsa) · [`ed25519`](sec97.md#mod-ed25519) · [`verify`](sec101.md#mod-verify) use it.

**How the limb width is chosen** — on a target we can measure, the width measured fastest; on one we cannot, the machine’s **multiplier width** (`machine.mul_bits` — the widest product whose upper half one instruction gives). The value is one cell of the target table and can be changed at any time.

| **Target** | **Multiplier width** | **Limb width** | **Source** |
|---|---|---|---|
| `x86_64` | 64 | 64 | measured (2026-10-23, P-256 sign + verify: 64-bit 5.5 ms · 32-bit 13.4 ms · 16-bit 42.3 ms) |
| `win64` | 64 | 64 | measured (2026-10-23, a Windows 11 virtual machine · x86-64: 64-bit 5.7 ms · 32-bit 12.7 ms · 16-bit 22.5 ms) |
| `arm64` · `riscv64` | 64 | 64 | multiplier width (not measured) |
| `cortex_m` · `mips_be` · `cheriot` | 32 | 32 | multiplier width (not measured) |
| `cortex_m0` | 16 | 16 | multiplier width — Cortex-M0 · M0+ are known to give only the lower 32 bits of a 32×32 product; `bigint16` multiplies limbs in 32 bits, so its Montgomery product uses that hardware multiply (checked in the code `arm-none-eabi-gcc -mcpu=cortex-m0` emits, not timed) |
| `word16` | 16 | 16 | multiplier width (a translate-only target) |

*Table 50.1 — The limb width of each target — the value of `machine.limb_bits` and where it comes from*

**To change the width for the whole program**, write `build limb_bits 32 .` (or `16` · `64`) in the project’s `pkg.low`, or pass `lowentc --limb-bits 32 …` — the value of `machine.limb_bits` changes, and every library that uses `bigint` runs at that width (the multiplier width is a fact of the machine and does not change). The command line wins over the manifest, and only the top-level package’s manifest is read: a dependency does not change the width of the program that uses it. **To use another width in one place only**, pass a width type to an op that takes one (`rsa.verify_pss_by` …; see [`bigwidth`](sec88.md#mod-bigwidth)).

> **The limb width is a speed default**
>
> > Every width gives **the same answer** on every machine — values sit in `u64` slots anyway. So if a width causes trouble on some target, falling back to another width (the shared arithmetic) is enough. A measurement can also disagree with the multiplier width: an x86 32-bit build (`-m32`) has a 32-bit multiplier, yet `bigint64` was 1.35 times faster than `bigint32` there (RSA-2048 exponentiation, 2026-10-22). There is no target row for 32-bit x86, so that number is not in the table.

The width is fixed **at translation**, so choosing costs nothing — the compiler folds the comparison and a call to `bigint.mont_mul` goes straight to the chosen module’s op (an op whose body only passes its inputs on to another op of the same signature is called through). The slots a number needs do not depend on the width: ⌈`nb` / 2⌉ + 2 slots are enough (`bigint16` uses the most).

| **op** | **What it does** | **Requires · notes** |
|---|---|---|
| `add_mod` · `sub_mod` | `o ← (a ± b) mod m` | `a`, `b` are below `m` · `o` may be the same slots as `a` or `b` |
| `is_zero` | Is the `nb`-byte number 0 (0 · 1) |  |
| `equal` | Are the two numbers equal (0 · 1) | not named `eq` because that is a builtin |
| `bit` | Bit `i` of the number (0 is the lowest) | to read an exponent or scalar from the top |

*Table 50.2 — Ops added to all four on 2026-10-23 — the limb arithmetic the curve modules used to do themselves*

**What is checked** — the new ops of the four modules (`bigint16` · `bigint32` · `bigint64` · `bigint`) against Python’s integer arithmetic (17 · 32 · 33 · 48 bytes), and curve signature verification and RSA verification giving the same answer at all three widths (`--limb-bits 16 · 32 · 64`). The five files (`bigint16/32/64` · `bigwidth` · `bigint`) come from one template (`scripts/gen-bigint.py`) — editing one file alone fails the comparison with the template.

---

[← Prev](sec91.md) · [Contents](README.md) · [Next →](sec93.md)
