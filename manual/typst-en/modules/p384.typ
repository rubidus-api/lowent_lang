#import "../../typst-ko/lib.typ": *

= `p384` --- the NIST P-384 curve and ECDSA verification <mod-p384>

#modhead(file: "lib/p384.low", layer: [L0 --- pure computation (the caller's backing)], caps: [none])

*ECDSA signature verification* on the curve `y² = x³ − 3x + b` over the prime `p = 2^384 − 2^128 − 2^96 + 2^32 − 1`. TLS 1.3's
`ecdsa_secp384r1_sha384` uses this curve. Many intermediate certificate authorities on the public web, and their roots, sign with P-384,
so checking a certificate chain to its end needs this curve.

#aside[What it promises and what it does not][
  It has not been audited. It exports *verification* only --- no signing, no point decompression, no constant-time scalar multiplication.
  The scalars in verification are public, so branching on them is fine; handling secret scalars is not this module's job.
]

*Why a new module instead of changing `p256`.* #modref("p256")[`p256`] hard-codes its 16 limbs into loop bounds and workspace offsets, and
several modules already use it, fully checked. Making it generic over the limb count would touch checked code. So each curve gets its own
module --- this file is `p256` carried from 16 limbs to 24, and *not one formula differs*. Only the sizes do.

#dtable(
  columns: 3,
  id: "mod-p384-sizes",
  caption: [`p256` and `p384` differ only in sizes],
  [*place*], [*`p256`*], [*`p384`*],
  [one number (16-bit limbs)], [16], [24],
  [one point (Jacobian X · Y · Z)], [48], [72],
  [Montgomery scratch], [18], [26],
  [exponent bits], [256], [384],
)

#dtable(
  columns: 2,
  id: "mod-p384-ops",
  caption: [ops of `p384`],
  [*op*], [*what it does*],
  [`ecdsa_ok`], [does the signature `(r, s)` match hash `e` and public key `pub` --- `ok 1` yes · `ok 0` no · `error short_workspace` the workspace is too small],
)

`ecdsa_ok` takes the curve constants (`p` · `n` · `Gx` · `Gy`) from the caller, the public key in affine form `x ‖ y` (48 limbs), and a
workspace `w` of at least 1364 limbs (1.5 × `p256`'s 924). *A short workspace is a failure, not an answer* --- once "not enough room" and
"bad signature" were both 0, and a program that passed the wrong workspace looked like it had a bad signature.

The computation: first check that `r` and `s` lie in `1 … n−1`, then

```text
w  = s⁻¹ mod n
u1 = e·w mod n ,  u2 = r·w mod n
R  = u1·G + u2·Q         (Q = the public key point)
true if R.x ≡ r (mod n)
```

Modular multiplication reuses #modref("bigint")[`bigint`]'s Montgomery product --- `p` and `n` are both odd, so the condition holds and no
new arithmetic is built. Coordinates are Jacobian for the same reason as `p256` (the inverse is postponed to one at the very end).

*What is checked* --- two oracles we did not build. ① A signature made by `openssl` (secp384r1 · SHA-384) and accepted by `openssl dgst -verify`
--- one positive and *three negatives* (one bit of `s` · one bit of the hash · `s = 0`). ② Real certificate chains signed by real CAs (the
intermediate of several public sites is P-384). The NIST CAVP vectors have *not been run yet* --- the file is not in the repository. The
consumer of this module is #modref("verify")[`verify`].
