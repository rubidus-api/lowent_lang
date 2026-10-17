#import "../../typst-ko/lib.typ": *

= `rsa` --- RSASSA-PSS verification <mod-rsa>

#modhead(file: "lib/rsa.low", layer: [L0 --- pure computation], caps: [none])

Answers whether an RSA-PSS signature from a peer matches the public key and message (RFC 8017 §8.1.2). TLS 1.3's `rsa_pss_rsae_sha256` is this.

#aside[What it promises and what it does not][
  It does not promise constant time and has not been audited. It does *PSS only* --- PKCS\#1 v1.5 is not built, because RFC 8446 §4.4.3 forbids it in CertificateVerify.
  There is no SHA-1 family, key generation or decryption (RSA encryption). Signing is not built either --- a TLS 1.3 server may need it someday, but what came first is
  #modref("p256")[`p256`], with smaller keys and signatures.
]

The old PKCS\#1 v1.5 had deterministic padding and shallow structure, and attracted several attacks. PSS mixes in a salt and covers it with a mask. PSS verification has
three pieces.

```text
① s^e mod n  →  EM (encoded message, emLen = key length)
② make a mask with MGF1(SHA-256) and strip DB
③ does H' = SHA-256(0x00×8 ‖ mHash ‖ salt) equal H inside EM
```

① is `mod_exp` of #modref("bigint")[`bigint`], and ② and ③ are this module's job. The eight `0x00×8` bytes are not decoration --- they are a prefix the standard added to
separate signed content from certificate signatures. Leaving them out lets signatures from another context be reused.

#dtable(
  columns: 3,
  id: "mod-rsa-ops",
  caption: [Ops of `rsa`],
  [*op*], [*What it does*], [*Requires*],
  [`mgf1`], [The MGF1-SHA256 mask generation function], [`out ≥ n` · `w` is backing],
  [`pss_verify`], [Given EM, is the PSS encoding right], [only ② and ③],
  [`verify_pss`], [signature · modulus · exponent · hash → 0 or 1], [all of ①②③],
)

Usually only `verify_pss` is used. `pss_verify` is exposed separately to test ① apart from ② and ③.

*What is checked* --- standard PSS test vectors (positive), two negatives (one bit of signature · one bit of hash), MGF1 against a reference implementation, and VM/native
agreement. There are two negatives because changing the signature and changing the message are different failures --- checking only one cannot filter out an implementation
that lets the other pass.
