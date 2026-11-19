#import "../../typst-ko/lib.typ": *

= `tlscli` --- the TLS 1.3 client handshake <mod-tlscli>

#modhead(file: "lib/tlscli.low", layer: [L0 --- pure computation (the caller's backing)], caps: [none])

The *mirror image* of #modref("tlssrv")[`tlssrv`]. The server side reads a ClientHello and builds a ServerHello. This module does the
opposite: it *builds* a ClientHello and *reads* the ServerHello and what follows. The protocol's computation (key schedule, record sealing) is
in #modref("tls13")[`tls13`]; this module is the layer that calls it in the client's order.

#aside[This alone is not a TLS client][
  *No transport* --- no sockets, no reassembly; the caller gathers the bytes. That is why this module is `effects none`.
  *It does not check certificates* --- it only takes certificates out of the `Certificate` message; the chain, validity and names are
  checked by the caller with #modref("x509")[`x509`] · #modref("verify")[`verify`] · #modref("trust")[`trust`]. A tool that does not check
  must say so (`lowget`'s `--insecure`).
  Not built: HelloRetryRequest · PSK/0-RTT · session resumption · client certificates · key-exchange groups other than x25519 · suites other
  than `TLS_CHACHA20_POLY1305_SHA256` · `TLS_AES_128_GCM_SHA256`.
]

== The order of the handshake

A client accepts messages in one order only. If that order is not kept, a man in the middle can drop or swap messages. `cnext_ok` lists
*the one message that may arrive* in each state and refuses everything else.

```text
 cst_start ──sends ClientHello──▶ cst_wait_sh
 cst_wait_sh ──ServerHello(2)──▶ cst_wait_ee        ── the handshake keys are made here (hs_secrets)
 cst_wait_ee ──EncryptedExtensions(8)──▶ cst_wait_cert
 cst_wait_cert ──Certificate(11)──▶ cst_wait_cv     ── taken out with cert_at; the caller checks them
 cst_wait_cv ──CertificateVerify(15)──▶ cst_wait_finished
 cst_wait_finished ──Finished(20)──▶ cst_connected  ── check_server_fin must be true
```

The number in parentheses is the handshake message type. `cstep` gives the next state.

== ops

#dtable(
  columns: 2,
  id: "mod-tlscli-ops",
  caption: [ops of `tlscli`],
  [*op*], [*what it does*],
  [`cst_start` … `cst_connected`], [state numbers (0 … 6)],
  [`cnext_ok` · `cstep`], [may this message arrive in this state · the next state],
  [`build_ch`], [builds a ClientHello --- suites ChaCha20-Poly1305 first, then AES-128-GCM; key exchange x25519],
  [`sh_ok` · `sh_usable`], [is the ServerHello well formed · can we continue with it (decided in one call)],
  [`sh_is_hrr`], [is it a HelloRetryRequest --- recognised and *refused*],
  [`sh_suite` · `sh_is_tls13` · `sh_key_share_off`], [the chosen suite · is it really 1.3 · where the server's x25519 value is],
  [`sh_ext_off` · `sh_ext_len` · `sh_ext_find` · `sh_ext_find_len`], [the extension block, and finding one extension],
  [`hs_secrets`], [makes the handshake secrets (the key ladder)],
  [`finished_vd`], [the Finished verify value from any traffic secret],
  [`check_server_fin`], [checks the server's Finished --- "does the other side really hold the secret"],
  [`build_client_fin`], [builds our Finished],
  [`seal_rec`], [seals one record (the caller picks the inner type)],
  [`plain_hdr` · `rec_len` · `rec_type`], [builds a plaintext record header · reads a received record header],
  [`cert_at` · `cert_len`], [offset and length of the `n`-th certificate in a Certificate message (0 is the leaf)],
)

== Design

*Every length on the reading side was written by the other side.* Everything from the ServerHello on is bytes nothing has authenticated yet.
Any offset past the buffer answers 0 at once, and a length that does not fit is not read "generously".

*A HelloRetryRequest is not read as a ServerHello.* An HRR has the same message type as a ServerHello and differs only by a fixed 32-byte
value in the random field. Without knowing that, the HRR's random is taken as real and the key ladder is built on a *silently wrong* value.
HRR is not built, so it is recognised and refused.

*The extension picks the version, not the header.* The `0303` in the message header is decoration to fool middleboxes. Whether it is really
1.3 is decided by the `supported_versions` extension being `0304` (`sh_is_tls13`).

*Why ChaCha20 is offered first.* The two ciphers written in this language were measured and the faster one goes first. A server that
honours the order picks the faster one.

*`check_server_fin` being true does not yet say who.* It confirms "the other side holds the handshake secret"; who that side is, is the
certificate's question (`verify`). Both must be checked before the connection can be trusted.

*Why this module exists.* With a client, a real outside implementation (`openssl s_server`, a real web server) can stand on the other side.
Testing only against our own server (`tlssrv`) lets both sides share the same misreading, and then "we read the spec the same way" is not
confirmed. The real tool built on this module is `apps/lowget`.
