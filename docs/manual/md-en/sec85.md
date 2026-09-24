# `crypto_hw` — arithmetic the machine helps with

Source

`lib/crypto_hw.low`

Layer

L0 — pure computation (the caller’s scratch)

Capabilities

none

The parts of cryptographic arithmetic **the CPU has an instruction for**. Today that is one thing: multiplication in GF(2^128) — what GCM’s GHASH stands on, and what CRC is too.

> **There is no `unsafe` in this module**
>
> > The **processor** emits the instruction; this source uses ordinary words (`clmul_lo`, `clmul_hi`). So nothing travels: callers stay `effects none`. Writing assembly directly would carry `unsafe` all the way up to TLS (see «The absorbing boundary» in the chapter 30 chapter).

| **op** | **what it does** | **requires** |
|---|---|---|
| `clmul128` | one carry-less product — 128 bits as two words | `out ≥ 2` |
| `gf128_mul` | GF(2^128) product — GCM’s reflected order, Karatsuba (three multiplies) | `out`, `x`, `h` each `≥ 2` |

*Table 50.1 — ops of `crypto_hw`*

**What carry-less multiplication is.** Multiplication with no carries — addition is xor. Every output bit is a combination of input bits, which is why cryptography and checksums are built on it. The answer is 128 bits and this language has no 128-bit type, so it comes back as **two words** (low, high).

**The build decides the speed.** Compiled with `lowentc --hw auto`, it uses the instruction where the machine has one and the plain computation where it does not. With `--hw none` (the default) it is always the computation. **The answer is the same either way** — only speed and timing behaviour differ.

**Measured.** The leaf `ghash` went 82 → 6562 MB/s, the leaf `aes_ctr` 80 → 5475, and AES-128-GCM as a whole 41 → 2848 MB/s (4 MiB, gcc, best of each column across three alignment settings). Those numbers need the AES instructions turned on too (`--hw aes`): with only one of the two, the bottleneck just moves.

**Carrying the instruction was not the end of it.** With the same instruction, the first version drew only half of what the machine could give. The other half was in **how it was used** — eight blocks at a time, one reduction per eight rather than per block, counters built in a register instead of through memory, and the reduction itself cut down to two multiplies. The leaf `aes_ctr` now runs at **the same speed** as OpenSSL on this box.

**What is checked** — the golden suite compares this module’s `gf128_mul` against the processor’s own leaf `ghash`: the same product computed in two different places, each the other’s witness. It also compares the instruction path, the computed path and the VM on the same inputs.

> **Constant time is not promised**
>
> > The instruction path reads no tables, so its timing behaviour is better. Even so, this repository’s cryptography promises no constant time and has not been audited — other places remain. Saying so is how that promise stays honest.

---

[← Prev](sec84.md) · [Contents](README.md) · [Next →](sec86.md)
