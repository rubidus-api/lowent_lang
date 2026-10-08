# <a id="mod-tls13"></a>`tls13` — the computational parts of TLS 1.3

Source

`lib/tls13.low`

Layer

L0 — pure computation (the caller’s backing)

Capabilities

none

Four pure computations TLS 1.3 requires (RFC 8446) — the **key schedule** (deriving traffic keys from the shared secret and transcript hash, §7.1), the **record layer** (sealing and opening records, §5), the **transcript hash** (the hash of every message exchanged so far, §4.4.1), and **Finished** (proof that the handshake was not altered along the way, §4.4.4). All are pure, so they can be built without transport and measured against test vectors — parser first, transport later.

> **This is not a TLS implementation**
>
> > There is no state machine — it does not drive the handshake (message layer and ordering are [`tlssrv`](sec98.md#mod-tlssrv)’s). There is no transport. No PSK, 0-RTT, exporter or resumption secrets; only the 1-RTT path was built. No PKI ([`der`](sec93.md#mod-der)). It does not promise constant time and has not been audited. There are two suites, and unknown suites are rejected.

```text
0 ─HKDF-Extract(PSK)→ Early Secret ─Derive-Secret("derived","")→ ┐
ECDHE ─HKDF-Extract────────────────→ Handshake Secret ←──────────┘
  ├─ Derive-Secret("c hs traffic", CH..SH)
  └─ Derive-Secret("s hs traffic", CH..SH)
─Derive-Secret("derived","")→ ┐
0 ─HKDF-Extract──────────────→ Master Secret
  ├─ Derive-Secret("c ap traffic", CH..server Finished)
  └─ Derive-Secret("s ap traffic", CH..server Finished)
```

Record keys come from each secret — `key = Expand-Label(secret, "key", "", length)`, `iv = Expand-Label(secret, "iv", "", 12)`.

| **op** | **What it does** |
|---|---|
| `build_label` · `expand_label` | HkdfLabel structure to bytes · `HKDF-Expand-Label` |
| `derive_secret` | `Derive-Secret(secret, label, transcript hash)` |
| `advance` | One rung of the ladder — one arrow in the diagram above |
| `traffic_key` · `traffic_iv` | Secret → record key · IV |
| `finished_key` · `verify_data` | Key and value of Finished |
| `record_header` · `record_nonce` | 5-byte header `23 ‖ 0x0303 ‖ length` · IV and sequence number → nonce |
| `record_seal` · `record_open` | Sealing · opening a record |
| `inner_type` | Reads the real content type from the **end** of the inner plaintext |
| `transcript` | Hash of a buffer of concatenated messages |
| `hs_type` · `hs_size` · `hs_count` | Walking handshake messages |
| `check_finished` | Recomputes the peer’s Finished and compares |

*Table 50.1 — Ops of `tls13`*

**A record’s outside differs from its inside.** The outside is always `23 ‖ 0x0303 ‖ length` — handshake and application data look the same. The real content type is at the end of the inner plaintext, arranged to hide it from observers. The AAD is that 5-byte header itself.

**Two suites give negotiation meaning.** Suite 1 = `TLS_AES_128_GCM_SHA256` (MUST, [`gcm`](sec85.md#mod-gcm)), suite 2 = `TLS_CHACHA20_POLY1305_SHA256` (SHOULD, [`aead`](sec82.md#mod-aead)). With only one built, this place was a dead branch. Unknown suites are rejected — it does not silently pick one.

**The transcript hash is the backbone of the handshake.** Keys and Finished alike stand on the hash of “every message exchanged so far”, so if a man in the middle changes even one byte, the two sides’ keys diverge. **Where the transcript is cut is half the standard** — RFC 8448 records those points as each Derive-Secret’s hash, and tests compare all three (`CH…SH`, `CH…server Finished`, `CH…client Finished`). SHA-256 here works in one shot (no streaming), so the transcript is measured over **a buffer the caller concatenated**. Message walking does not trust length fields — if a length exceeding the buffer is written, it stops there. `check_finished` accumulates XOR over 32 bytes and looks once at the end (no early return).

**Why workspaces are bundled into two.** The parameter limit is 16, so laying out buffers per suite overflows quickly. So they are bundled into bytes `w` and `u64` `u`, with positions written in source comments. It is not pretty — it is the shape the language’s limit made, and better not hidden.

**What is checked — two layers.** §7.1 independently implemented with Python `hashlib` and `hmac` for byte comparison, and 96 values of RFC 8448 §3 extracted mechanically from the canonical text and compared. **And the canonical text caught a defect.** `traffic_key` had the key length fixed at 32, but that length goes inside HkdfLabel, so with a peer using AES-128-GCM (16) the key differs entirely. The second implementation did not catch it — the same person wrote 32 in both. A second implementation is not the canonical source.

---

[← Prev](sec96.md) · [Contents](README.md) · [Next →](sec98.md)
