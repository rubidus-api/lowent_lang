# <a id="mod-tlssrv"></a>`tlssrv` — the TLS 1.3 server handshake

Source

`lib/tlssrv.low`

Layer

L0 — pure computation (the caller’s backing)

Capabilities

none

[`tls13`](sec97.md#mod-tls13) holds the **computations** of the standard. This module is the **layer that calls them in order** — reading ClientHello, building server messages, moving state, and sealing and opening application data records. Every failure is `0`, and ops yield **positions** — the caller cuts with `subslice`.

> **This alone is not a TLS server**
>
> > **There is no transport** — no sockets, no reassembly; the caller gathers the bytes (and decides record boundaries). No KeyUpdate, record padding, HelloRetryRequest, PSK, 0-RTT, client certificate requests or session resumption. `build_cert` holds only one certificate of a chain. Extensions are only **found** (interpreting SNI and ALPN is the caller’s). It has not been audited.

**ClientHello is the most hostile input.** It is a byte string nothing has authenticated yet, and every length field was written by the other side. So — any position exceeding the buffer fails immediately. Extension walking does not recurse and bounds its steps by the buffer size (a run of zero-length extensions never ends an unbounded loop). Mismatched lengths are not read charitably — `ch_ok` requires the declared length to equal the buffer **exactly**, and `ch_ext_len` requires extensions to fit to the end.

| **op** | **What it does** |
|---|---|
| `st_start` · `st_recvd_ch` · `st_negotiated` · `st_wait_flight2` · `st_wait_finished` · `st_connected` | State numbers |
| `hs_client_hello` · `hs_server_hello` · `hs_encrypted_extensions` · `hs_certificate` · `hs_certificate_verify` · `hs_finished` | Message type numbers |
| `next_ok` · `transit` | May this message be received now · move state (yields the same state if it cannot) |
| `ch_ok` | Do type and length match the buffer |
| `ch_random_off` · `ch_sid_off` · `ch_sid_len` | Random · session_id |
| `ch_suites_off` · `ch_suites_len` · `ch_has_suite` | Offered suites |
| `ch_ext_off` · `ch_ext_len` · `ch_ext_find` · `ch_ext_find_len` | Extension block · find by type |
| `ch_x25519_off` · `be16` | Position of the x25519 public key in key_share · big-endian 2 bytes |
| `build_sh` · `build_ee` · `build_cert` | Build ServerHello · EncryptedExtensions · Certificate (one DER) |
| `cv_content` · `build_cv` · `build_fin` | The 130 bytes CertificateVerify signs · build CertificateVerify · Finished |
| `server_finished` | From ECDHE up the key schedule to the server Finished in one line |
| `app_secrets` · `check_client_finished` | Application traffic secrets (c · s) · check the peer’s Finished (1 = match) |
| `traffic_keys` · `seal_app` · `open_app` | Secret → key ‖ IV (one direction at a time) · seal · open application data records |

*Table 50.1 — Ops of `tlssrv`*

Message building shares one frame (`<type 1> <length 3> <body>`) — so the frame is written once. Repeating the same arithmetic in four places brings the day only one is fixed.

**`cv_content` — the 64 spaces are not decoration.** The signed content is `0x20 × 64 ‖ "TLS 1.3, server CertificateVerify" ‖ 0x00 ‖ transcript hash` (§4.4.3). Without that prefix, this signature could be reused as a signature in another context (a certificate signature, a client-side signature). Constants in a standard that make you ask “why is this here” are usually traces of attacks that already happened.

**Order is half the standard.** `next_ok` enumerates what is accepted, and enumerating is rejecting everything else. Without it a man in the middle could send Finished early or ClientHello twice. **`session_id` is echoed back** — TLS 1.3 does not use that field, but not echoing it gets connections cut by middleboxes on real networks, because it is a compatibility device to look like 1.2 (§4.1.3).

**The transcript is cut three times in this layer.** `s hs traffic` stands on `CH‖SH`, the server Finished on `CH‖…‖CertificateVerify`, and application traffic secrets and **the client Finished** on `CH‖…‖server Finished`. The client Finished does not include itself, and its verify_data comes from the **client** handshake secret — same transcript, different secret. Using the server’s always rejects, and the symptom looks like “the client is broken”.

**Application data uses a different secret and a different sequence per direction.** Sharing one makes nonces overlap, and overlapping nonces in AEAD lose plaintext and authentication key together. So key bundles are made one direction at a time. Sequences increase **per record** and restart at 0 per key generation. The output buffer of `traffic_keys` must be 32 bytes even when the key is 16 — `expand_label` uses one HMAC block and always writes 32. Alerts are the same records too — the inner type is 21, and without looking at it an alert is read as data.

**What is checked** — the real handshake of RFC 8448 §3. The canonical ClientHello’s fields are read, and five messages are built and compared byte for byte with the canonical ones (ServerHello 90 · EncryptedExtensions 40 · Certificate 445 · CertificateVerify 136 · Finished 36). And it is woven end to end — we compute ECDHE from the canonical keys, climb the key schedule and match the server Finished with the canonical one. That one move measures the transcript cut points, the key schedule, finished_key and verify_data together. The canonical CertificateVerify is RSA-PSS and our signer is ECDSA, so the frame is what is measured here; the signature itself is measured separately by [`ecdsa`](sec91.md#mod-ecdsa).

---

[← Prev](sec97.md) · [Contents](README.md) · [Next →](sec99.md)
