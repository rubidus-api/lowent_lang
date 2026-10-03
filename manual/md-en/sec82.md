# <a id="mod-x25519"></a>`x25519` — key agreement on a curve

Source

`lib/x25519.low`

Layer

L0 — pure computation (the caller’s backing)

Capabilities

none

From my secret scalar and the peer’s public point, makes **32 bytes only the two of us know** (RFC 7748). Standard practice is not to use those bytes as a key directly but to split keys out with HKDF from [`hmac`](sec78.md#mod-hmac).

> **What this implementation cannot promise**
>
> > **It does not promise constant time.** The ladder is written with conditional swaps (`fsel`) so as not to branch on secret bits, but the language has bounds checks and stops, so timing cannot be promised. **It has not been audited.** **It does not filter small-order points** — the public key a peer sends may be a point that yields zero, so checking whether the agreed result is all zeros is the caller’s job. Making private keys (entropy) is not here — that belongs to [`random`](sec77.md#mod-random) and `cap random`.

```lowent
rem w has at least 175 u64 elements --- one workspace
let n be u64 x25519.agree shared mysecret theirpub zbuf w .
guard eq n 32 . else return 1 .
```

**Why one workspace.** `scalarmult` takes nine field elements and the multiplication area separately — easier to read inside. But laying out fourteen at the call site quickly eats the parameter limit of 16. The first program using this library was actually rejected with `E-IR-ARITY`. So `agree` slices one `w` (at least 175 elements), and the layout (nine field elements = `w[0..144]`, multiplication area = `w[144..175]`) lives in one place in the source.

**How the representation was chosen.** The prime is `p = 2^255 − 19`. The common reference implementation (ref10) holds limbs of 26 and 25 bits, **lets limbs go negative** and carries with an arithmetic right shift. This language’s `shr` is a **logical shift**, so that layout cannot be used. So a **representation with no negatives at all** (16 limbs × 16 bits, the TweetNaCl layout) was chosen. Subtraction is `a + 4p − b` instead of `a − b` — since `4p ≡ 0 (mod p)` the value is the same and every intermediate is non-negative. The tool’s limit chose the representation, and the one chosen was easier to reason about.

| **op** | **What it does** | **Requires · answers** |
|---|---|---|
| `agree` | scalar × point → 32-byte shared secret (one workspace) | `w ≥ 175` · others 32 bytes each. Answers 32, **0 means failure** |
| `scalarmult` | The same computation, workspaces separately | fourteen arguments |
| `fzero` · `fone` · `fcopy` · `fcar` | Field element init · copy · carry | for code that knows the layout |
| `fadd` · `fsub` · `fmul` · `fsq` · `fmul121665` | Field add · subtract · multiply · square · constant multiply | ditto |
| `finv` · `fsel` · `funpack` · `fpack` | Inverse · conditional swap · bytes ↔ element | ditto |

*Table 50.1 — Ops of `x25519`*

Field arithmetic is `export` **for testing** (identities are measured). Unless you are building a new protocol, `agree` is all you need. [`ed25519`](sec91.md#mod-ed25519) reuses the same field.

**What is checked** — RFC 7748 §5.2 scalar multiplication vectors, the §6.1 DH round trip (do two parties reach the same secret), field identities (`a·a⁻¹ = 1` and so on), VM/native agreement. **Not built** — constant-time guarantees, small-order point filtering, key generation and clamping convenience ops, X448.

---

[← Prev](sec81.md) · [Contents](README.md) · [Next →](sec83.md)
