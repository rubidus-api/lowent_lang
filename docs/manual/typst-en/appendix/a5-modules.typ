#import "../../typst-ko/lib.typ": *

= Appendix E --- Standard library modules

Part IX (#chrange("lib-map", "lib-terminal")) toured the standard library by use. This appendix is a *reference*, one page per module, gathering what each
does, which ops it offers and what it does not do. It is not a place to read from the start but a place to open when needed.

Module pages share one shape. The head gives the source file, layer and capabilities received, followed by what the module does, its ops at a glance, how to
use it, design and boundaries, and what is not built. The maturity each module writes (`experimental`, `incubating`, `standard`) is authoritatively in the source
head and the development repository's ledger and moves between editions, so it is not copied here. The conventions every module follows --- the caller's buffer,
failure as a value, all or nothing, ownership that must be settled --- are in #chref("lib-map").

Module names may differ from file names. What `use` looks for is the `module` declaration in the file (`lib/str.low` is `strings`, `lib/alloc.low` is `allocs`).
Cross-module names are always called qualified (`strings.find`), and a frequently used one can get an alias with `use codec as c .`.

#dtable(
  columns: 2,
  id: "a5-l0",
  caption: [L0 --- pure computation (`effects none` · the caller's buffer)],
  [*Module*], [*In one line*],
  [#modref("strings")[`strings`]], [String view operations --- cut, find, compare],
  [#modref("strbuf")[`strbuf`]], [Owned string buffer and null-terminated `cstr`],
  [#modref("fmt")[`fmt`]], [Formatting assembled into the caller's buffer],
  [#modref("utf8")[`utf8`]], [UTF-8 code point iteration and validation],
  [#modref("utf16")[`utf16`]], [UTF-16 surrogate arithmetic],
  [#modref("unicode")[`unicode`]], [Unicode property tables --- extracted mechanically],
  [#modref("codec")[`codec`]], [Hex · base64],
  [#modref("regex")[`regex`]], [Pike VM regular expressions without backtracking],
  [#modref("term")[`term`]], [The pure half of a terminal renderer --- ANSI assembly · screen diff · width],
  [#modref("sortlib")[`sortlib`]], [In-place quicksort for `u64`],
  [#modref("sortgen")[`sortgen`]], [Generic sort --- the type brings the comparison],
  [#modref("searchlib")[`searchlib`]], [Binary search over sorted slices],
  [#modref("hashmap")[`hashmap`]], [`u64 → u64` open addressing],
  [#modref("strmap")[`strmap`]], [Byte string → `u64` hash map],
  [#modref("vecs")[`vecs`]], [Growing byte vector],
  [#modref("spsc")[`spsc`]], [Lock-free single-producer single-consumer ring buffer],
  [#modref("hash")[`hash`]], [Hash-map slots (FNV-1a) · damage detection (CRC-32) · SHA-256 · SHA-384 · SHA-512],
  [#modref("hash_legacy")[`hash_legacy`]], [Old hashes MD5 · SHA-1 --- only to match something old],
  [#modref("math")[`math`]], [Floating-point maths --- `close` instead of `==`],
  [#modref("random")[`random`]], [Separates reproducible sequences from OS entropy by name],
  [#modref("hmac")[`hmac`]], [HMAC-SHA256 · HKDF --- who sent it],
  [#modref("chacha")[`chacha`]], [ChaCha20 stream --- not safe alone],
  [#modref("poly")[`poly`]], [Poly1305 --- a fresh key per message],
  [#modref("aead")[`aead`]], [ChaCha20-Poly1305 seal and open --- cryptography starts here],
  [#modref("x25519")[`x25519`]], [Key agreement],
  [#modref("aes")[`aes`]], [AES-128 block cipher --- usually wrong used alone],
  [#modref("gcm")[`gcm`]], [AES-128-GCM --- never repeat a nonce],
  [#modref("bigint")[`bigint`]], [Big-number modular arithmetic],
  [#modref("rsa")[`rsa`]], [RSASSA-PSS verification],
  [#modref("p256")[`p256`]], [NIST P-256 curve and ECDSA verification],
  [#modref("p384")[`p384`]], [NIST P-384 curve and ECDSA verification --- the public web's intermediates],
  [#modref("ecdsa")[`ecdsa`]], [ECDSA P-256 signing --- nonce derived without randomness],
  [#modref("ed25519")[`ed25519`]], [Ed25519 signature verification],
  [#modref("der")[`der`]], [Minimal DER parser --- extracts public keys only],
  [#modref("pem")[`pem`]], [Unwrapping PEM envelopes],
  [#modref("x509")[`x509`]], [Reading X.509 certificates --- offsets and lengths only],
  [#modref("verify")[`verify`]], [Certificate signatures and one link of a chain],
  [#modref("tls13")[`tls13`]], [TLS 1.3 parts --- key schedule · records · transcript · Finished],
  [#modref("tlssrv")[`tlssrv`]], [Server TLS handshake and application data records],
  [#modref("tlscli")[`tlscli`]], [Client TLS 1.3 handshake --- the caller checks certificates],
  [#modref("http")[`http`]], [HTTP/1.1 request parser --- its heart is rejection],
  [#modref("soa")[`soa`]], [A trial of laying structs out as per-field arrays],
)

#dtable(
  columns: 2,
  id: "a5-l1",
  caption: [L1 --- storage (needs the allocation capability or borrowed bytes)],
  [*Module*], [*In one line*],
  [#modref("allocs")[`allocs`]], [Allocator trait · bump allocators · two default allocators],
  [#modref("pool")[`pool`]], [Generational-handle block pool],
  [#modref("shard")[`shard`]], [Tokens splitting storage into non-overlapping pieces],
  [#modref("budget")[`budget`]], [Bit budgets for handles and retiring generations],
  [#modref("wire")[`wire`]], [Fields in one word --- a mask carries position and width],
  [#modref("flags")[`flags`]], [Named on/off settings in one word],
  [#modref("segarena")[`segarena`]], [Fixed-size segment arena],
  [#modref("pagecache")[`pagecache`]], [Page ids and pin tokens],
  [#modref("growvec")[`growvec`]], [Growing byte vector --- short name for `vecgen.vec u8`],
  [#modref("vecgen")[`vecgen`]], [Generic growing vector `vec t`],
  [#modref("mapgen")[`mapgen`]], [Generic hash map `table k v`],
  [#modref("nodelist")[`nodelist`]], [Fixed-size intrusive list],
  [#modref("segview")[`segview`]], [Cursor, total length and flattening for scattered views],
  [#modref("lifemode")[`lifemode`]], [`lifemode` · `lifeatom` --- when a value ends],
)

#dtable(
  columns: 2,
  id: "a5-l2",
  caption: [L2 --- host (needs capabilities)],
  [*Module*], [*In one line*],
  [#modref("io")[`io`]], [Stream reading over slices],
  [#modref("outbuf")[`outbuf`]], [Buffered output --- forgetting to flush is rejected],
  [#modref("files")[`files`]], [Files and directories --- forgetting to close is rejected],
  [#modref("tty")[`tty`]], [Terminal input --- raw mode · reading keys · pure key parsing],
  [#modref("net")[`net`]], [Sockets --- TCP loopback · in-process connection pairs],
  [#modref("clock")[`clock`]], [Time and deadlines --- monotonic and wall clocks],
  [#modref("trust")[`trust`]], [Finding a trusted root in the trust store --- the bundle is streamed],
)

The cryptographic modules are stacked in layers towards TLS 1.3; that order and the top that does not exist yet are in #chref("lib-io-net").
