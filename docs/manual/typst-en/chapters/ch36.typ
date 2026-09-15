#import "../../typst-ko/lib.typ": *

= Input/output, networking, time, randomness, cryptography

#chapter-toc()

#prereq(
  ([#chref("io-files"), Input, output and files], [a file handle is an owned value needing completion]),
  ([#chref("capabilities"), Capabilities], [`cap net`, `cap clock` and `cap random`]),
  ([#chref("errors-design"), Designing failure], [the heart of the HTTP parser is rejection]),
)

#deepqa[
  In #chref("io-files"), why did the answer of `files.read` split into three positions?
][
  Because if one value carries both "end" and "failure", a read failure is reported as "read everything". A line-counting program really did report a read failure as the
  success "0 lines". The modules in this chapter follow the same principle --- one position per meaning, one kind per capability.
]

#why[
  Modules touching the outside easily mix two things: *computation* and *authority*. Building text to output and emitting it to standard output, computing the next random
  value and obtaining entropy from the operating system, parsing a protocol's bytes and opening a connection --- these are different jobs. Mixed, even pure computation
  drags capabilities and effects along and becomes hard to test. The modules in this chapter separate the two. We also see in what order the cryptographic modules are
  stacked towards TLS.
]

#organizer[
  You will gather output in a buffer with `outbuf` before emitting it and confirm that forgetting to flush is rejected at translation. You will learn to exchange bytes
  over `net`'s in-process connection pair, why deterministic `random.step` is separated from operating-system entropy `random.bytes`, and that monotonic clocks and wall
  clocks make different promises. You will also see what the HTTP request parser rejects, how the cryptographic modules are layered and what does not exist yet.
]

#chapter-questions()

== Buffered output --- forget to flush and translation refuses

#demo("examples/ch36/buffered.low")

- `outbuf.open 1` makes *pending output* to emit to standard output (1). It is `owned outbuf.pending`.
- `outbuf.write out p buf s` gathers `s` into the buffer, emitting and continuing when the buffer fills. It takes a pending value and returns a new one (ownership moves
  along).
- `outbuf.finish` emits the remaining bytes and ends the pending value.

The buffer is only 8 bytes, so it is flushed once partway through writing "buffered ". The output is the same. Gathering and emitting instead of `write_out` byte by byte is
the value of buffering. Forget the final `finish` and the bytes left in the buffer vanish. So translation refuses.

#demo("examples/ch36/unflushed.low")

`finish` takes `owned pending` and returns a `result`, so `pending` is a type needing completion (#chref("ownership")). The defect "the last line was not printed" shows before
running.

== Networking --- handles are resources

#demo("examples/ch36/loopback.low")

`net.pair_of` makes a pair of connections joined to each other within the process. "ping" is sent through one side (`pair_a`) and received on the other (`pair_b`), getting
4 bytes. `net.shut_pair` closes the pair. Closing can fail and gives a `result`.

`net`'s connections and listeners are *resources* like `files`' handles. If sockets are not closed, a server runs out of descriptors the longer it runs. `serve`, `dial` and
`take` open TCP loopback connections, and every op takes `cap net` as its first argument. The head shows that code not using this module cannot reach the network.

#qa[
  Why does the example use an in-process pair instead of opening a real TCP port?
][
  Examples must run identically on the VM and natively and give the same answer. Real ports give results that depend on the machine's state (ports already in use,
  firewalls). An in-process pair does not depend on outside state, so it is deterministic. Real server code uses the same `send_all` and `recv_once` through `serve` and
  `take`.
]

== Randomness --- reproducible sequences and operating-system entropy

#demo("examples/ch36/dice.low")

`random.step` *computes* the next value from a seed. It is a pure `fn` with no capabilities or effects. Rolling twice with the same seed 42 gives 3 both times. Randomness
that *must be reproducible*, as in simulations, tests and procedural generation, uses this.

Operating-system entropy is obtained with `random.bytes k dst`, which receives `cap random`. Randomness that *must not be predictable*, like keys and nonces, goes this way.
The two jobs get different names and capabilities because merged, tests that must reproduce would depend on operating-system entropy, or conversely keys would come from a
predictable sequence. `below_biased` is, as its name says, a biased range reduction. The name carries the meaning that it is not used where bias matters.

== Time --- monotonic clocks and wall clocks

#demo("examples/ch36/timing.low")

`clock.now_ns` is a *monotonic* clock. It never goes backwards, so two readings can be subtracted (`since_ns`). It is not an absolute time. The wall clock (`local_packed`,
`year_of` and so on) is the date and time people use, and it can go backwards with time zone changes or clock synchronisation. Measuring elapsed time with a wall clock can
give negative numbers. The two make different promises, so the ops differ.

Reading a clock gives different answers for the same input, breaking determinism. That is why `cap clock` is needed. But it leaves no trace outside, so the effect is `none`
(#chref("capabilities")). This example's answer differs on each run, so this book's verification script only checks it. `sleep_ms` waits, so it is the `wait` effect.

== The HTTP request parser --- its heart is rejection

#demo("examples/ch36/request_line.low")

`http.method_code` gives a request's method as a number (`GET` is 1), and `http.version_ok` answers whether the request line's version is valid. The unknown method `BREW` is
0. The parser is pure computation and has no capabilities. Receiving bytes from a connection (`net`) and interpreting them (`http`) are different modules.

The design principle of this parser is *not to accept ambiguous input*. Request smuggling lives where servers and proxies accept an ambiguous request differently. Accepting
leniently things like spaces in header names, duplicate length headers or malformed line endings creates that gap. This module does not build responses --- it is the side
that reads requests.

#misconception[A lenient parser is kind to users][
  For configuration files people write, perhaps. In network protocols, one side's leniency becomes a difference of interpretation with the other, and that difference becomes
  attack surface. What is kind at a protocol boundary is saying the reason for a rejection by name.
]

== Cryptography --- the order of the stack and the missing top

The cryptographic modules are all pure computation (L0) and are stacked in layers towards TLS 1.3.

#dtable(
  columns: 2,
  id: "net-crypto",
  caption: [The order the cryptographic modules are stacked in],
  [*Layer*], [*Modules*],
  [Derivation], [`hash` (SHA-256) · `hmac` (HMAC-SHA256, HKDF)],
  [Sealing (two suites)], [`chacha`, `poly` → `aead` · `aes` → `gcm`],
  [Key agreement], [`x25519`],
  [Signature verification], [`bigint` → `rsa` (PSS) · `p256` (ECDSA) · `ed25519`],
  [Signature generation], [`p256` → `ecdsa` (derives the nonce without randomness)],
  [Extracting keys and certificates], [`pem` → `der` (a minimal parser extracting only public keys)],
  [Protocol computation], [`tls13` (key schedule, records, transcript, Finished)],
  [Handshake], [`tlssrv` (server handshake both ways + application data records --- transport not yet)],
)

The top of each module document warns *how it goes wrong when used alone*. `chacha` is not safe alone (→ `aead`), `aes` used alone is usually wrong (→ `gcm`), `poly`'s key
must be fresh per message, and `gcm`'s nonce must never repeat. Filtering small-order points for `x25519` is the caller's job. If you use cryptography, sealing starts from
`aead`.

And the top *does not exist yet*. `tlssrv` performs the whole handshake but transport over real sockets is not there yet, and `der` is not a certificate infrastructure (PKI);
certificates are received from outside. The principle of not pretending to have what does not exist matters most in cryptography.

== Common mistakes

#antipattern[Writing again with the old pending value passed to `outbuf.write`][
  #demo("examples/ch36/mistake_pendingmoved.low")

  `outbuf.write` takes an `owned pending` and returns a *new* pending value inside its `result`. The `p` you passed has already moved, so
  using it again is `E-OWN-MOVED`. Pending values move around so that exactly one value always knows what is left in the buffer; only then
  can translation count whether the final `finish` was forgotten. Receive them in turn, `p` → `p2` → `p3`, as `buffered.low` in this chapter
  does.
]

#antipattern[Rolling twice from the same seed][
  #demo("examples/ch36/mistake_sameseed.low")

  `random.step` is a pure `fn`, so the same input always gives the same answer. Give both rolls the same `seed` and the two dice always
  match (55 with seed 2). There is no global random state, so passing on *the next state* is the caller's job.

  #demo("examples/ch36/sameseed_fixed.low")

  The fixed version starts the second step from the `s1` produced by the first and returns 56. That the same seed always yields the same two
  numbers is not a defect; it is this module's promise.
]

#misconception[`recv_once` receives everything the other side sent in one go][
  #demo("examples/ch36/recv_partial.low")

  Nine bytes, "ping pong", were sent, but one receive into a four-slot buffer gives 4. A receive returns at most the buffer's size, and only
  what has arrived by then. A stream has no message boundaries: what the sender sent in two parts may arrive at once, and what it sent at once
  may arrive in parts. If you need messages, prefix a length or define a delimiter, and receive repeatedly until the whole message is there.
]

== This chapter's syntax at a glance

#dtable(
  columns: 3,
  id: "lib-io-net-glance",
  caption: [Shapes of the I/O, network and crypto modules --- shape · meaning · why it looks this way],
  [*Shape*], [*Meaning*], [*Why*],
  [`var p owned outbuf.pending be outbuf.open 1 .`], [pending output for standard output], [forgetting it: `E-OWN-INCOMPLETE`],
  [`outbuf.write out p buf s` → `result (owned pending) …`], [gather, flush when full, return a new pending value], [one value knows --- the old one is `E-OWN-MOVED`],
  [`outbuf.finish out p buf`], [flush the rest and finish], [completion --- it can fail],
  [`net.pair_of k` · `net.send_all` · `net.recv_once` · `net.shut_pair`], [connected pair · send all · receive once · close], [`cap net` first --- a receive takes at most the buffer],
  [`random.step seed` · `random.bytes k dst`], [reproducible next state · OS entropy (`cap random`)], [computation separated from authority],
  [`clock.now_ns k` · `clock.since_ns k start`], [monotonic clock --- elapsed time], [a different promise from wall time --- `cap clock`, effect `none`],
  [`http.method_code req` · `http.version_ok req`], [parse the request line (pure)], [ambiguous input is rejected],
  [`aead` · `gcm` · `x25519` · `ed25519` · `tls13` · `tlssrv`], [sealing · key agreement · signatures · TLS computation], [pieces unsafe on their own are flagged in their docs],
)

#recap[
  `outbuf` gathers output before emitting it, and translation rejects pending values never flushed. `net` connections are resources opened with `cap net`, and closing can
  fail. `random.step` is reproducible pure computation, and `random.bytes` is entropy obtained with `cap random`. Monotonic clocks and wall clocks promise different things.
  The `http` parser is pure computation that rejects ambiguous input. The cryptographic modules are stacked as derivation, sealing, key agreement, signatures and TLS
  computation, and a TLS that transports does not exist yet.
]
