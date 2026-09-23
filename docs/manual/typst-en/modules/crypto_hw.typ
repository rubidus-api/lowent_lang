#import "../../typst-ko/lib.typ": *

= `crypto_hw` --- arithmetic the machine helps with <mod-crypto_hw>

#modhead(file: "lib/crypto_hw.low", layer: [L0 --- pure computation (the caller's scratch)], caps: [none])

The parts of cryptographic arithmetic *the CPU has an instruction for*. Today that is one thing: multiplication in GF(2#super[128]) --- what GCM's GHASH stands on, and what CRC is too.

#aside[There is no `unsafe` in this module][
  The *processor* emits the instruction; this source uses ordinary words (`clmul_lo`, `clmul_hi`). So nothing travels: callers stay `effects none`. Writing assembly directly would carry `unsafe` all the way up to TLS (see «The absorbing boundary» in the #chref("hardware") chapter).
]

#dtable(
  columns: 3,
  id: "mod-crypto_hw-ops",
  caption: [ops of `crypto_hw`],
  [*op*], [*what it does*], [*requires*],
  [`clmul128`], [one carry-less product --- 128 bits as two words], [`out ≥ 2`],
  [`gf128_mul`], [GF(2#super[128]) product --- GCM's reflected order, Karatsuba (three multiplies)], [`out`, `x`, `h` each `≥ 2`],
)

*What carry-less multiplication is.* Multiplication with no carries --- addition is xor. Every output bit is a combination of input bits, which is why cryptography and checksums are built on it. The answer is 128 bits and this language has no 128-bit type, so it comes back as *two words* (low, high).

*The build decides the speed.* Compiled with `lowentc --hw auto`, it uses the instruction where the machine has one and the plain computation where it does not. With `--hw none` (the default) it is always the computation. *The answer is the same either way* --- only speed and timing behaviour differ.

*Measured.* The leaf `ghash` went 66 → 594 MB/s and AES-128-GCM as a whole 26.3 → 303.7 MB/s (4 MiB, gcc -O2, best of three). Those numbers need the AES instructions turned on too (`--hw aes`): with only one of the two, the bottleneck just moves.

*What is checked* --- the golden suite compares this module's `gf128_mul` against the processor's own leaf `ghash`: the same product computed in two different places, each the other's witness. It also compares the instruction path, the computed path and the VM on the same inputs.

#aside[Constant time is not promised][
  The instruction path reads no tables, so its timing behaviour is better. Even so, this repository's cryptography promises no constant time and has not been audited --- other places remain. Saying so is how that promise stays honest.
]
