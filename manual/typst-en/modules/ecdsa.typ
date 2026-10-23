#import "../../typst-ko/lib.typ": *

= `ecdsa` --- ECDSA P-256 signing <mod-ecdsa>

#modhead(file: "lib/ecdsa.low", layer: [L0 --- pure computation (the caller's backing)], caps: [none])

Takes a private key and a message hash and produces a signature `(r, s)` (RFC 6979). Verification is `ecdsa_verify` of #modref("p256")[`p256`]. A TLS server uses it to
sign `CertificateVerify` with its own private key --- without signing there is no server.

#aside[What it promises and what it does not][
  *Constant time is promised here only, with its extent written down.* What is removed is branching and memory access that depend on secret scalars (private key d, nonce
  k). The final conditional subtraction of Montgomery multiplication, the cache hierarchy, power, electromagnetic leakage and whatever a compiler might introduce are not
  promised. It has not been audited. P-256 only, no key generation. If `r = 0` or `s = 0` it does not retry but answers failure (probability around 2#super[−128]) ---
  building that branch creates untestable code, and untested cryptographic code is worse than none.
]

*Why the nonce is derived, not random.* In ECDSA, a nonce leaked once or repeated once reveals the whole private key.

```text
s = k⁻¹ (h + r·d)   ⇒   d = (s·k − h) / r
```

Knowing `k` finishes it in that one line, and using the same `k` twice lets `k` be solved from two signatures. And the ways a random source fails are silent --- empty
entropy, identical state after a fork, a restored virtual machine snapshot. So *no random source is used at all.* `k = HMAC-DRBG(private key, message hash)`. The same (key,
message) gives the same signature, and that is a property, not a defect.

#dtable(
  columns: 3,
  id: "mod-ecdsa-ops",
  caption: [Ops of `ecdsa`],
  [*op*], [*What it does*], [*Requires*],
  [`nonce6979`], [RFC 6979 §3.2 --- (d, h) → k], [`w ≥ 480` bytes · `wu ≥ 16` limbs],
  [`sign`], [(d, h) → `r ‖ s`, 64 bytes], [`wb ≥ 512` bytes · `wu ≥ 908` limbs],
)

Curve constants (p · n · Gx · Gy) are given by the caller --- this module holds no tables. Signing uses only `smul_ct` of `p256`, and a test confirms it. `k⁻¹` is Fermat
exponentiation whose exponent (n − 2) is public, so the order of operations is fixed.

*Generosity is not free.* The workspace was first sized at 1052 limbs, which exceeded `--run`'s array argument limit (u64 1024) and could not be tested on the VM. Gaps were
pulled in twice to reach 908 --- a size that cannot be tested is not a size but a defect. *Performance* --- `sign` has many parameters, locals and instructions and does not
enter the typed fast path. One signature per connection makes that bearable for now.

*What is checked* --- the official RFC 6979 §A.2.5 vectors (nonce k and r · s for messages `"sample"` and `"test"`, byte for byte), whether our verifier accepts the
signatures produced, and whether flipping one bit is rejected. Vector comparison asks "is it per the standard"; the round trip asks "do our two sides agree".
