# <a id="mod-hash_legacy"></a>`hash_legacy` — old hashes (MD5 · SHA-1)

Source

`lib/hash_legacy.low`

Layer

L0 — pure computation

Capabilities

none

Produces MD5 and SHA-1. Neither may be used for **security**. Where security is not the point — checksums, naming things by their content, reading old files — they are still widely used.

> **Not for security**
>
> > For both hashes, different inputs with the same digest can be built — real collisions appeared in 2004 for MD5 and in 2017 for SHA-1. They must not be used for signatures, certificates, passwords or tamper detection. That is the job of `sha256` (SHA-256), `sha384` and `sha512` in [`hash`](sec75.md#mod-hash). This module fits where nobody is trying to deceive: a checksum that tells whether a download arrived intact, a storage format that names things by their content, a cache key, and reading old files that were already written with MD5 or SHA-1.

```lowent
use hash_legacy .

proc sum input data slice u8 . input work mut slice u64 . input out mut slice u8 . output u64 . effects none . do
  return hash_legacy.md5 data work out .
end
```

**Why a separate module.** The name is the warning. Written `hash.md5`, it would read with the same weight as `hash.sha256`. `hash_legacy.md5` makes the reader stop once. It does not mean the module is going away — it is kept apart so that it is not mixed with the hashes meant for security.

**Why not a builtin.** It is written entirely in this language. SHA-2 is a leaf of the compiler because of cost, and checksums and reading old files are not that hot a path. It is slower, but it leans on nothing in the compiler.

| **op** | **Shape** | **Use** |
|---|---|---|
| `md5` | `(data slice u8, work mut slice u64, out mut slice u8) → u64` | MD5 (RFC 1321) — bytes written (16) |
| `sha1` | `(data slice u8, work mut slice u64, out mut slice u8) → u64` | SHA-1 (FIPS 180) — bytes written (20) |

*Table 50.1 — The ops of `hash_legacy`*

`work` is working space given by the caller — `md5` needs 16 words and `sha1` needs 80. The library does not allocate behind your back. If `out` or `work` is too short the answer is 0 and **nothing is written** — a digest that was quietly cut short looks right.

The tests compare with the answers the standards give: the test suite of RFC 1321 Appendix A.5 for MD5 and the examples of FIPS 180 for SHA-1, and separately the boundaries where the padding takes one more block (lengths 55, 56, 63 and 64). The VM and the native build give the same answers.

**Not built** — streaming (feeding in pieces), HMAC-MD5 and HMAC-SHA1, even older hashes such as MD4.

---

[← Prev](sec75.md) · [Contents](README.md) · [Next →](sec77.md)
