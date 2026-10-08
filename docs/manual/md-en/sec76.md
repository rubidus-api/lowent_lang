# <a id="mod-hash_legacy"></a>`hash_legacy` — old hashes (MD5 · SHA-1)

Source

`lib/hash_legacy.low`

Layer

L0 — pure computation

Capabilities

none

Produces MD5 and SHA-1. Both are **broken hashes**, and this module exists only for places that must speak with something old.

> **Do not use them in a new design**
>
> > For both hashes, different inputs with the same digest can be built — real collisions appeared in 2004 for MD5 and in 2017 for SHA-1. They must not be used for signatures, certificates, passwords or tamper detection. That is the job of `digest` (SHA-256), `digest384` and `digest512` in [`hash`](sec75.md#mod-hash). There is one reason to use this module: checking a checksum that was already written with MD5 or SHA-1, a storage format that names things by such a hash, one step of an old protocol. In those places the question is not “is it safe” but “does it give the same answer as the other side”.

```lowent
use hash_legacy .

proc sum input data slice u8 . input work mut slice u64 . input out mut slice u8 . output u64 . effects none . do
  return hash_legacy.md5 data work out .
end
```

**Why a separate module.** The name is the warning. Written `hash.md5`, it would read with the same weight as `hash.digest`. `hash_legacy.md5` makes the reader stop once. And this module will go away some day — on that day `hash` does not change by a line.

**Why not a builtin.** It is written entirely in this language. SHA-2 is a leaf of the compiler because of cost, and matching something old is not a hot path. It is slower, but it leans on nothing in the compiler, so removing this module leaves the compiler unchanged.

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
