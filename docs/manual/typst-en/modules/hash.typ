#import "../../typst-ko/lib.typ": *

= `hash` --- hashing (hash map slots · corruption detection · SHA-2) <mod-hash>

#modhead(file: "lib/hash.low", layer: [L0 --- pure computation], caps: [none])

Produces numbers from byte strings. Three things of different character live in one module, and mixing them goes wrong in the dangerous direction.

#aside[`of` and `crc` are not cryptographic][
  `of` (FNV-1a 64) is for *hash map slots* --- fast and well spread, but not cryptographic. `crc` (reflected CRC-32) detects *accidental corruption* --- matching a
  desired CRC is easy, so it does not stop forgery. `digest` and `digest_ok` (SHA-256), `digest384` and `digest512` are the cryptographic hashes; proof of integrity lives there. Keyed authentication is
  not `digest` either but #modref("hmac")[`hmac`] --- a hash alone cannot answer *who sent it*.
]

```lowent
use hash .

fn slot input key slice u8 . input nslots u64 . output u64 . do
  return hash.bucket_of key nslots .
end
```

*Why the algorithms are fixed.* A hash looks right whenever a plausible number comes out. If not fixed, nothing is left to check. Because they are fixed, standard check
values become the tests' reference (CRC-32 of `"123456789"` → `0xCBF43926`), and if VM and native diverge it shows on the spot.

#dtable(
  columns: 3,
  id: "mod-hash-ops",
  caption: [Ops of `hash`],
  [*op*], [*Shape*], [*Use*],
  [`of`], [`(data slice u8) → u64`], [FNV-1a 64 --- hash map slots],
  [`bucket_of`], [`(data, nslots u64) → u64`], [Choosing a slot (remainder)],
  [`bucket_mask`], [`(data, mask u64) → u64`], [When the slot count is a power of two],
  [`crc`], [`(data slice u8) → u64`], [Reflected CRC-32 --- corruption detection],
  [`crc_ok`], [`(data, want u64) → bool`], [Compare with a check value],
  [`maybe_same`], [`(a, b slice u8) → bool`], [Are the hashes equal --- not proof of equality],
  [`digest`], [`(data, out mut slice u8) → u64`], [SHA-256 --- bytes written (32)],
  [`digest_ok`], [`(data, out mut slice u8, want slice u8) → bool`], [Compare with a received digest],
  [`digest384`], [`(data, out mut slice u8) → u64`], [SHA-384 --- bytes written (48)],
  [`digest512`], [`(data, out mut slice u8) → u64`], [SHA-512 --- bytes written (64)],
  [`digest_eq`], [`(a, b slice u8) → bool`], [Are two digests equal --- reads to the end whatever the length],
)

Why `digest_ok` exists --- the right use of a hash is not a round trip but *comparison*. Recomputing and matching against what was received is the only correct use, so
that shape is an op (if `want` is not 32 bytes it is simply `false` --- the contract starts with the length). Why `maybe_same` says `maybe` --- equal hashes do not mean
equal values. Calling it `same` would have made the name a lie.

`digest384` and `digest512` follow the same rule --- the digest goes into the caller's buffer, and a buffer that is too short gets the answer 0 and nothing written. To
compare with a received digest use `digest_eq`. It does not stop at the first differing byte, so the time it takes does not tell how far the two agreed.

MD5 and SHA-1 are not in this module. They are broken hashes and live apart in #modref("hash_legacy")[`hash_legacy`], for places that must match something old.

*Not built* --- keyed hashing (that is `hmac`), seedable hashing (HashDoS defence), streaming (feeding in pieces), SHA-3.
