# <a id="mod-pem"></a>`pem` — unwrapping PEM envelopes

Source

`lib/pem.low`

Layer

L0 — pure computation (the caller’s backing)

Capabilities

none

Extracts **the bytes in the middle (DER)** from the folded base64 between `-----BEGIN CERTIFICATE-----` and `-----END CERTIFICATE-----` (RFC 7468). Key files have the same shape with a different label. [`codec`](sec65.md#mod-codec) knows base64 but not line folding — this module strips the header, unfolds and hands it on. Certificates come from outside (a tool like certbot leaves them as files). ACME is not built.

> **What it promises and what it does not**
>
> > **It cannot open encrypted PEM** (`Proc-Type: 4,ENCRYPTED`). **It yields only the first one** — if a file holds several (a certificate chain), the caller calls again for the later ones. No URL-safe base64. **It is a parser, not a trust decision** — it verifies nothing.

| **op** | **What it does** |
|---|---|
| `find_from` | Finds `needle` in `hay` (`len hay` if absent) |
| `body_off` | Position of **the line after** `-----BEGIN <label>-----`. 0 = absent |
| `end_off` | Position of `-----END <label>-----`. 0 = absent |
| `unwrap` | One PEM block → DER bytes. `option u64` (bytes written), `none` on failure |

*Table 50.1 — Ops of `pem`*

In `unwrap src label scratch out`, `scratch` carries two loads — assembling the header (front) and the unfolded base64 (back). `len scratch ≥ len src + label length + 16` is enough.

**Why a label is required.** `unwrap` takes by name what it opens, and rejects when the BEGIN and END labels differ. A file holding several blocks is normal. Without matching pairs, opening `CERTIFICATE` would swallow up to the next BEGIN and decode garbage — and base64 silently accepts garbage too.

**Line ends accept both LF and CRLF — the opposite of [`http`](sec94.md#mod-http).** There, the boundary is security, so a bare LF is rejected (request smuggling). Here it is a file format, and real files use both. Strictness is not a virtue but a tool — its strength depends on what it guards against.

**Why `unwrap`, not `decode`.** `utf8.decode` already exists, and when names collide there is a known defect where the processor measures types with another module’s signature even when called qualified. A test using both modules together caught that combination breaking. A library green on its own is not enough — it must compose to be usable.

The path PEM → DER → PKCS#8 → 32-byte scalar goes with [`der`](sec90.md#mod-der) — get DER with `pem.unwrap src "PRIVATE KEY" sc buf`, then find the scalar’s position with `der.p8_inner_off` and `der.ec_priv_off`.

**What is checked** — comparison with openssl output (certificate DER of 375 bytes identical, the scalar inside PKCS#8 at position 36 with length 32), rejection of label mismatch and missing envelope, VM/native agreement. The test vector’s scalar is the synthetic value `01 02 … 20` — the structure is exactly what openssl produced, so it measures the parser just as well, and it is plainly not a secret.

---

[← Prev](sec90.md) · [Contents](README.md) · [Next →](sec92.md)
