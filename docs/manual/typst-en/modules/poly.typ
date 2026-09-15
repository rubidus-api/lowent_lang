#import "../../typst-ko/lib.typ": *

= `poly` --- Poly1305 one-time authenticator <mod-poly>

#modhead(file: "lib/poly.low", layer: [L0 --- pure computation], caps: [none])

Feeds a message in 16-byte blocks and produces a *16-byte tag*. It testifies that "someone who knows this key sent this byte string" (RFC 8439 §2.5).

#aside[The key must be fresh for every message][
  Poly1305 is a *one-time* authenticator. Authenticating two messages with the same key lets the key be recovered, after which forgery is possible. That is why
  #modref("aead")[`aead`] makes a one-time key from the nonce for every message (`key_gen`). If you use this module directly, you carry that discipline yourself. It does
  not promise constant time (there are bounds checks and stops) and has not been audited. Comparing tags is the caller's job --- compare without early return (as
  `aead.unseal` does).
]

It does 130-bit arithmetic on `u64` split into *five 26-bit limbs*. The largest product is around 2#super[52], safe inside `u64` --- *the limb layout is the safety argument*,
which is why that number is written at the top of the source.

#dtable(
  columns: 2,
  id: "mod-poly-ops",
  caption: [Ops of `poly`],
  [*op*], [*What it does*],
  [`setup`], [Sets up state (five h · five r) from the key],
  [`block`], [Feeds one 16-byte block. `addhi` is 1 for a full block],
  [`emit`], [Produces the 16-byte tag from state and key],
)

`addhi` is an argument because a short last block must place the top 1 bit differently. `aead` *fills every block* with `pad16`, so it always passes 1 --- the saying that
padding exists to reduce cases to one becomes concrete here.

*What is checked* --- the RFC 8439 §2.5.2 tag vector and VM/native agreement. *Not built* --- constant-time guarantees, tag comparison, detection of key reuse,
serialising streaming state.
