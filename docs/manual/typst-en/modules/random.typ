#import "../../typst-ko/lib.typ": *

= `random` --- random numbers (reproducible sequences · OS entropy) <mod-random>

#modhead(file: "lib/random.low", layer: [L0 --- pure computation · OS entropy via capability], caps: [`cap random` for `bytes` · `seed_from_os`])

#aside[Do not mix the two --- this warning is the whole module][
  `advance_seed`, `below_biased` and `coin` are a reproducible sequence made with *splitmix64*. They need no capability and serve tests, simulations and shuffles. `bytes` and
  `seed_from_os` are *OS entropy*. They need `cap random` and serve keys, nonces and tokens. Tests must be reproducible and keys must not be predictable --- one word cannot
  do both, so the names are split. Use a value made by `advance_seed` as a key and anyone who knows the seed knows that key, and seeds usually remain in code or logs.
]

*The reproducible side.* Takes a state (= seed) and returns the next state. The caller carries the state, so the same seed always gives the same sequence.

```lowent
var s u64 be 12345 .
set s (random.advance_seed s) .
let c bool be random.coin s .
```

*The unpredictable side.* Callable only with `cap random`. It answers the number of bytes filled, and *0 if it could not fill* --- taking 0 and using the buffer anyway means
using an uninitialised buffer as a key.

```lowent
proc make_key input k cap random . input key mut slice u8 . output bool . effects none . do
  return eq (random.bytes k key) (len key) .
end
```

#dtable(
  columns: 3,
  id: "mod-random-ops",
  caption: [Ops of `random`],
  [*op*], [*Shape*], [*Notes*],
  [`advance_seed`], [`(seed u64) → u64`], [splitmix64 --- algorithm fixed (so check values can be the reference)],
  [`below_biased`], [`(seed u64, bound u64) → u64`], [The name *confesses the bias* --- not this if you need uniformity],
  [`coin`], [`(seed u64) → bool`], [Heads · tails],
  [`bytes`], [`(cap random, dst mut slice u8) → u64`], [Bytes filled. *0 = failure*],
  [`seed_from_os`], [`(cap random, scratch mut slice u8) → u64`], [One seed from the OS --- makes the starting point of a reproducible sequence unpredictable. `scratch ≥ 8`],
)

Why `below_biased` says `biased` --- narrowing a range with the remainder makes small values come up slightly more often. Rather than hide it, the name says it.

*Tested by summoning failure.* The environment variable `LOW_HOST_FAULT="random:err"` makes entropy unavailable (→ 0), and `LOW_HOST_FAULT="random:short=3"` fills only 3
bytes. *Partial filling is the quietly frightening case* --- a key made with half the buffer still holding old values. So checking the count filled is the contract
(#chref("io-files")).

*Not built* --- uniform range sampling (rejection sampling), shuffles, distributions (normal and so on), serialising reproducible sequences, a cryptographic CSPRNG
(`bytes` asks the OS; this module does not make it).
