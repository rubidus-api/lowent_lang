#import "../../typst-ko/lib.typ": *

= `bigint` --- big-number modular arithmetic <mod-bigint>

#modhead(file: "lib/bigint.low", layer: [L0 --- pure computation (the caller's backing)], caps: [none])

*Modular multiplication and exponentiation* for numbers that do not fit in one `u64` --- such as RSA's 2048-bit numbers.

#aside[What it promises and what it does not][
  It does not promise constant time and has not been audited. *Only what is needed was built* --- what RSA verification (`s^e mod n`) and curve arithmetic require.
  There is no big-number division, no negatives, no GCD, no inverse. The exponent `e` is *one `u64`* --- narrowed to hold RSA's public exponent (usually 65537). What
  was not built cannot be wrong.
]

*Representation --- the limb width is set by accumulation.* Numbers are *32-bit limbs* held in `u64`, laid out little-endian. The Montgomery product passes the
carry on after every single product, so one slot's sum is (2#super[32]−1) + (2#super[32]−1)#super[2] + (2#super[32]−1) = 2#super[64] − 1 --- it fits `u64` exactly. If it
overflowed, `add` would stop, so a wrong count shows at once. Until 2026-10-22 the limbs were 16 bits; at 32 bits the RSA-2048 exponentiation got about 2.7 times and P-256
signing and verification about 3.2 times faster (a quarter of the limb products). 64-bit limbs need the upper half of a product (`mul_hi`), and measured they were only
1.3 times faster than 32-bit, so the module did not move. It does wrap `mul_hi` for others. The curve modules (`p256` · `p384` · `ed25519`) keep the slot positions of
their workspaces and use half as many limbs (256 bits = 8 limbs in a 16-slot place).

#dtable(
  columns: 3,
  id: "mod-bigint-ops",
  caption: [Ops of `bigint`],
  [*op*], [*What it does*], [*Requires · notes*],
  [`zero`], [Zeroes the first `k` limbs of `a`], [`a ≥ k`],
  [`from_bytes` · `to_bytes`], [*Big-endian* bytes ↔ limbs], [RSA's encoding · at most `4k` bytes (missing high bytes are 0)],
  [`from_bytes_le` · `to_bytes_le`], [*Little-endian* bytes ↔ limbs], [curve convention],
  [`ge_mod`], [Is `a ≥ n` (0 · 1)], [compares `k` limbs],
  [`dbl_mod`], [`a ← 2a mod n`], [used to make `R²`],
  [`mont_mul`], [`out ← a·b·R⁻¹ mod n` (CIOS)], [`t` is workspace],
  [`n0inv32`], [`−n⁻¹ mod 2^32` --- the Montgomery constant], [takes only the lowest limb of `n`],
  [`mul_hi`], [the upper 64 bits of the product of two `u64`], [the lower half is `wrap_mul` · wraps the builtin `mul_hi`],
  [`mod_exp`], [`out ← bse^e mod n`], [`e` is `u64` · `r2`, `acc`, `tmp`, `t` are workspace],
)

*Why Montgomery --- to avoid building division.* In the usual modular product `a·b mod n`, the `mod` is big-number division. Montgomery multiplication yields
`a·b·R⁻¹ mod n` instead, and with `R = 2^(32k)` the division becomes a *shift*. The price is moving values into and out of the Montgomery domain and needing `R² mod n`,
which is also obtained without division --- doubling 1 `2·32k` times (`dbl_mod`) and subtracting `n` whenever it overflows. The whole design of this module is that one
line: *the product was changed so that division need not be built.*

*What is checked* --- byte comparison with Python integer arithmetic, RSA-PSS verification passing end to end (#modref("rsa")[`rsa`]), VM/native agreement. *Not built* ---
constant time, big-number division, negatives, GCD, modular inverse (the curve side's inverse is Fermat exponentiation in #modref("p256")[`p256`]), very large `e`.
